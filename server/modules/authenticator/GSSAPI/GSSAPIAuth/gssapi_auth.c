/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "GSSAPIAuth"

#include <maxscale/cdefs.h>

#include <maxscale/alloc.h>
#include <maxscale/authenticator.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/secrets.h>
#include <maxscale/service.h>
#include <maxscale/sqlite3.h>
#include <maxscale/users.h>

#include "../gssapi_auth.h"

/**
 * MySQL queries for retrieving the list of users
 */

/** Query that gets all users that authenticate via the gssapi plugin */
const char *gssapi_users_query =
    "SELECT u.user, u.host, d.db, u.select_priv, u.authentication_string FROM "
    "mysql.user AS u LEFT JOIN mysql.db AS d "
    "ON (u.user = d.user AND u.host = d.host) WHERE u.plugin = 'gssapi' "
    "UNION "
    "SELECT u.user, u.host, t.db, u.select_priv, u.authentication_string FROM "
    "mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
    "ON (u.user = t.user AND u.host = t.host) WHERE u.plugin = 'gssapi' "
    "ORDER BY user";

#define GSSAPI_USERS_QUERY_NUM_FIELDS 5

/**
 * SQLite queries for authenticating users
 */

/** Name of the in-memory database */
#define GSSAPI_DATABASE_NAME "file:gssapi.db?mode=memory&cache=shared"

/** The table name where we store the users */
#define GSSAPI_TABLE_NAME    "gssapi_users"

/** CREATE TABLE statement for the in-memory table */
const char create_sql[] =
    "CREATE TABLE IF NOT EXISTS " GSSAPI_TABLE_NAME
    "(user varchar(255), host varchar(255), db varchar(255), anydb boolean, princ text)";

/** The query that is executed when a user is authenticated */
static const char gssapi_auth_query[] =
    "SELECT * FROM " GSSAPI_TABLE_NAME
    " WHERE user = '%s' AND '%s' LIKE host AND (anydb = '1' OR '%s' = '' OR '%s' LIKE db)"
    " AND ('%s' = '%s' OR princ = '%s') LIMIT 1";

/** Delete query used to clean up the database before loading new users */
static const char delete_query[] = "DELETE FROM " GSSAPI_TABLE_NAME;

/**
 * The insert query template which adds users to the gssapi_users table.
 *
 * Note that the last two values are strings that can be NULL and thus they have
 * no quoted around them. The quotes for strings are added in add_gssapi_user().
 */
static const char insert_sql_pattern[] =
    "INSERT INTO " GSSAPI_TABLE_NAME " VALUES ('%s', '%s', %s, %s, %s)";

/** Used for NULL value creation in the INSERT query */
static const char null_token[] = "NULL";

/** Flags for sqlite3_open_v2() */
static int db_flags = SQLITE_OPEN_READWRITE |
                      SQLITE_OPEN_CREATE |
                      SQLITE_OPEN_URI |
                      SQLITE_OPEN_SHAREDCACHE;

/** The instance structure for the client side GSSAPI authenticator, created in
 *  gssapi_auth_init() */
typedef struct gssapi_instance
{
    char    *principal_name; /**< Service principal name given to the client */
    sqlite3 *handle;         /**< SQLite3 database handle */
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

        if (sqlite3_open_v2(GSSAPI_DATABASE_NAME, &instance->handle, db_flags, NULL) != SQLITE_OK)
        {
            MXS_ERROR("Failed to open SQLite3 handle.");
            MXS_FREE(instance);
            return NULL;
        }

        char *err;

        if (sqlite3_exec(instance->handle, create_sql, NULL, NULL, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to create database: %s", err);
            sqlite3_free(err);
            sqlite3_close_v2(instance->handle);
            MXS_FREE(instance);
            return NULL;
        }

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

void* gssapi_auth_alloc(void *instance)
{
    gssapi_auth_t* rval = MXS_MALLOC(sizeof(gssapi_auth_t));

    if (rval)
    {
        rval->state = GSSAPI_AUTH_INIT;
        rval->principal_name = NULL;
        rval->principal_name_len = 0;
        rval->sequence = 0;

        if (sqlite3_open_v2(GSSAPI_DATABASE_NAME, &rval->handle, db_flags, NULL) == SQLITE_OK)
        {
            sqlite3_busy_timeout(rval->handle, MXS_SQLITE_BUSY_TIMEOUT);
        }
        else
        {
            MXS_ERROR("Failed to open SQLite3 handle.");
            MXS_FREE(rval);
            rval = NULL;
        }
    }

    return rval;
}

void gssapi_auth_free(void *data)
{
    if (data)
    {
        gssapi_auth_t *auth = (gssapi_auth_t*)data;
        sqlite3_close_v2(auth->handle);
        MXS_FREE(auth->principal_name);
        MXS_FREE(auth);
    }
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
 * @return True if authentication can continue, false if not
 */
static bool gssapi_auth_extract(DCB *dcb, GWBUF *read_buffer)
{
    int rval = false;
    gssapi_auth_t *auth = (gssapi_auth_t*)dcb->authenticator_data;

    switch (auth->state)
    {
    case GSSAPI_AUTH_INIT:
        copy_client_information(dcb, read_buffer);
        rval = true;
        break;

    case GSSAPI_AUTH_DATA_SENT:
        store_client_token(dcb, read_buffer);
        rval = true;
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
 * @param output Pointer where the client principal name is stored
 * @return True if client token is valid
 */
static bool validate_gssapi_token(char* principal, uint8_t* token, size_t len, char **output)
{
    OM_uint32 major = 0, minor = 0;
    gss_buffer_desc server_buf = {0, 0};
    gss_cred_id_t credentials;

    server_buf.value = (void*)principal;
    server_buf.length = strlen(principal) + 1;

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
        gss_buffer_desc client_name = {0, 0};
        gss_OID_desc *oid;
        gss_name_t client;

        in.value = token;
        in.length = len;

        major = gss_accept_sec_context(&minor, &handle, GSS_C_NO_CREDENTIAL,
                                       &in, GSS_C_NO_CHANNEL_BINDINGS,
                                       &client, &oid, &out,
                                       0, 0, NULL);
        if (GSS_ERROR(major))
        {
            report_error(major, minor);
            return false;
        }

        major = gss_display_name(&minor, client, &client_name, NULL);

        if (GSS_ERROR(major))
        {
            report_error(major, minor);
            return false;
        }

        char *princ_name = MXS_MALLOC(client_name.length + 1);

        if (!princ_name)
        {
            return false;
        }

        memcpy(princ_name, (const char*)client_name.value, client_name.length);
        princ_name[client_name.length] = '\0';
        *output = princ_name;
    }
    while (major & GSS_S_CONTINUE_NEEDED);

    return true;
}

/** @brief Callback for sqlite3_exec() */
static int auth_cb(void *data, int columns, char** rows, char** row_names)
{
    bool *rv = (bool*)data;
    *rv = true;
    return 0;
}

/**
 * @brief Verify the user has access to the database
 *
 * @param auth Authenticator session
 * @param dcb Client DCB
 * @param session MySQL session
 * @param princ Client principal name
 * @return True if the user has access to the database
 */
static bool validate_user(gssapi_auth_t *auth, DCB *dcb, MYSQL_session *session, const char *princ)
{
    ss_dassert(princ);
    size_t len = sizeof(gssapi_auth_query) + strlen(session->user) * 2 +
                 strlen(session->db) * 2 + strlen(dcb->remote) + strlen(princ) * 2;
    char sql[len + 1];
    bool rval = false;
    char *err;

    char princ_user[strlen(princ) + 1];
    strcpy(princ_user, princ);
    char *at = strchr(princ_user, '@');
    if (at)
    {
        *at = '\0';
    }

    sprintf(sql, gssapi_auth_query, session->user, dcb->remote, session->db,
            session->db, princ_user, session->user, princ);

    /**
     * Try authentication twice; first time with the current users, second
     * time with fresh users
     */
    for (int i = 0; i < 2 && !rval; i++)
    {
        if (sqlite3_exec(auth->handle, sql, auth_cb, &rval, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute auth query: %s", err);
            sqlite3_free(err);
            rval = false;
        }

        if (!rval)
        {
            service_refresh_users(dcb->service);
        }
    }

    return rval;
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
        char *princ = NULL;

        if (validate_gssapi_token(instance->principal_name, ses->auth_token, ses->auth_token_len, &princ) &&
            validate_user(auth, dcb, ses, princ))
        {
            rval = MXS_AUTH_SUCCEEDED;
        }

        MXS_FREE(princ);
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
 * @brief Delete old users from the database
 * @param handle Database handle
 */
static void delete_old_users(sqlite3 *handle)
{
    char *err;

    if (sqlite3_exec(handle, delete_query, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to delete old users: %s", err);
        sqlite3_free(err);
    }
}

/**
 * @brief Add new GSSAPI user to the internal user database
 *
 * @param handle Database handle
 * @param user   Username
 * @param host   Host
 * @param db     Database
 * @param anydb  Global access to databases
 */
static void add_gssapi_user(sqlite3 *handle, const char *user, const char *host,
                            const char *db, bool anydb, const char *princ)
{
    size_t dblen = db ? strlen(db) + 2 : sizeof(null_token); /** +2 for single quotes */
    char dbstr[dblen + 1];

    if (db)
    {
        sprintf(dbstr, "'%s'", db);
    }
    else
    {
        strcpy(dbstr, null_token);
    }

    size_t princlen = princ && *princ ? strlen(princ) + 2 : sizeof(null_token); /** +2 for single quotes */
    char princstr[princlen + 1];

    if (princ && *princ)
    {
        sprintf(princstr, "'%s'", princ);
    }
    else
    {
        strcpy(princstr, null_token);
    }

    size_t len = sizeof(insert_sql_pattern) + strlen(user) + strlen(host) + dblen + princlen + 1;

    char insert_sql[len + 1];
    sprintf(insert_sql, insert_sql_pattern, user, host, dbstr, anydb ? "1" : "0", princstr);

    char *err;
    if (sqlite3_exec(handle, insert_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to insert user: %s", err);
        sqlite3_free(err);
    }

    MXS_INFO("Added user: %s", insert_sql);
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
    GSSAPI_INSTANCE *inst = (GSSAPI_INSTANCE*)listener->auth_instance;

    if (serviceGetUser(listener->service, &user, &pw) && (pw = decrypt_password(pw)))
    {
        for (SERVER_REF *servers = listener->service->dbref; servers; servers = servers->next)
        {
            MYSQL *mysql = mysql_init(NULL);

            if (mxs_mysql_real_connect(mysql, servers->server, user, pw))
            {
                if (mysql_query(mysql, gssapi_users_query))
                {
                    MXS_ERROR("Failed to query server '%s' for GSSAPI users: %s",
                              servers->server->unique_name, mysql_error(mysql));
                }
                else
                {
                    MYSQL_RES *res = mysql_store_result(mysql);

                    delete_old_users(inst->handle);

                    if (res)
                    {
                        ss_dassert(mysql_num_fields(res) == GSSAPI_USERS_QUERY_NUM_FIELDS);
                        MYSQL_ROW row;

                        while ((row = mysql_fetch_row(res)))
                        {
                            add_gssapi_user(inst->handle, row[0], row[1], row[2],
                                            row[3] && strcasecmp(row[3], "Y") == 0,
                                            row[4]);
                        }

                        rval = MXS_AUTH_LOADUSERS_OK;
                        mysql_free_result(res);
                    }
                }

                mysql_close(mysql);

                if (rval == MXS_AUTH_LOADUSERS_OK)
                {
                    break;
                }
            }
        }

        MXS_FREE(pw);
    }

    return rval;
}

/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_AUTHENTICATOR MyObject =
    {
        gssapi_auth_init,                /* Initialize authenticator */
        gssapi_auth_alloc,               /* Allocate authenticator data */
        gssapi_auth_extract,             /* Extract data into structure   */
        gssapi_auth_connectssl,          /* Check if client supports SSL  */
        gssapi_auth_authenticate,        /* Authenticate user credentials */
        gssapi_auth_free_data,           /* Free the client data held in DCB */
        gssapi_auth_free,                /* Free authenticator data */
        gssapi_auth_load_users,          /* Load database users */
        users_default_diagnostic,        /* Default user diagnostic */
        users_default_diagnostic_json,   /* Default user diagnostic */
        NULL                             /* No user reauthentication */
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "GSSAPI authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        { { MXS_END_MODULE_PARAMS} }
    };

    return &info;
}
