/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
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
#include <maxscale/paths.hh>
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

/**
 * Initialize the authenticator instance
 *
 * @param options Authenticator options
 * @return New authenticator module instance
 */
MariaDBAuthenticatorModule* MariaDBAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    bool log_pw_mismatch = false;
    const std::string opt_log_mismatch = "log_password_mismatch";
    if (options->contains(opt_log_mismatch))
    {
        log_pw_mismatch = options->get_bool(opt_log_mismatch);
        options->remove(opt_log_mismatch);
    }
    return new MariaDBAuthenticatorModule(log_pw_mismatch);
}

MariaDBAuthenticatorModule::MariaDBAuthenticatorModule(bool log_pw_mismatch)
    : m_log_pw_mismatch(log_pw_mismatch)
{
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

MariaDBClientAuthenticator::MariaDBClientAuthenticator(bool log_pw_mismatch)
    : m_log_pw_mismatch(log_pw_mismatch)
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
    return check_password(session, entry->password);
}

mariadb::SBackendAuth
MariaDBAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new MariaDBBackendSession(auth_data));
}

uint64_t MariaDBAuthenticatorModule::capabilities() const
{
    return 0;
}

mariadb::SClientAuth MariaDBAuthenticatorModule::create_client_authenticator()
{
    return mariadb::SClientAuth(new(std::nothrow) MariaDBClientAuthenticator(m_log_pw_mismatch));
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

mariadb::BackendAuthenticator::AuthRes
MariaDBBackendSession::exchange(const mxs::Buffer& input, mxs::Buffer* output)
{
    auto rval = AuthRes::FAIL;
    // Protocol catches Ok and Error-packets, so the only expected packet here is AuthSwitch-request.
    switch (m_state)
    {
    case State::EXPECT_AUTHSWITCH:
        {
            auto parse_res = mariadb::parse_auth_switch_request(input);
            if (parse_res.success && parse_res.plugin_data.size() == MYSQL_SCRAMBLE_LEN)
            {
                // Expecting the server to only ask for native password plugin.
                if (parse_res.plugin_name == DEFAULT_MYSQL_AUTH_PLUGIN)
                {
                    // Looks ok. The server has sent a new scramble. Save it and generate a response.
                    memcpy(m_shared_data.scramble, parse_res.plugin_data.data(), MYSQL_SCRAMBLE_LEN);
                    int old_seqno = MYSQL_GET_PACKET_NO(GWBUF_DATA(input.get()));
                    *output = generate_auth_response(old_seqno + 1);
                    m_state = State::PW_SENT;
                    rval = AuthRes::SUCCESS;
                }
                else
                {
                    MXB_ERROR(WRONG_PLUGIN_REQ, m_shared_data.servername, parse_res.plugin_name.c_str(),
                              m_shared_data.client_data->user_and_host().c_str(), DEFAULT_MYSQL_AUTH_PLUGIN);
                }
            }
            else
            {
                MXB_ERROR(MALFORMED_AUTH_SWITCH, m_shared_data.servername);
            }
        }
        break;

    case State::PW_SENT:
        // Server is sending more packets than expected. Error.
        MXB_ERROR("Server '%s' sent more packets than expected.", m_shared_data.servername);
        m_state = State::ERROR;
        break;

    case State::ERROR:
        // Should not get here.
        mxb_assert(!true);
        break;
    }
    return rval;
}

mxs::Buffer MariaDBBackendSession::generate_auth_response(int seqno)
{
    int pload_len = SHA_DIGEST_LENGTH;
    mxs::Buffer buffer(MYSQL_HEADER_LEN + pload_len);
    uint8_t* data = buffer.data();
    mariadb::set_byte3(data, pload_len);
    data[3] = seqno;
    auto& sha_pw = m_shared_data.client_data->auth_token_phase2;
    const uint8_t* curr_passwd = sha_pw.empty() ? null_client_sha1 : sha_pw.data();
    mxs_mysql_calculate_hash(m_shared_data.scramble, curr_passwd, data + MYSQL_HEADER_LEN);
    return mxs::Buffer();
}

MariaDBBackendSession::MariaDBBackendSession(mariadb::BackendAuthData& shared_data)
    : m_shared_data(shared_data)
{
}
