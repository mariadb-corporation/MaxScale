/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/gw_authenticator.h>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/secrets.h>
#include <maxscale/mysql_utils.h>
#include "gssapi_auth.h"

/** Query that gets all users that authenticate via the gssapi plugin */
const char *gssapi_users_query =
    "SELECT u.user, u.host, d.db FROM "
    "mysql.user AS u JOIN mysql.db AS d "
    "ON (u.user = d.user AND u.host = d.host) WHERE u.plugin = 'gssapi' "
    "UNION "
    "SELECT u.user, u.host, t.db FROM "
    "mysql.user AS u JOIN mysql.tables_priv AS t "
    "ON (u.user = t.user AND u.host = t.host) WHERE u.plugin = 'gssapi';";

#define GSSAPI_USERS_QUERY_NUM_FIELDS 3

typedef struct gssapi_instance
{
    char *principal_name;
} GSSAPI_INSTANCE;

/**
 * @brief Initialize the GSSAPI authenticator
 *
 * This function processes the service principal name that is given to the client.
 *
 * @param listener Listener port
 * @param options Listener options
 * @return Authenticator instance
 */
void* gssapi_auth_init(char **options)
{
    GSSAPI_INSTANCE *instance = MXS_MALLOC(sizeof(GSSAPI_INSTANCE));

    if (instance)
    {
        instance->principal_name = NULL;

        for (int i = 0; options[i]; i++)
        {
            if (strstr(options[i], "principal_name"))
            {
                char *ptr = strchr(options[i], '=');
                if (ptr)
                {
                    ptr++;
                    instance->principal_name = MXS_STRDUP_A(ptr);
                }
            }
            else
            {
                MXS_ERROR("Unknown option: %s", options[i]);
                MXS_FREE(instance->principal_name);
                MXS_FREE(instance);
                return NULL;
            }
        }

        if (instance->principal_name == NULL)
        {
            instance->principal_name = MXS_STRDUP_A(default_princ_name);
            MXS_NOTICE("Using default principal name: %s", instance->principal_name);
        }
    }

    return instance;
}

/**
 * @brief Create a AuthSwitchRequest packet
 *
 * This function also contains the first part of the GSSAPI authentication.
 * The server (MaxScale) send the principal name that will be used to generate
 * the token the client will send us. The principal name needs to exist in the
 * GSSAPI server in order for the client to be able to request a token.
 *
 * @return Allocated packet or NULL if memory allocation failed
 * @see https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::AuthSwitchRequest
 * @see https://web.mit.edu/kerberos/krb5-1.5/krb5-1.5.4/doc/krb5-user/What-is-a-Kerberos-Principal_003f.html
 */
static GWBUF* create_auth_change_packet(GSSAPI_INSTANCE *instance, gssapi_auth_t *auth)
{
    size_t principal_name_len = strlen(instance->principal_name);
    size_t plen = sizeof(auth_plugin_name) + 1 + principal_name_len;
    GWBUF *buffer = gwbuf_alloc(plen + MYSQL_HEADER_LEN);

    if (buffer)
    {
        uint8_t *data = (uint8_t*)GWBUF_DATA(buffer);
        gw_mysql_set_byte3(data, plen);
        data += 3;
        *data++ = ++auth->sequence; // Second packet
        *data++ = 0xfe; // AuthSwitchRequest command
        memcpy(data, auth_plugin_name, sizeof(auth_plugin_name)); // Plugin name
        data += sizeof(auth_plugin_name);
        memcpy(data, instance->principal_name, principal_name_len); // Plugin data
    }

    return buffer;
}

/**
 * @brief Store the client's GSSAPI token
 *
 * This token will be shared with all the DCBs for this session when the backend
 * GSSAPI authentication is done.
 *
 * @param dcb Client DCB
 * @param buffer Buffer containing the key
 * @return True on success, false if memory allocation failed
 */
bool store_client_token(DCB *dcb, GWBUF *buffer)
{
    bool rval = false;
    uint8_t hdr[MYSQL_HEADER_LEN];

    if (gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, hdr) == MYSQL_HEADER_LEN)
    {
        size_t plen = gw_mysql_get_byte3(hdr);
        MYSQL_session *ses = (MYSQL_session*)dcb->data;

        if ((ses->auth_token = MXS_MALLOC(plen)))
        {
            gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, plen, ses->auth_token);
            ses->auth_token_len = plen;
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Copy username to shared session data
 * @param dcb Client DCB
 * @param buffer Buffer containing the first authentication response
 */
static void copy_client_information(DCB *dcb, GWBUF *buffer)
{
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;
    gwbuf_copy_data(buffer, MYSQL_SEQ_OFFSET, 1, &auth->sequence);
}

/**
 * @brief Extract data from client response
 *
 * @param dcb Client DCB
 * @param read_buffer Buffer containing the client's response
 * @return MXS_AUTH_SUCCEEDED if authentication can continue, MXS_AUTH_FAILED if
 * authentication failed
 */
static int gssapi_auth_extract(DCB *dcb, GWBUF *read_buffer)
{
    int rval = MXS_AUTH_FAILED;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

    switch (auth->state)
    {
        case GSSAPI_AUTH_INIT:
            copy_client_information(dcb, read_buffer);
            rval = MXS_AUTH_SUCCEEDED;
            break;

        case GSSAPI_AUTH_DATA_SENT:
            store_client_token(dcb, read_buffer);
            rval = MXS_AUTH_SUCCEEDED;
            break;

        default:
            MXS_ERROR("Unexpected authentication state: %d", auth->state);
            ss_dassert(false);
            break;
    }

    return rval;
}

/**
 * @brief Is the client SSL capable
 *
 * @param dcb Client DCB
 * @return True if client supports SSL
 */
bool gssapi_auth_connectssl(DCB *dcb)
{
    MySQLProtocol *protocol = (MySQLProtocol*)dcb->protocol;
    return protocol->client_capabilities & GW_MYSQL_CAPABILITIES_SSL;
}

static gss_name_t server_name = GSS_C_NO_NAME;

/**
 * @brief Check if the client token is valid
 *
 * @param token Client token
 * @param len Length of the token
 * @return True if client token is valid
 */
static bool validate_gssapi_token(uint8_t* token, size_t len)
{
    OM_uint32 major = 0, minor = 0;
    gss_buffer_desc server_buf = {0, 0};
    gss_cred_id_t credentials;

    /** TODO: Make this configurable */
    server_buf.value = (void*)default_princ_name;
    server_buf.length = sizeof(default_princ_name);

    major = gss_import_name(&minor, &server_buf, GSS_C_NT_USER_NAME, &server_name);

    if (GSS_ERROR(major))
    {
        report_error(major, minor);
        return false;
    }

    major = gss_acquire_cred(&minor, server_name, GSS_C_INDEFINITE,
                             GSS_C_NO_OID_SET, GSS_C_ACCEPT,
                             &credentials, NULL, NULL);
    if (GSS_ERROR(major))
    {
        report_error(major, minor);
        return false;
    }

    do
    {

        gss_ctx_id_t handle = NULL;
        gss_buffer_desc in = {0, 0};
        gss_buffer_desc out = {0, 0};
        gss_OID_desc *oid;


        in.value = token;
        in.length = len;

        major = gss_accept_sec_context(&minor, &handle, GSS_C_NO_CREDENTIAL,
                                       &in, GSS_C_NO_CHANNEL_BINDINGS,
                                       &server_name, &oid, &out,
                                       0, 0, NULL);
        if (GSS_ERROR(major))
        {
            return false;
            report_error(major, minor);
        }
    }
    while (major & GSS_S_CONTINUE_NEEDED);

    return true;
}

/**
 * @brief Authenticate the client
 *
 * @param dcb Client DCB
 * @return MXS_AUTH_INCOMPLETE if authentication is not yet complete, MXS_AUTH_SUCCEEDED
 * if authentication was successfully completed or MXS_AUTH_FAILED if authentication
 * has failed.
 */
int gssapi_auth_authenticate(DCB *dcb)
{
    int rval = MXS_AUTH_FAILED;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;
    GSSAPI_INSTANCE *instance = (GSSAPI_INSTANCE*)dcb->listener->auth_instance;

    if (auth->state == GSSAPI_AUTH_INIT)
    {
        /** We need to send the authentication switch packet to change the
         * authentication to something other than the 'mysql_native_password'
         * method */
        GWBUF *buffer = create_auth_change_packet(instance, auth);

        if (buffer && dcb->func.write(dcb, buffer))
        {
            auth->state = GSSAPI_AUTH_DATA_SENT;
            rval = MXS_AUTH_INCOMPLETE;
        }
    }
    else if (auth->state == GSSAPI_AUTH_DATA_SENT)
    {
        /** We sent the principal name and the client responded with the GSSAPI
         * token that we must validate */

        MYSQL_session *ses = (MYSQL_session*)dcb->data;

        if (validate_gssapi_token(ses->auth_token, ses->auth_token_len))
        {
            rval = MXS_AUTH_SUCCEEDED;
        }
    }

    return rval;
}

/**
 * @brief Free authenticator data from a DCB
 *
 * @param dcb DCB to free
 */
void gssapi_auth_free_data(DCB *dcb)
{
    if (dcb->data)
    {
        MYSQL_session *ses = dcb->data;
        MXS_FREE(ses->auth_token);
        MXS_FREE(ses);
        dcb->data = NULL;
    }
}

/**
 * @brief Load database users that use GSSAPI authentication
 *
 * Loading the list of database users that use the 'gssapi' plugin allows us to
 * give more precise error messages to the clients when authentication fails.
 *
 * @param listener Listener definition
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR on error
 */
int gssapi_auth_load_users(SERV_LISTENER *listener)
{
    char *user, *pw;
    int rval = MXS_AUTH_LOADUSERS_ERROR;

    if (serviceGetUser(listener->service, &user, &pw) && (pw = decryptPassword(pw)))
    {
        for (SERVER_REF *servers = listener->service->dbref; servers; servers = servers->next)
        {
            MYSQL *mysql = mysql_init(NULL);

            if (mxs_mysql_real_connect(mysql, servers->server, user, pw))
            {
                if (mysql_query(mysql, gssapi_users_query))
                {
                    MXS_ERROR("Failed to query server '%s' for GSSAPI users.",
                              servers->server->unique_name);
                }
                else
                {
                    MYSQL_RES *res = mysql_store_result(mysql);

                    if (res)
                    {
                        ss_dassert(mysql_num_fields(res) == GSSAPI_USERS_QUERY_NUM_FIELDS);
                        MYSQL_ROW row;

                        while ((row = mysql_fetch_row(res)))
                        {
                            /** TODO: Store this information in the users table of the listener */
                            MXS_INFO("Would add: '%s'@'%s' for '%s'", row[0], row[1], row[2]);
                        }

                        rval = MXS_AUTH_LOADUSERS_OK;
                        mysql_free_result(res);
                    }
                }

                mysql_close(mysql);
            }
        }

        MXS_FREE(pw);
    }

    return rval;
}

/**
 * Implementation of the authenticator module interface
 */
static GWAUTHENTICATOR MyObject =
{
    gssapi_auth_init,                /* Initialize authenticator */
    gssapi_auth_alloc,               /* Allocate authenticator data */
    gssapi_auth_extract,             /* Extract data into structure   */
    gssapi_auth_connectssl,          /* Check if client supports SSL  */
    gssapi_auth_authenticate,        /* Authenticate user credentials */
    gssapi_auth_free_data,           /* Free the client data held in DCB */
    gssapi_auth_free,                /* Free authenticator data */
    gssapi_auth_load_users           /* Load database users */
};

MODULE_INFO info =
{
    MODULE_API_AUTHENTICATOR,
    MODULE_GA,
    GWAUTHENTICATOR_VERSION,
    "GSSAPI authenticator"
};

static char *version_str = "V1.0.0";

/**
 * Version string entry point
 */
char* version()
{
    return version_str;
}

/**
 * Module initialization entry point
 */
void ModuleInit()
{
}

/**
 * Module handle entry point
 */
GWAUTHENTICATOR* GetModuleObject()
{
    return &MyObject;
}
