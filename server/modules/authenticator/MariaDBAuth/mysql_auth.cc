/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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
MariaDBAuthenticatorModule* MariaDBAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    return new(std::nothrow) MariaDBAuthenticatorModule();
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

AuthRes MariaDBClientAuthenticator::authenticate(const UserEntry* entry, MYSQL_session* session)
{
    mxb_assert(m_state == State::CHECK_TOKEN);
    AuthRes auth_ret;
    auth_ret.status = validate_mysql_user(entry, session) ? AuthRes::Status::SUCCESS :
        AuthRes::Status::FAIL_WRONG_PW;
    return auth_ret;
}

mariadb::SBackendAuth
MariaDBAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
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
    return 0;
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
