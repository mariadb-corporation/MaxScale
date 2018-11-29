/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file cdc_auth.c
 *
 * CDC Authentication module for handling the checking of clients credentials
 * in the CDC protocol.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 11/03/2016   Massimiliano Pinto      Initial version
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "CDCPlainAuth"

#include <maxscale/authenticator.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cdc.hh>
#include <maxscale/alloc.h>
#include <maxscale/event.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/modutil.hh>
#include <maxscale/paths.h>
#include <maxscale/secrets.h>
#include <maxscale/users.h>
#include <maxscale/utils.h>

/* Allowed time interval (in seconds) after last update*/
#define CDC_USERS_REFRESH_TIME 30
/* Max number of load calls within the time interval */
#define CDC_USERS_REFRESH_MAX_PER_TIME 4

const char CDC_USERS_FILENAME[] = "cdcusers";

static bool cdc_auth_set_protocol_data(DCB* dcb, GWBUF* buf);
static bool cdc_auth_is_client_ssl_capable(DCB* dcb);
static int  cdc_auth_authenticate(DCB* dcb);
static void cdc_auth_free_client_data(DCB* dcb);

static int cdc_set_service_user(Listener* listener);
static int cdc_replace_users(Listener* listener);

static int cdc_auth_check(DCB* dcb,
                          CDC_protocol* protocol,
                          char* username,
                          uint8_t* auth_data,
                          unsigned int* flags
                          );

static bool cdc_auth_set_client_data(CDC_session* client_data,
                                     CDC_protocol* protocol,
                                     uint8_t* client_auth_packet,
                                     int client_auth_packet_size
                                     );

/**
 * @brief Add a new CDC user
 *
 * This function should not be called directly. The module command system will
 * call it when necessary.
 *
 * @param args Arguments for this command
 * @return True if user was successfully added
 */
static bool cdc_add_new_user(const MODULECMD_ARG* args, json_t** output)
{
    const char* user = args->argv[1].value.string;
    size_t userlen = strlen(user);
    const char* password = args->argv[2].value.string;
    uint8_t phase1[SHA_DIGEST_LENGTH];
    uint8_t phase2[SHA_DIGEST_LENGTH];
    SHA1((uint8_t*)password, strlen(password), phase1);
    SHA1(phase1, sizeof(phase1), phase2);

    size_t data_size = userlen + 2 + SHA_DIGEST_LENGTH * 2;     // Extra for the : and newline
    char final_data[data_size];
    strcpy(final_data, user);
    strcat(final_data, ":");
    gw_bin2hex(final_data + userlen + 1, phase2, sizeof(phase2));
    final_data[data_size - 1] = '\n';

    SERVICE* service = args->argv[0].value.service;
    char path[PATH_MAX + 1];
    snprintf(path, PATH_MAX, "%s/%s/", get_datadir(), service->name);
    bool rval = false;

    if (mxs_mkdir_all(path, 0777))
    {
        strcat(path, CDC_USERS_FILENAME);
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0660);

        if (fd != -1)
        {
            if (write(fd, final_data, sizeof(final_data)) == static_cast<int>(sizeof(final_data)))
            {
                MXS_NOTICE("Added user '%s' to service '%s'", user, service->name);
                rval = true;
            }
            else
            {
                const char* real_err = mxs_strerror(errno);
                MXS_NOTICE("Failed to write to file '%s': %s", path, real_err);
                modulecmd_set_error("Failed to write to file '%s': %s", path, real_err);
            }

            close(fd);
        }
        else
        {
            const char* real_err = mxs_strerror(errno);
            MXS_NOTICE("Failed to open file '%s': %s", path, real_err);
            modulecmd_set_error("Failed to open file '%s': %s", path, real_err);
        }
    }
    else
    {
        modulecmd_set_error("Failed to create directory '%s'. Read the MaxScale "
                            "log for more details.",
                            path);
    }

    return rval;
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static modulecmd_arg_type_t args[] =
        {
            {MODULECMD_ARG_SERVICE, "Service where the user is added"},
            {MODULECMD_ARG_STRING,  "User to add"                    },
            {MODULECMD_ARG_STRING,  "Password of the user"           }
        };

        modulecmd_register_command("cdc",
                                   "add_user",
                                   MODULECMD_TYPE_ACTIVE,
                                   cdc_add_new_user,
                                   3,
                                   args,
                                   "Add a new CDC user");

        static MXS_AUTHENTICATOR MyObject =
        {
            NULL,                           /* No initialize entry point */
            NULL,                           /* No create entry point */
            cdc_auth_set_protocol_data,     /* Extract data into structure   */
            cdc_auth_is_client_ssl_capable, /* Check if client supports SSL  */
            cdc_auth_authenticate,          /* Authenticate user credentials */
            cdc_auth_free_client_data,      /* Free the client data held in DCB */
            NULL,                           /* No destroy entry point */
            cdc_replace_users,              /* Load CDC users */
            users_default_diagnostic,       /* Default diagnostic */
            users_default_diagnostic_json,  /* Default diagnostic */
            NULL                            /* No user reauthentication */
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_AUTHENTICATOR,
            MXS_MODULE_GA,
            MXS_AUTHENTICATOR_VERSION,
            "The CDC client to MaxScale authenticator implementation",
            "V1.1.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/**
 * @brief Function to easily call authentication check.
 *
 * @param dcb Request handler DCB connected to the client
 * @param protocol  The protocol structure for the connection
 * @param username  String containing username
 * @param auth_data  The encrypted password for authentication
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int cdc_auth_check(DCB* dcb,
                          CDC_protocol* protocol,
                          char* username,
                          uint8_t* auth_data,
                          unsigned int* flags)
{
    int rval = CDC_STATE_AUTH_FAILED;

    if (dcb->listener->users)
    {
        /* compute SHA1 of auth_data */
        uint8_t sha1_step1[SHA_DIGEST_LENGTH] = "";
        char hex_step1[2 * SHA_DIGEST_LENGTH + 1] = "";

        gw_sha1_str(auth_data, SHA_DIGEST_LENGTH, sha1_step1);
        gw_bin2hex(hex_step1, sha1_step1, SHA_DIGEST_LENGTH);

        if (users_auth(dcb->listener->users, username, hex_step1))
        {
            rval = CDC_STATE_AUTH_OK;
        }
    }

    return rval;
}

/**
 * @brief Authenticates a CDC user who is a client to MaxScale.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status
 * @note Authentication status codes are defined in cdc.h
 */
static int cdc_auth_authenticate(DCB* dcb)
{
    CDC_protocol* protocol = DCB_PROTOCOL(dcb, CDC_protocol);
    CDC_session* client_data = (CDC_session*)dcb->data;
    int auth_ret;

    if (0 == strlen(client_data->user))
    {
        auth_ret = CDC_STATE_AUTH_ERR;
    }
    else
    {
        MXS_DEBUG("Receiving connection from '%s'",
                  client_data->user);

        auth_ret =
            cdc_auth_check(dcb, protocol, client_data->user, client_data->auth_data, client_data->flags);

        /* On failed authentication try to reload users and authenticate again */
        if (CDC_STATE_AUTH_OK != auth_ret && cdc_replace_users(dcb->listener) == MXS_AUTH_LOADUSERS_OK)
        {
            auth_ret = cdc_auth_check(dcb,
                                      protocol,
                                      client_data->user,
                                      client_data->auth_data,
                                      client_data->flags);
        }

        /* on successful authentication, set user into dcb field */
        if (CDC_STATE_AUTH_OK == auth_ret)
        {
            dcb->user = MXS_STRDUP_A(client_data->user);
        }
        else if (dcb->service->log_auth_warnings)
        {
            MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                          "%s: login attempt for user '%s', authentication failed.",
                          dcb->service->name,
                          client_data->user);
        }
    }

    return auth_ret;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * The request handler DCB has a field called data that contains protocol
 * specific information. This function examines a buffer containing CDC
 * authentication data and puts it into a structure that is referred to
 * by the DCB. If the information in the buffer is invalid, then a failure
 * code is returned. A call to cdc_auth_set_client_data does the
 * detailed work.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return True on success, false on error
 */
static bool cdc_auth_set_protocol_data(DCB* dcb, GWBUF* buf)
{
    uint8_t* client_auth_packet = GWBUF_DATA(buf);
    CDC_protocol* protocol = NULL;
    CDC_session* client_data = NULL;
    int client_auth_packet_size = 0;

    protocol = DCB_PROTOCOL(dcb, CDC_protocol);
    if (dcb->data == NULL)
    {
        if (NULL == (client_data = (CDC_session*)MXS_CALLOC(1, sizeof(CDC_session))))
        {
            return false;
        }
        dcb->data = client_data;
    }
    else
    {
        client_data = (CDC_session*)dcb->data;
    }

    client_auth_packet_size = gwbuf_length(buf);

    return cdc_auth_set_client_data(client_data,
                                    protocol,
                                    client_auth_packet,
                                    client_auth_packet_size);
}

/**
 * @brief Transfer detailed data from the authentication request to the DCB.
 *
 * The caller has created the data structure pointed to by the DCB, and this
 * function fills in the details. If problems are found with the data, the
 * return code indicates failure.
 *
 * @param client_data The data structure for the DCB
 * @param protocol The protocol structure for this connection
 * @param client_auth_packet The data from the buffer received from client
 * @param client_auth_packet size An integer giving the size of the data
 * @return True on success, false on error
 */
static bool cdc_auth_set_client_data(CDC_session* client_data,
                                     CDC_protocol* protocol,
                                     uint8_t* client_auth_packet,
                                     int client_auth_packet_size)
{
    if (client_auth_packet_size % 2 != 0)
    {
        /** gw_hex2bin expects an even number of bytes */
        client_auth_packet_size--;
    }

    bool rval = false;
    int decoded_size = client_auth_packet_size / 2;
    char decoded_buffer[decoded_size + 1];      // Extra for terminating null

    /* decode input data */
    if (client_auth_packet_size <= CDC_USER_MAXLEN)
    {
        gw_hex2bin((uint8_t*)decoded_buffer,
                   (const char*)client_auth_packet,
                   client_auth_packet_size);
        decoded_buffer[decoded_size] = '\0';
        char* tmp_ptr = strchr(decoded_buffer, ':');

        if (tmp_ptr)
        {
            size_t user_len = tmp_ptr - decoded_buffer;
            *tmp_ptr++ = '\0';
            size_t auth_len = decoded_size - (tmp_ptr - decoded_buffer);

            if (user_len <= CDC_USER_MAXLEN && auth_len == SHA_DIGEST_LENGTH)
            {
                strcpy(client_data->user, decoded_buffer);
                memcpy(client_data->auth_data, tmp_ptr, auth_len);
                rval = true;
            }
        }
        else
        {
            MXS_ERROR("Authentication failed, the decoded client authentication "
                      "packet is malformed. Expected <username>:SHA1(<password>)");
        }
    }
    else
    {
        MXS_ERROR("Authentication failed, client authentication packet length "
                  "exceeds the maximum allowed length of %d bytes.",
                  CDC_USER_MAXLEN);
    }

    return rval;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * The authentication request from the client will indicate whether the client
 * is expecting to make an SSL connection. The information has been extracted
 * in the previous functions.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable
 */
static bool cdc_auth_is_client_ssl_capable(DCB* dcb)
{
    return false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * Currently all that is required is to free the storage pointed to by
 * dcb->data.  But this is intended to be implemented as part of the
 * authentication API at which time this code will be moved into the
 * CDC authenticator.  If the data structure were to become more complex
 * the mechanism would still work and be the responsibility of the authenticator.
 * The DCB should not know authenticator implementation details.
 *
 * @param dcb Request handler DCB connected to the client
 */
static void cdc_auth_free_client_data(DCB* dcb)
{
    MXS_FREE(dcb->data);
}

/*
 * Add the service user to CDC dbusers (service->users)
 * via cdc_users_alloc
 *
 * @param service   The current service
 * @return      0 on success, 1 on failure
 */
static int cdc_set_service_user(Listener* listener)
{
    SERVICE* service = listener->service;
    char* dpwd = NULL;
    char* newpasswd = NULL;
    const char* service_user = NULL;
    const char* service_passwd = NULL;

    serviceGetUser(service, &service_user, &service_passwd);
    dpwd = decrypt_password(service_passwd);

    if (!dpwd)
    {
        MXS_ERROR("decrypt password failed for service user %s, service %s",
                  service_user,
                  service->name);

        return 1;
    }

    newpasswd = create_hex_sha1_sha1_passwd(dpwd);

    if (!newpasswd)
    {
        MXS_ERROR("create hex_sha1_sha1_password failed for service user %s",
                  service_user);

        MXS_FREE(dpwd);
        return 1;
    }

    /* add service user */
    const char* user;
    const char* password;
    serviceGetUser(service, &user, &password);
    users_add(listener->users, user, newpasswd, USER_ACCOUNT_ADMIN);

    MXS_FREE(newpasswd);
    MXS_FREE(dpwd);

    return 0;
}

/**
 * Load the AVRO users
 *
 * @param service    Current service
 * @param usersfile  File with users
 * @return -1 on error or users loaded (including 0)
 */

static int cdc_read_users(USERS* users, char* usersfile)
{
    FILE* fp;
    int loaded = 0;
    char* avro_user;
    char* user_passwd;
    /* user maxlen ':' password hash  '\n' '\0' */
    char read_buffer[CDC_USER_MAXLEN + 1 + SHA_DIGEST_LENGTH + 1 + 1];

    int max_line_size = sizeof(read_buffer) - 1;

    if ((fp = fopen(usersfile, "r")) == NULL)
    {
        return -1;
    }

    while (!feof(fp))
    {
        if (fgets(read_buffer, max_line_size, fp) != NULL)
        {
            char* tmp_ptr = read_buffer;

            if ((tmp_ptr = strchr(read_buffer, ':')) != NULL)
            {
                *tmp_ptr++ = '\0';
                avro_user = read_buffer;
                user_passwd = tmp_ptr;
                if ((tmp_ptr = strchr(user_passwd, '\n')) != NULL)
                {
                    *tmp_ptr = '\0';
                }

                /* add user */
                users_add(users, avro_user, user_passwd, USER_ACCOUNT_ADMIN);

                loaded++;
            }
        }
    }

    fclose(fp);

    return loaded;
}

/**
 * @brief Replace the user/passwd in the servicei users tbale from a db file
 *
 * @param service The current service
 */
int cdc_replace_users(Listener* listener)
{
    int rc = MXS_AUTH_LOADUSERS_ERROR;
    USERS* newusers = users_alloc();

    if (newusers)
    {
        char path[PATH_MAX + 1];
        snprintf(path,
                 PATH_MAX,
                 "%s/%s/%s",
                 get_datadir(),
                 listener->service->name,
                 CDC_USERS_FILENAME);

        int i = cdc_read_users(newusers, path);
        USERS* oldusers = NULL;

        if (i > 0)
        {
            /** Successfully loaded at least one user */
            oldusers = listener->users;
            listener->users = newusers;
            rc = MXS_AUTH_LOADUSERS_OK;
        }
        else if (listener->users)
        {
            /** Failed to load users, use the old users table */
            users_free(newusers);
        }
        else
        {
            /** No existing users, use the new empty users table */
            listener->users = newusers;
        }

        cdc_set_service_user(listener);

        if (oldusers)
        {
            users_free(oldusers);
        }
    }
    return rc;
}
