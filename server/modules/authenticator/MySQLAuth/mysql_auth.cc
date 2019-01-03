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
 * @file mysql_auth.c
 *
 * MySQL Authentication module for handling the checking of clients credentials
 * in the MySQL protocol.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 02/02/2016   Martin Brampton         Initial version
 *
 * @endverbatim
 */

#include "mysql_auth.h"

#include <maxscale/protocol/mysql.h>
#include <maxscale/authenticator.h>
#include <maxscale/alloc.h>
#include <maxscale/event.hh>
#include <maxscale/poll.h>
#include <maxscale/paths.h>
#include <maxscale/secrets.h>
#include <maxscale/utils.h>
#include <maxscale/routingworker.h>

static void* mysql_auth_init(char** options);
static bool  mysql_auth_set_protocol_data(DCB* dcb, GWBUF* buf);
static bool  mysql_auth_is_client_ssl_capable(DCB* dcb);
static int   mysql_auth_authenticate(DCB* dcb);
static void  mysql_auth_free_client_data(DCB* dcb);
static int   mysql_auth_load_users(SERV_LISTENER* port);
static void* mysql_auth_create(void* instance);
static void  mysql_auth_destroy(void* data);

static int combined_auth_check(DCB* dcb,
                               uint8_t* auth_token,
                               size_t   auth_token_len,
                               MySQLProtocol* protocol,
                               char* username,
                               uint8_t* stage1_hash,
                               char* database
                               );
static bool mysql_auth_set_client_data(MYSQL_session* client_data,
                                       MySQLProtocol* protocol,
                                       GWBUF* buffer);

void    mysql_auth_diagnostic(DCB* dcb, SERV_LISTENER* port);
json_t* mysql_auth_diagnostic_json(const SERV_LISTENER* port);

int mysql_auth_reauthenticate(DCB* dcb,
                              const char* user,
                              uint8_t* token,
                              size_t   token_len,
                              uint8_t* scramble,
                              size_t   scramble_len,
                              uint8_t* output_token,
                              size_t   output_token_len);

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
        static MXS_AUTHENTICATOR MyObject =
        {
            mysql_auth_init,                    /* Initialize the authenticator */
            NULL,                               /* Create entry point */
            mysql_auth_set_protocol_data,       /* Extract data into structure   */
            mysql_auth_is_client_ssl_capable,   /* Check if client supports SSL  */
            mysql_auth_authenticate,            /* Authenticate user credentials */
            mysql_auth_free_client_data,        /* Free the client data held in DCB */
            NULL,                               /* Destroy entry point */
            mysql_auth_load_users,              /* Load users from backend databases */
            mysql_auth_diagnostic,
            mysql_auth_diagnostic_json,
            mysql_auth_reauthenticate       /* Handle COM_CHANGE_USER */
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_AUTHENTICATOR,
            MXS_MODULE_GA,
            MXS_AUTHENTICATOR_VERSION,
            "The MySQL client to MaxScale authenticator implementation",
            "V1.1.0",
            ACAP_TYPE_ASYNC,
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

static bool open_instance_database(const char* path, sqlite3** handle)
{
    int rc = sqlite3_open_v2(path, handle, db_flags, NULL);

    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite3 handle: %d", rc);
        return false;
    }

    char* err;

    if (sqlite3_exec(*handle, users_create_sql, NULL, NULL, &err) != SQLITE_OK
        || sqlite3_exec(*handle, databases_create_sql, NULL, NULL, &err) != SQLITE_OK
        || sqlite3_exec(*handle, pragma_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to create database: %s", err);
        sqlite3_free(err);
        sqlite3_close_v2(*handle);
        return false;
    }

    return true;
}

sqlite3* get_handle(MYSQL_AUTH* instance)
{
    int i = mxs_rworker_get_current_id();
    mxb_assert(i >= 0);

    if (instance->handles[i] == NULL)
    {
        MXB_AT_DEBUG(bool rval = ) open_instance_database(":memory:", &instance->handles[i]);
        mxb_assert(rval);
    }

    return instance->handles[i];
}

/**
 * @brief Check if service permissions should be checked
 *
 * @param instance Authenticator instance
 *
 * @return True if permissions should be checked
 */
static bool should_check_permissions(MYSQL_AUTH* instance)
{
    // Only check permissions when the users are loaded for the first time.
    return instance->check_permissions;
}

/**
 * @brief Initialize the authenticator instance
 *
 * @param options Authenticator options
 * @return New MYSQL_AUTH instance or NULL on error
 */
static void* mysql_auth_init(char** options)
{
    MYSQL_AUTH* instance = static_cast<MYSQL_AUTH*>(MXS_MALLOC(sizeof(*instance)));

    if (instance
        && (instance->handles = static_cast<sqlite3**>(MXS_CALLOC(config_threadcount(), sizeof(sqlite3*)))))
    {
        bool error = false;
        instance->cache_dir = NULL;
        instance->inject_service_user = true;
        instance->skip_auth = false;
        instance->check_permissions = true;
        instance->lower_case_table_names = false;

        for (int i = 0; options[i]; i++)
        {
            char* value = strchr(options[i], '=');

            if (value)
            {
                *value++ = '\0';

                if (strcmp(options[i], "cache_dir") == 0)
                {
                    if ((instance->cache_dir = MXS_STRDUP(value)) == NULL
                        || !clean_up_pathname(instance->cache_dir))
                    {
                        error = true;
                    }
                }
                else if (strcmp(options[i], "inject_service_user") == 0)
                {
                    instance->inject_service_user = config_truth_value(value);
                }
                else if (strcmp(options[i], "skip_authentication") == 0)
                {
                    instance->skip_auth = config_truth_value(value);
                }
                else if (strcmp(options[i], "lower_case_table_names") == 0)
                {
                    instance->lower_case_table_names = config_truth_value(value);
                }
                else
                {
                    MXS_ERROR("Unknown authenticator option: %s", options[i]);
                    error = true;
                }
            }
            else
            {
                MXS_ERROR("Unknown authenticator option: %s", options[i]);
                error = true;
            }
        }

        if (error)
        {
            MXS_FREE(instance->cache_dir);
            MXS_FREE(instance->handles);
            MXS_FREE(instance);
            instance = NULL;
        }
    }
    else if (instance)
    {
        MXS_FREE(instance);
        instance = NULL;
    }

    return instance;
}

static bool is_localhost_address(struct sockaddr_storage* addr)
{
    bool rval = false;

    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in* ip = (struct sockaddr_in*)addr;
        if (ip->sin_addr.s_addr == INADDR_LOOPBACK)
        {
            rval = true;
        }
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6* ip = (struct sockaddr_in6*)addr;
        if (memcmp(&ip->sin6_addr, &in6addr_loopback, sizeof(ip->sin6_addr)) == 0)
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Authenticates a MySQL user who is a client to MaxScale.
 *
 * First call the SSL authentication function. Call other functions to validate
 * the user, reloading the user data if the first attempt fails.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
static int mysql_auth_authenticate(DCB* dcb)
{
    int auth_ret = MXS_AUTH_SSL_COMPLETE;
    MYSQL_session* client_data = (MYSQL_session*)dcb->data;
    if (*client_data->user)
    {
        MXS_DEBUG("Receiving connection from '%s' to database '%s'.",
                  client_data->user,
                  client_data->db);

        MYSQL_AUTH* instance = (MYSQL_AUTH*)dcb->listener->auth_instance;
        MySQLProtocol* protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
        auth_ret = validate_mysql_user(instance,
                                       dcb,
                                       client_data,
                                       protocol->scramble,
                                       sizeof(protocol->scramble));

        if (auth_ret != MXS_AUTH_SUCCEEDED
            && service_refresh_users(dcb->service) == 0)
        {
            auth_ret = validate_mysql_user(instance,
                                           dcb,
                                           client_data,
                                           protocol->scramble,
                                           sizeof(protocol->scramble));
        }

        /* on successful authentication, set user into dcb field */
        if (auth_ret == MXS_AUTH_SUCCEEDED)
        {
            auth_ret = MXS_AUTH_SUCCEEDED;
            dcb->user = MXS_STRDUP_A(client_data->user);
            /** Send an OK packet to the client */
        }
        else if (dcb->service->log_auth_warnings)
        {
            // The default failure is a `User not found` one
            char extra[256] = "User not found.";

            if (auth_ret == MXS_AUTH_FAILED_DB)
            {
                snprintf(extra, sizeof(extra), "Unknown database: %s", client_data->db);
            }
            else if (auth_ret == MXS_AUTH_FAILED_WRONG_PASSWORD)
            {
                strcpy(extra, "Wrong password.");
            }

            if (dcb->path)
            {
                MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                              "%s: login attempt for user '%s'@[%s]:%s, authentication failed. %s",
                              dcb->service->name,
                              client_data->user,
                              dcb->remote,
                              dcb->path,
                              extra);
            }
            else
            {
                MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                              "%s: login attempt for user '%s'@[%s]:%d, authentication failed. %s",
                              dcb->service->name,
                              client_data->user,
                              dcb->remote,
                              dcb_get_port(dcb),
                              extra);
            }

            if (is_localhost_address(&dcb->ip)
                && !dcb->service->localhost_match_wildcard_host)
            {
                MXS_NOTICE("If you have a wildcard grant that covers this address, "
                           "try adding 'localhost_match_wildcard_host=true' for "
                           "service '%s'. ",
                           dcb->service->name);
            }
        }

        /* let's free the auth_token now */
        if (client_data->auth_token)
        {
            MXS_FREE(client_data->auth_token);
            client_data->auth_token = NULL;
        }
    }

    return auth_ret;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * The request handler DCB has a field called data that contains protocol
 * specific information. This function examines a buffer containing MySQL
 * authentication data and puts it into a structure that is referred to
 * by the DCB. If the information in the buffer is invalid, then a failure
 * code is returned. A call to mysql_auth_set_client_data does the
 * detailed work.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffer containing data from client
 * @return True on success, false on error
 */
static bool mysql_auth_set_protocol_data(DCB* dcb, GWBUF* buf)
{
    MySQLProtocol* protocol = NULL;
    MYSQL_session* client_data = NULL;
    int client_auth_packet_size = 0;
    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);

    client_data = (MYSQL_session*)dcb->data;

    client_auth_packet_size = gwbuf_length(buf);

    /* For clients supporting CLIENT_PROTOCOL_41
     * the Handshake Response Packet is:
     *
     * 4            bytes mysql protocol heade
     * 4            bytes capability flags
     * 4            max-packet size
     * 1            byte character set
     * string[23]   reserved (all [0])
     * ...
     * ...
     * Note that the fixed elements add up to 36
     */

    /* Detect now if there are enough bytes to continue */
    if (client_auth_packet_size < (4 + 4 + 4 + 1 + 23))
    {
        /* Packet is not big enough */
        return false;
    }

    return mysql_auth_set_client_data(client_data, protocol, buf);
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
static bool mysql_auth_set_client_data(MYSQL_session* client_data,
                                       MySQLProtocol* protocol,
                                       GWBUF* buffer)
{
    int client_auth_packet_size = gwbuf_length(buffer);
    uint8_t client_auth_packet[client_auth_packet_size];
    gwbuf_copy_data(buffer, 0, client_auth_packet_size, client_auth_packet);

    int packet_length_used = 0;

    /* Make authentication token length 0 and token null in case none is provided */
    client_data->auth_token_len = 0;
    client_data->auth_token = NULL;

    if (client_auth_packet_size > MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        /* Should have a username */
        uint8_t* name = client_auth_packet + MYSQL_AUTH_PACKET_BASE_SIZE;
        uint8_t* end = client_auth_packet + sizeof(client_auth_packet);
        int user_length = 0;

        while (name < end && *name)
        {
            name++;
            user_length++;
        }

        if (name == end)
        {
            // The name is not null terminated
            return false;
        }

        if (client_auth_packet_size > (MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1))
        {
            /* Extra 1 is for the terminating null after user name */
            packet_length_used = MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1;
            /*
             * We should find an authentication token next
             * One byte of packet is the length of authentication token
             */
            client_data->auth_token_len = client_auth_packet[packet_length_used];

            if (client_auth_packet_size
                > (packet_length_used + client_data->auth_token_len))
            {
                client_data->auth_token = (uint8_t*)MXS_MALLOC(client_data->auth_token_len);

                if (client_data->auth_token)
                {
                    /* The extra 1 is for the token length byte, just extracted*/
                    memcpy(client_data->auth_token,
                           client_auth_packet + MYSQL_AUTH_PACKET_BASE_SIZE + user_length + 1 + 1,
                           client_data->auth_token_len);
                }
                else
                {
                    /* Failed to allocate space for authentication token string */
                    return false;
                }
            }
            else
            {
                /* Packet was too small to contain authentication token */
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    return true;
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
static bool mysql_auth_is_client_ssl_capable(DCB* dcb)
{
    MySQLProtocol* protocol;

    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    return (protocol->client_capabilities & (int)GW_MYSQL_CAPABILITIES_SSL) ? true : false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * Currently all that is required is to free the storage pointed to by
 * dcb->data.  But this is intended to be implemented as part of the
 * authentication API at which time this code will be moved into the
 * MySQL authenticator.  If the data structure were to become more complex
 * the mechanism would still work and be the responsibility of the authenticator.
 * The DCB should not know authenticator implementation details.
 *
 * @param dcb Request handler DCB connected to the client
 */
static void mysql_auth_free_client_data(DCB* dcb)
{
    MXS_FREE(dcb->data);
}

/**
 * @brief Inject the service user into the cache
 *
 * @param port Service listener
 * @return True on success, false on error
 */
static bool add_service_user(SERV_LISTENER* port)
{
    const char* user = NULL;
    const char* password = NULL;
    bool rval = false;

    serviceGetUser(port->service, &user, &password);

    char* pw;

    if ((pw = decrypt_password(password)))
    {
        char* newpw = create_hex_sha1_sha1_passwd(pw);

        if (newpw)
        {
            MYSQL_AUTH* inst = (MYSQL_AUTH*)port->auth_instance;
            sqlite3* handle = get_handle(inst);
            add_mysql_user(handle, user, "%", "", "Y", newpw);
            add_mysql_user(handle, user, "localhost", "", "Y", newpw);
            MXS_FREE(newpw);
            rval = true;
        }
        MXS_FREE(pw);
    }
    else
    {
        MXS_ERROR("[%s] Failed to decrypt service user password.", port->service->name);
    }

    return rval;
}

static bool service_has_servers(SERVICE* service)
{
    for (SERVER_REF* s = service->dbref; s; s = s->next)
    {
        if (s->active)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Load MySQL authentication users
 *
 * This function loads MySQL users from the backend database.
 *
 * @param port Listener definition
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR and
 * MXS_AUTH_LOADUSERS_FATAL on fatal error
 */
static int mysql_auth_load_users(SERV_LISTENER* port)
{
    int rc = MXS_AUTH_LOADUSERS_OK;
    SERVICE* service = port->listener->service;
    MYSQL_AUTH* instance = (MYSQL_AUTH*)port->auth_instance;
    bool first_load = false;

    if (should_check_permissions(instance))
    {
        if (!check_service_permissions(port->service))
        {
            return MXS_AUTH_LOADUSERS_FATAL;
        }

        // Permissions are OK, no need to check them again
        instance->check_permissions = false;
        first_load = true;
    }

    int loaded = replace_mysql_users(port, first_load);
    bool injected = false;

    if (loaded <= 0)
    {
        if (loaded < 0)
        {
            MXS_ERROR("[%s] Unable to load users for listener %s listening at [%s]:%d.",
                      service->name,
                      port->name,
                      port->address ? port->address : "::",
                      port->port);
        }

        if (instance->inject_service_user)
        {
            /** Inject the service user as a 'backup' user that's available
             * if loading of the users fails */
            if (!add_service_user(port))
            {
                MXS_ERROR("[%s] Failed to inject service user.", port->service->name);
            }
            else
            {
                injected = true;
            }
        }
    }

    if (injected)
    {
        if (service_has_servers(service))
        {
            MXS_NOTICE("[%s] No users were loaded but 'inject_service_user' is enabled. "
                       "Enabling service credentials for authentication until "
                       "database users have been successfully loaded.",
                       service->name);
        }
    }
    else if (loaded == 0 && !first_load)
    {
        MXS_WARNING("[%s]: failed to load any user information. Authentication"
                    " will probably fail as a result.",
                    service->name);
    }
    else if (loaded > 0 && first_load)
    {
        MXS_NOTICE("[%s] Loaded %d MySQL users for listener %s.", service->name, loaded, port->name);
    }

    return rc;
}

int mysql_auth_reauthenticate(DCB* dcb,
                              const char* user,
                              uint8_t* token,
                              size_t   token_len,
                              uint8_t* scramble,
                              size_t   scramble_len,
                              uint8_t* output_token,
                              size_t   output_token_len)
{
    MYSQL_session* client_data = (MYSQL_session*)dcb->data;
    MYSQL_session temp;
    int rval = 1;

    memcpy(&temp, client_data, sizeof(*client_data));
    strcpy(temp.user, user);
    temp.auth_token = token;
    temp.auth_token_len = token_len;

    MYSQL_AUTH* instance = (MYSQL_AUTH*)dcb->listener->auth_instance;
    int rc = validate_mysql_user(instance, dcb, &temp, scramble, scramble_len);

    if (rc != MXS_AUTH_SUCCEEDED && service_refresh_users(dcb->service) == 0)
    {
        rc = validate_mysql_user(instance, dcb, &temp, scramble, scramble_len);
    }

    if (rc == MXS_AUTH_SUCCEEDED)
    {
        memcpy(output_token, temp.client_sha1, output_token_len);
        rval = 0;
    }

    return rval;
}

int diag_cb(void* data, int columns, char** row, char** field_names)
{
    DCB* dcb = (DCB*)data;
    dcb_printf(dcb, "%s@%s ", row[0], row[1]);
    return 0;
}

void mysql_auth_diagnostic(DCB* dcb, SERV_LISTENER* port)
{
    MYSQL_AUTH* instance = (MYSQL_AUTH*)port->auth_instance;
    sqlite3* handle = get_handle(instance);
    char* err;

    if (sqlite3_exec(handle,
                     "SELECT user, host FROM " MYSQLAUTH_USERS_TABLE_NAME,
                     diag_cb,
                     dcb,
                     &err) != SQLITE_OK)
    {
        dcb_printf(dcb, "Could not access users: %s", err);
        MXS_ERROR("Could not access users: %s", err);
        sqlite3_free(err);
    }
}

int diag_cb_json(void* data, int columns, char** row, char** field_names)
{
    json_t* obj = json_object();
    json_object_set_new(obj, "user", json_string(row[0]));
    json_object_set_new(obj, "host", json_string(row[1]));

    json_t* arr = (json_t*)data;
    json_array_append_new(arr, obj);
    return 0;
}

json_t* mysql_auth_diagnostic_json(const SERV_LISTENER* port)
{
    json_t* rval = json_array();

    MYSQL_AUTH* instance = (MYSQL_AUTH*)port->auth_instance;
    char* err;
    sqlite3* handle = get_handle(instance);

    if (sqlite3_exec(handle,
                     "SELECT user, host FROM " MYSQLAUTH_USERS_TABLE_NAME,
                     diag_cb_json,
                     rval,
                     &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to print users: %s", err);
        sqlite3_free(err);
    }

    return rval;
}
