/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mysql_auth.hh"

#include <maxbase/alloc.h>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/event.hh>
#include <maxscale/poll.hh>
#include <maxscale/paths.h>
#include <maxscale/secrets.h>
#include <maxscale/utils.h>
#include <maxscale/routingworker.hh>

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
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The MySQL client to MaxScale authenticator implementation",
        "V1.1.0",
        MXS_NO_MODULE_CAPABILITIES, // Authenticator capabilities are in the instance object
        &mxs::AuthenticatorApiGenerator<MariaDBAuthenticatorModule>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

static bool open_instance_database(const char* path, sqlite3** handle)
{
    // This only opens database in memory if path is exactly ":memory:"
    // To use the URI filename SQLITE_OPEN_URI flag needs to be used.
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

sqlite3* MariaDBAuthenticatorModule::get_handle()
{
    int i = mxs_rworker_get_current_id();
    mxb_assert(i >= 0);

    if (m_handles[i] == NULL)
    {
        MXB_AT_DEBUG(bool rval = ) open_instance_database(":memory:", &m_handles[i]);
        mxb_assert(rval);
    }
    return m_handles[i];
}

/**
 * @brief Initialize the authenticator instance
 *
 * @param options Authenticator options
 * @return New MYSQL_AUTH instance or NULL on error
 */
MariaDBAuthenticatorModule* MariaDBAuthenticatorModule::create(char** options)
{
    auto instance = new(std::nothrow) MariaDBAuthenticatorModule();
    if (instance
        && (instance->m_handles = static_cast<sqlite3**>(MXS_CALLOC(config_threadcount(), sizeof(sqlite3*)))))
    {
        bool error = false;
        for (int i = 0; options[i]; i++)
        {
            char* value = strchr(options[i], '=');

            if (value)
            {
                *value++ = '\0';

                if (strcmp(options[i], "cache_dir") == 0)
                {
                    if ((instance->m_cache_dir = MXS_STRDUP(value)) == NULL
                        || !clean_up_pathname(instance->m_cache_dir))
                    {
                        error = true;
                    }
                }
                else if (strcmp(options[i], "inject_service_user") == 0)
                {
                    instance->m_inject_service_user = config_truth_value(value);
                }
                else if (strcmp(options[i], "skip_authentication") == 0)
                {
                    instance->m_skip_auth = config_truth_value(value);
                }
                else if (strcmp(options[i], "lower_case_table_names") == 0)
                {
                    instance->m_lower_case_table_names = config_truth_value(value);
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
            MXS_FREE(instance->m_cache_dir);
            MXS_FREE(instance->m_handles);
            delete instance;
            instance = NULL;
        }
    }
    else if (instance)
    {
        delete instance;
        instance = NULL;
    }

    return instance;
}

static bool is_localhost_address(const struct sockaddr_storage* addr)
{
    bool rval = false;

    if (addr->ss_family == AF_INET)
    {
        const struct sockaddr_in* ip = (const struct sockaddr_in*)addr;
        if (ip->sin_addr.s_addr == INADDR_LOOPBACK)
        {
            rval = true;
        }
    }
    else if (addr->ss_family == AF_INET6)
    {
        const struct sockaddr_in6* ip = (const struct sockaddr_in6*)addr;
        if (memcmp(&ip->sin6_addr, &in6addr_loopback, sizeof(ip->sin6_addr)) == 0)
        {
            rval = true;
        }
    }

    return rval;
}

// Helper function for generating an AuthSwitchRequest packet.
static GWBUF* gen_auth_switch_request_packet(MYSQL_session* client_data, const uint8_t* scramble)
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * string[EOF] - Scramble
     */
    const char plugin[] = DEFAULT_MYSQL_AUTH_PLUGIN;

    /* When sending an AuthSwitchRequest for "mysql_native_password", the scramble data needs an extra
     * byte in the end. */
    unsigned int payloadlen = 1 + sizeof(plugin) + GW_MYSQL_SCRAMBLE_SIZE + 1;
    unsigned int buflen = MYSQL_HEADER_LEN + payloadlen;
    GWBUF* buffer = gwbuf_alloc(buflen);
    uint8_t* bufdata = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(bufdata, payloadlen);
    bufdata += 3;
    *bufdata++ = client_data->next_sequence;
    *bufdata++ = MYSQL_REPLY_AUTHSWITCHREQUEST;     // AuthSwitchRequest command
    memcpy(bufdata, plugin, sizeof(plugin));
    bufdata += sizeof(plugin);
    memcpy(bufdata, scramble, GW_MYSQL_SCRAMBLE_SIZE);
    bufdata += GW_MYSQL_SCRAMBLE_SIZE;
    *bufdata = '\0';
    return buffer;
}

MariaDBClientAuthenticator::MariaDBClientAuthenticator(MariaDBAuthenticatorModule* module)
    : ClientAuthenticatorT(module)
{
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
int MariaDBClientAuthenticator::authenticate(DCB* generic_dcb)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    int auth_ret = MXS_AUTH_SSL_COMPLETE;
    auto protocol = static_cast<MariaDBClientConnection*>(dcb->protocol());
    auto client_data = static_cast<MYSQL_session*>(dcb->session()->protocol_data());
    if (*client_data->user)
    {
        MXS_DEBUG("Receiving connection from '%s' to database '%s'.",
                  client_data->user,
                  client_data->db);

        if (!m_correct_authenticator)
        {
            // Client is attempting to use wrong authenticator, send switch request packet.
            GWBUF* switch_packet = gen_auth_switch_request_packet(client_data, protocol->scramble());
            if (dcb->writeq_append(switch_packet))
            {
                m_auth_switch_sent = true;
                return MXS_AUTH_INCOMPLETE;
            }
            else
            {
                return MXS_AUTH_FAILED;
            }
        }

        auth_ret = validate_mysql_user(dcb, client_data,
                                       protocol->scramble(), MYSQL_SCRAMBLE_LEN,
                                       client_data->auth_token, client_data->client_sha1);

        if (auth_ret != MXS_AUTH_SUCCEEDED && service_refresh_users(dcb->service()))
        {
            auth_ret = validate_mysql_user(dcb, client_data,
                                           protocol->scramble(), MYSQL_SCRAMBLE_LEN,
                                           client_data->auth_token, client_data->client_sha1);
        }

        /* on successful authentication, set user into dcb field */
        if (auth_ret == MXS_AUTH_SUCCEEDED)
        {
            auth_ret = MXS_AUTH_SUCCEEDED;
            dcb->session()->set_user(client_data->user);
            /** Send an OK packet to the client */
        }
        else if (dcb->service()->config().log_auth_warnings)
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

            MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                          "%s: login attempt for user '%s'@[%s]:%d, authentication failed. %s",
                          dcb->service()->name(),
                          client_data->user,
                          dcb->remote().c_str(),
                          static_cast<ClientDCB*>(dcb)->port(),
                          extra);

            if (is_localhost_address(&dcb->ip()) && !dcb->service()->config().localhost_match_wildcard_host)
            {
                MXS_NOTICE("If you have a wildcard grant that covers this address, "
                           "try adding 'localhost_match_wildcard_host=true' for "
                           "service '%s'. ",
                           dcb->service()->name());
            }
        }

        client_data->auth_token.clear();
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
bool MariaDBClientAuthenticator::extract(DCB* generic_dcb, GWBUF* buf)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    int client_auth_packet_size = gwbuf_length(buf);
    auto client_data = static_cast<MYSQL_session*>(dcb->session()->protocol_data());

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

    /* Check that the packet length is reasonable. The first packet needs to be sufficiently large to
     * contain required data. If the buffer is unexpectedly large (likely an erroneous or malicious client),
     * discard the packet as parsing it may cause overflow. The limit is just a guess, but it seems the
     * packets from most plugins are < 100 bytes. */
    if ((!m_auth_switch_sent
         && (client_auth_packet_size >= MYSQL_AUTH_PACKET_BASE_SIZE && client_auth_packet_size < 1028))
        // If the client is replying to an AuthSwitchRequest, the length is predetermined.
        || (m_auth_switch_sent
            && (client_auth_packet_size == MYSQL_HEADER_LEN + MYSQL_SCRAMBLE_LEN)))
    {
        return set_client_data(client_data, dcb, buf);
    }
    else
    {
        /* Packet is not big enough */
        return false;
    }
}

/**
 * Helper function for reading a 0-terminated string safely from an array that may not be 0-terminated.
 * The output array should be long enough to contain any string that fits into the packet starting from
 * packet_length_used.
 */
static bool read_zstr(const uint8_t* client_auth_packet, size_t client_auth_packet_size,
                      int* packet_length_used, char* output)
{
    int null_char_ind = -1;
    int start_ind = *packet_length_used;
    for (size_t i = start_ind; i < client_auth_packet_size; i++)
    {
        if (client_auth_packet[i] == '\0')
        {
            null_char_ind = i;
            break;
        }
    }

    if (null_char_ind >= 0)
    {
        if (output)
        {
            memcpy(output, client_auth_packet + start_ind, null_char_ind - start_ind + 1);
        }
        *packet_length_used = null_char_ind + 1;
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Transfer detailed data from the authentication request to the DCB.
 *
 * The caller has created the data structure pointed to by the DCB, and this
 * function fills in the details. If problems are found with the data, the
 * return code indicates failure.
 *
 * @param client_data The data structure for the DCB
 * @param client_dcb The client DCB.
 * @param buffer The authentication data.
 * @return True on success, false on error
 */
bool MariaDBClientAuthenticator::set_client_data(MYSQL_session* client_data, DCB* client_dcb, GWBUF* buffer)
{
    int client_auth_packet_size = gwbuf_length(buffer);
    uint8_t client_auth_packet[client_auth_packet_size];
    gwbuf_copy_data(buffer, 0, client_auth_packet_size, client_auth_packet);

    int packet_length_used = 0;

    client_data->auth_token.clear(); // Usually no-op
    m_correct_authenticator = false;

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
            int auth_token_len = client_auth_packet[packet_length_used];
            packet_length_used++;

            if (client_auth_packet_size < (packet_length_used + auth_token_len))
            {
                /* Packet was too small to contain authentication token */
                return false;
            }
            else
            {
                uint8_t* copy_begin = client_auth_packet + packet_length_used;
                uint8_t* copy_end = copy_begin + auth_token_len;
                client_data->auth_token.assign(copy_begin, copy_end);
                packet_length_used += auth_token_len;

                auto client_capabilities = client_data->client_capabilities();
                // Database name may be next. It has already been read and is skipped.
                if (client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB)
                {
                    if (!read_zstr(client_auth_packet, client_auth_packet_size,
                                   &packet_length_used, NULL))
                    {
                        return false;
                    }
                }

                // Authentication plugin name.
                if (client_capabilities & GW_MYSQL_CAPABILITIES_PLUGIN_AUTH)
                {
                    int bytes_left = client_auth_packet_size - packet_length_used;
                    if (bytes_left < 1)
                    {
                        return false;
                    }
                    else
                    {
                        char plugin_name[bytes_left];
                        if (!read_zstr(client_auth_packet, client_auth_packet_size,
                                       &packet_length_used, plugin_name))
                        {
                            return false;
                        }
                        else
                        {
                            // Check that the plugin is as expected. If not, make a note so the
                            // authentication function switches the plugin.
                            bool correct_auth = strcmp(plugin_name, DEFAULT_MYSQL_AUTH_PLUGIN) == 0;
                            m_correct_authenticator = correct_auth;
                            if (!correct_auth)
                            {
                                // The switch attempt is done later but the message is clearest if
                                // logged at once.
                                MXS_INFO("Client '%s'@[%s] is using an unsupported authenticator "
                                         "plugin '%s'. Trying to switch to '%s'.",
                                         client_data->user, client_dcb->remote().c_str(), plugin_name,
                                         DEFAULT_MYSQL_AUTH_PLUGIN);
                            }
                        }
                    }
                }
                    else
                    {
                        m_correct_authenticator = true;
                    }
            }
        }
        else
        {
            return false;
        }
    }
    else if (m_auth_switch_sent)
    {
        // Client is replying to an AuthSwitch request. The packet should contain the authentication token.
        // Length has already been checked.
        mxb_assert(client_auth_packet_size == MYSQL_HEADER_LEN + MYSQL_SCRAMBLE_LEN);
        uint8_t* copy_begin = client_auth_packet + MYSQL_HEADER_LEN;
        uint8_t* copy_end = copy_begin + MYSQL_SCRAMBLE_LEN;
        client_data->auth_token.assign(copy_begin, copy_end);
        // Assume that correct authenticator is now used. If this is not the case, authentication fails.
        m_correct_authenticator = true;
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
bool MariaDBClientAuthenticator::ssl_capable(DCB* dcb)
{
    auto mariadbses = static_cast<MYSQL_session*>(dcb->session()->protocol_data());
    return mariadbses->ssl_capable();
}

/**
 * @brief Inject the service user into the cache
 *
 * @return True on success, false on error
 */
bool MariaDBAuthenticatorModule::add_service_user(SERVICE* service)
{
    const char* user = NULL;
    const char* password = NULL;
    bool rval = false;

    serviceGetUser(service, &user, &password);

    char* pw;

    if ((pw = decrypt_password(password)))
    {
        char* newpw = create_hex_sha1_sha1_passwd(pw);

        if (newpw)
        {
            sqlite3* handle = get_handle();
            add_mysql_user(handle, user, "%", "", "Y", newpw);
            add_mysql_user(handle, user, "localhost", "", "Y", newpw);
            MXS_FREE(newpw);
            rval = true;
        }
        MXS_FREE(pw);
    }
    else
    {
        MXS_ERROR("[%s] Failed to decrypt service user password.", service->name());
    }

    return rval;
}

static bool service_has_servers(SERVICE* service)
{
    return !service->reachable_servers().empty();
}

/**
 * @brief Load MySQL authentication users
 *
 * This function loads MySQL users from the backend database.
 *
 * @param service Service definition
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR and
 * MXS_AUTH_LOADUSERS_FATAL on fatal error
 */
int MariaDBAuthenticatorModule::load_users(SERVICE* service)
{
    int rc = MXS_AUTH_LOADUSERS_OK;
    bool first_load = false;

    if (m_check_permissions)
    {
        if (!check_service_permissions(service))
        {
            return MXS_AUTH_LOADUSERS_FATAL;
        }

        // Permissions are OK, no need to check them again
        m_check_permissions = false;
        first_load = true;
    }

    SERVER* srv = nullptr;
    int loaded = get_users(service, first_load, &srv);
    bool injected = false;

    if (loaded <= 0)
    {
        if (loaded < 0)
        {
            MXB_ERROR("Unable to load users for service %s.", service->name());
        }

        if (m_inject_service_user)
        {
            /** Inject the service user as a 'backup' user that's available
             * if loading of the users fails */
            if (!add_service_user(service))
            {
                MXS_ERROR("[%s] Failed to inject service user.", service->name());
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
                       service->name());
        }
    }
    else if (loaded == 0 && !first_load)
    {
        MXS_WARNING("[%s]: failed to load any user information. Authentication"
                    " will probably fail as a result.",
                    service->name());
    }
    else if (loaded > 0 && first_load)
    {
        mxb_assert(srv);
        MXS_NOTICE("Loaded %d MySQL users for service %s from server %s.",
                   loaded, service->name(), srv->name());
    }

    return rc;
}

int MariaDBClientAuthenticator::reauthenticate(DCB* generic_dcb, uint8_t* scramble, size_t scramble_len,
                                               const ByteVec& auth_token, uint8_t* output_token)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);
    auto client_data = static_cast<MYSQL_session*>(dcb->session()->protocol_data());
    int rval = 1;

    uint8_t phase2_scramble[MYSQL_SCRAMBLE_LEN];
    int rc = validate_mysql_user(dcb, client_data, scramble, scramble_len, auth_token, phase2_scramble);

    if (rc != MXS_AUTH_SUCCEEDED && service_refresh_users(dcb->service()))
    {
        rc = validate_mysql_user(dcb, client_data, scramble, scramble_len, auth_token, phase2_scramble);
    }

    if (rc == MXS_AUTH_SUCCEEDED)
    {
        memcpy(output_token, phase2_scramble, sizeof(phase2_scramble));
        rval = 0;
    }

    return rval;
}

std::unique_ptr<mxs::BackendAuthenticator> MariaDBAuthenticatorModule::create_backend_authenticator()
{
    return std::unique_ptr<mxs::BackendAuthenticator>(new(std::nothrow) MariaDBBackendSession());
}

int diag_cb(void* data, int columns, char** row, char** field_names)
{
    DCB* dcb = (DCB*)data;
    dcb_printf(dcb, "%s@%s ", row[0], row[1]);
    return 0;
}

void MariaDBAuthenticatorModule::diagnostics(DCB* dcb)
{
    sqlite3* handle = get_handle();
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

json_t* MariaDBAuthenticatorModule::diagnostics_json()
{
    json_t* rval = json_array();

    char* err;
    sqlite3* handle = get_handle();

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

uint64_t MariaDBAuthenticatorModule::capabilities() const
{
    return CAP_REAUTHENTICATE | CAP_CONC_LOAD_USERS | CAP_BACKEND_AUTH;
}

std::unique_ptr<mxs::ClientAuthenticator> MariaDBAuthenticatorModule::create_client_authenticator()
{
    return std::unique_ptr<mxs::ClientAuthenticator>(new(std::nothrow) MariaDBClientAuthenticator(this));
}

std::string MariaDBAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::string MariaDBAuthenticatorModule::name() const
{
    return MXS_MODULE_NAME;
}

bool MariaDBBackendSession::extract(DCB* backend, GWBUF* buffer)
{
    bool rval = false;

    switch (state)
    {
        case State::NEED_OK:
            if (mxs_mysql_is_ok_packet(buffer))
            {
                rval = true;
                state = State::AUTH_OK;
            }
            else
            {
                state = State::AUTH_FAILED;
            }
            break;

        default:
            MXS_ERROR("Unexpected call to MySQLBackendAuth::extract");
            mxb_assert(false);
            break;
    }

    return rval;
}

bool MariaDBBackendSession::ssl_capable(DCB* dcb)
{
    // TODO: The argument should be a BackendDCB.
    mxb_assert(dcb->role() == DCB::Role::BACKEND);
    BackendDCB* backend = static_cast<BackendDCB*>(dcb);
    return backend->server()->ssl().context() != nullptr;
}

int MariaDBBackendSession::authenticate(DCB* backend)
{
    int rval = MXS_AUTH_FAILED;
    if (state == State::AUTH_OK)
    {
        /** Authentication completed successfully */
        rval = MXS_AUTH_SUCCEEDED;
    }
    return rval;
}
