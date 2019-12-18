/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-12-18
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
#include <maxscale/secrets.hh>
#include <maxscale/utils.h>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;

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
        MXS_NO_MODULE_CAPABILITIES,     // Authenticator capabilities are in the instance object
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

static sqlite3* open_instance_database(const char* path)
{
    sqlite3* handle;

    // This only opens database in memory if path is exactly ":memory:"
    // To use the URI filename SQLITE_OPEN_URI flag needs to be used.
    int rc = sqlite3_open_v2(path, &handle, db_flags, NULL);

    if (rc != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite3 handle: %d", rc);
        return nullptr;
    }

    char* err;

    if (sqlite3_exec(handle, users_create_sql, NULL, NULL, &err) != SQLITE_OK
        || sqlite3_exec(handle, databases_create_sql, NULL, NULL, &err) != SQLITE_OK
        || sqlite3_exec(handle, pragma_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to create database: %s", err);
        sqlite3_free(err);
        sqlite3_close_v2(handle);
        return nullptr;
    }

    return handle;
}

sqlite3* MariaDBAuthenticatorModule::get_handle()
{
    sqlite3* handle = *m_handle;

    if (!handle)
    {
        handle = open_instance_database(":memory:");
        mxb_assert(handle);
        *m_handle = handle;
    }

    return handle;
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
    if (instance)
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
static GWBUF* gen_auth_switch_request_packet(MYSQL_session* client_data)
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
    unsigned int payloadlen = 1 + sizeof(plugin) + MYSQL_SCRAMBLE_LEN + 1;
    unsigned int buflen = MYSQL_HEADER_LEN + payloadlen;
    GWBUF* buffer = gwbuf_alloc(buflen);
    uint8_t* bufdata = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(bufdata, payloadlen);
    bufdata += 3;
    *bufdata++ = client_data->next_sequence;
    *bufdata++ = MYSQL_REPLY_AUTHSWITCHREQUEST;     // AuthSwitchRequest command
    memcpy(bufdata, plugin, sizeof(plugin));
    bufdata += sizeof(plugin);
    memcpy(bufdata, client_data->scramble, MYSQL_SCRAMBLE_LEN);
    bufdata += GW_MYSQL_SCRAMBLE_SIZE;
    *bufdata = '\0';
    return buffer;
}

MariaDBClientAuthenticator::MariaDBClientAuthenticator(MariaDBAuthenticatorModule* module)
    : ClientAuthenticatorT(module)
{
}

mariadb::ClientAuthenticator::ExchRes
MariaDBClientAuthenticator::exchange(GWBUF* buf, MYSQL_session* session, mxs::Buffer* output_packet)
{
    auto client_data = session;
    auto rval = ExchRes::FAIL;

    switch (m_state)
    {
    case State::INIT:
        // First, check that session is using correct plugin. The handshake response has already been
        // parsed in protocol code.
        if (client_data->plugin == DEFAULT_MYSQL_AUTH_PLUGIN)
        {
            // Correct plugin, token should have been read by protocol code.
            m_state = State::CHECK_TOKEN;
            rval = ExchRes::READY;
        }
        else
        {
            // Client is attempting to use wrong authenticator, send switch request packet.
            MXS_INFO("Client '%s'@'%s' is using an unsupported authenticator "
                     "plugin '%s'. Trying to switch to '%s'.",
                     client_data->user.c_str(), client_data->remote.c_str(),
                     client_data->plugin.c_str(), DEFAULT_MYSQL_AUTH_PLUGIN);
            GWBUF* switch_packet = gen_auth_switch_request_packet(client_data);
            if (switch_packet)
            {
                output_packet->reset(switch_packet);
                m_state = State::AUTHSWITCH_SENT;
                rval = ExchRes::INCOMPLETE;
            }
        }
        break;

    case State::AUTHSWITCH_SENT:
        {
            // Client is replying to an AuthSwitch request. The packet should contain
            // the authentication token.
            if (gwbuf_length(buf) == MYSQL_HEADER_LEN + MYSQL_SCRAMBLE_LEN)
            {
                auto& auth_token = client_data->auth_token;
                auth_token.clear();
                auth_token.resize(MYSQL_SCRAMBLE_LEN);
                gwbuf_copy_data(buf, MYSQL_HEADER_LEN, MYSQL_SCRAMBLE_LEN, auth_token.data());
                // Assume that correct authenticator is now used. If this is not the case,
                // authentication will fail.
                m_state = State::CHECK_TOKEN;
                rval = ExchRes::READY;
            }
        }
        break;

    default:
        mxb_assert(!true);
        break;
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
AuthRes MariaDBClientAuthenticator::authenticate(DCB* generic_dcb, const UserEntry* entry,
                                                 MYSQL_session* session)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    mxb_assert(m_state == State::CHECK_TOKEN);

    auto auth_ret = validate_mysql_user(entry, session, session->scramble, MYSQL_SCRAMBLE_LEN,
                                        session->auth_token, session->client_sha1);

    if (auth_ret != AuthRes::SUCCESS && dcb->service()->config().log_auth_warnings)
    {
        // The default failure is a `User not found` one
        char extra[256] = "User not found.";

        if (auth_ret == AuthRes::FAIL_WRONG_PW)
        {
            strcpy(extra, "Wrong password.");
        }

        MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE,
                      "%s: login attempt for user '%s'@[%s]:%d, authentication failed. %s",
                      dcb->service()->name(),
                      session->user.c_str(),
                      dcb->remote().c_str(),
                      static_cast<ClientDCB*>(dcb)->port(),
                      extra);

        if (is_localhost_address(&dcb->ip())
            && !dcb->service()->config().localhost_match_wildcard_host)
        {
            MXS_NOTICE("If you have a wildcard grant that covers this address, "
                       "try adding 'localhost_match_wildcard_host=true' for "
                       "service '%s'. ",
                       dcb->service()->name());
        }
    }
    return auth_ret;
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

    std::string pw = decrypt_password(password);
    char* newpw = create_hex_sha1_sha1_passwd(pw.c_str());
    if (newpw)
    {
        sqlite3* handle = get_handle();
        add_mysql_user(handle, user, "%", "", "Y", newpw);
        add_mysql_user(handle, user, "localhost", "", "Y", newpw);
        MXS_FREE(newpw);
        rval = true;
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

AuthRes MariaDBClientAuthenticator::reauthenticate(const UserEntry* entry, DCB* generic_dcb,
                                                   uint8_t* scramble, size_t scramble_len,
                                                   const ByteVec& auth_token, uint8_t* output_token)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);
    auto client_data = static_cast<MYSQL_session*>(dcb->session()->protocol_data());
    auto rval = AuthRes::FAIL;

    uint8_t phase2_scramble[MYSQL_SCRAMBLE_LEN];
    auto rc = validate_mysql_user(entry, client_data, scramble, scramble_len, auth_token, phase2_scramble);

    if (rc == AuthRes::SUCCESS)
    {
        memcpy(output_token, phase2_scramble, sizeof(phase2_scramble));
        rval = AuthRes::SUCCESS;
    }

    return rval;
}

mariadb::SBackendAuth MariaDBAuthenticatorModule::create_backend_authenticator()
{
    return mariadb::SBackendAuth(new(std::nothrow) MariaDBBackendSession());
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

json_t* MariaDBAuthenticatorModule::diagnostics()
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

mariadb::SClientAuth MariaDBAuthenticatorModule::create_client_authenticator()
{
    return mariadb::SClientAuth(new(std::nothrow) MariaDBClientAuthenticator(this));
}

std::string MariaDBAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::string MariaDBAuthenticatorModule::name() const
{
    return MXS_MODULE_NAME;
}

const std::unordered_set<std::string>& MariaDBAuthenticatorModule::supported_plugins() const
{
    // Support the empty plugin as well, as that means default.
    static const std::unordered_set<std::string> plugins = {
        "mysql_native_password", "caching_sha2_password", ""};
    return plugins;
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

mariadb::BackendAuthenticator::AuthRes MariaDBBackendSession::authenticate(DCB* backend)
{
    auto rval = AuthRes::FAIL;
    if (state == State::AUTH_OK)
    {
        /** Authentication completed successfully */
        rval = AuthRes::SUCCESS;
    }
    return rval;
}
