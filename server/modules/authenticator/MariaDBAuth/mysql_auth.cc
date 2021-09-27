/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXS_MODULE_NAME MXS_MARIADBAUTH_AUTHENTICATOR_NAME

#include "mysql_auth.hh"
#include <maxbase/alloc.h>
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/built_in_modules.hh>
#include <maxscale/config_common.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;

namespace
{
// Support the empty plugin as well, as that means default.
const std::unordered_set<std::string> plugins = {"mysql_native_password", "caching_sha2_password",
                                                 "mysql_clear_password",  ""};
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

uint64_t MariaDBAuthenticatorModule::capabilities() const
{
    return 0;
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
    return plugins;
}

mariadb::SClientAuth MariaDBAuthenticatorModule::create_client_authenticator()
{
    return mariadb::SClientAuth(new(std::nothrow) MariaDBClientAuthenticator(m_log_pw_mismatch));
}

mariadb::SBackendAuth
MariaDBAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new MariaDBBackendSession(auth_data));
}

mariadb::AuthByteVec MariaDBAuthenticatorModule::generate_token(const std::string& password)
{
    mariadb::AuthByteVec rval;
    if (!password.empty())
    {
        rval.resize(SHA_DIGEST_LENGTH);
        gw_sha1_str((const uint8_t*)password.c_str(), password.length(), rval.data());
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
    bufdata = mariadb::write_header(bufdata, payloadlen, 0);// protocol will set sequence
    *bufdata++ = MYSQL_REPLY_AUTHSWITCHREQUEST;             // AuthSwitchRequest command
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
MariaDBClientAuthenticator::exchange(GWBUF* buf, MYSQL_session* session)
{
    using ExchRes = mariadb::ClientAuthenticator::ExchRes;
    ExchRes rval;
    auto client_data = session;

    switch (m_state)
    {
    case State::INIT:
        // First, check that session is using correct plugin. The handshake response has already been
        // parsed in protocol code. Some old clients may send an empty plugin name. If so, assume
        // that they are using "mysql_native_password". If this is not the case, authentication will fail.
        if (client_data->plugin == DEFAULT_MYSQL_AUTH_PLUGIN || client_data->plugin.empty())
        {
            // Correct plugin, token should have been read by protocol code.
            m_state = State::CHECK_TOKEN;
            rval.status = ExchRes::Status::READY;
        }
        else
        {
            // Client is attempting to use wrong authenticator, send switch request packet.
            MXS_INFO("Client %s is using an unsupported authenticator plugin '%s'. Trying to "
                     "switch to '%s'.",
                     client_data->user_and_host().c_str(),
                     client_data->plugin.c_str(), DEFAULT_MYSQL_AUTH_PLUGIN);
            GWBUF* switch_packet = gen_auth_switch_request_packet(client_data);
            if (switch_packet)
            {
                rval.packet.reset(switch_packet);
                m_state = State::AUTHSWITCH_SENT;
                rval.status = ExchRes::Status::INCOMPLETE;
            }
        }
        break;

    case State::AUTHSWITCH_SENT:
        {
            // Client is replying to an AuthSwitch request. The packet should contain
            // the authentication token.
            if (gwbuf_length(buf) == MYSQL_HEADER_LEN + MYSQL_SCRAMBLE_LEN)
            {
                auto& auth_token = client_data->client_token;
                auth_token.clear();
                auth_token.resize(MYSQL_SCRAMBLE_LEN);
                gwbuf_copy_data(buf, MYSQL_HEADER_LEN, MYSQL_SCRAMBLE_LEN, auth_token.data());
                // Assume that correct authenticator is now used. If this is not the case,
                // authentication will fail.
                m_state = State::CHECK_TOKEN;
                rval.status = ExchRes::Status::READY;
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

/**
 * Check if auth token sent by client matches the one in the user account entry.
 *
 * @param session Client session with auth token
 * @param stored_pw_hash2 SHA1(SHA1(password)) in hex form, as queried from server.
 * @return Authentication result
 */
AuthRes MariaDBClientAuthenticator::check_password(MYSQL_session* session, const std::string& stored_pw_hash2)
{

    const auto& auth_token = session->client_token;     // Binary-form token sent by client.

    bool empty_token = auth_token.empty();
    bool empty_pw = stored_pw_hash2.empty();
    if (empty_token || empty_pw)
    {
        AuthRes rval;
        if (empty_token && empty_pw)
        {
            // If the user entry has empty password and the client gave no password, accept.
            rval.status = AuthRes::Status::SUCCESS;
        }
        else if (m_log_pw_mismatch)
        {
            // Save reason of failure.
            rval.msg = empty_token ? "Client gave no password when one was expected" :
                "Client gave a password when none was expected";
        }
        return rval;
    }
    else if (auth_token.size() != SHA_DIGEST_LENGTH)
    {
        AuthRes rval;
        rval.msg = mxb::string_printf("Client authentication token is %zu bytes when %i was expected",
                                      auth_token.size(), SHA_DIGEST_LENGTH);
        return rval;
    }
    else if (stored_pw_hash2.length() != 2 * SHA_DIGEST_LENGTH)
    {
        AuthRes rval;
        rval.msg = mxb::string_printf("Stored password hash length is %lu when %i was expected",
                                      stored_pw_hash2.length(), 2 * SHA_DIGEST_LENGTH);
        return rval;
    }

    uint8_t stored_pw_hash2_bin[SHA_DIGEST_LENGTH] = {};
    size_t stored_hash_len = sizeof(stored_pw_hash2_bin);

    // Convert the hexadecimal string to binary.
    mxs::hex2bin(stored_pw_hash2.c_str(), stored_pw_hash2.length(), stored_pw_hash2_bin);

    /**
     * The client authentication token is made up of:
     *
     * XOR( SHA1(real_password), SHA1( CONCAT( scramble, <value of mysql.user.password> ) ) )
     *
     * Since we know the scramble and the value stored in mysql.user.password,
     * we can extract the SHA1 of the real password by doing a XOR of the client
     * authentication token with the SHA1 of the scramble concatenated with the
     * value of mysql.user.password.
     *
     * Once we have the SHA1 of the original password,  we can create the SHA1
     * of this hash and compare the value with the one stored in the backend
     * database. If the values match, the user has sent the right password.
     */

    // First, calculate the SHA1(scramble + stored pw hash).
    uint8_t step1[SHA_DIGEST_LENGTH];
    gw_sha1_2_str(session->scramble, sizeof(session->scramble), stored_pw_hash2_bin, stored_hash_len, step1);

    // Next, extract SHA1(password) by XOR'ing the auth token sent by client with the previous step result.
    uint8_t step2[SHA_DIGEST_LENGTH] = {};
    mxs::bin_bin_xor(auth_token.data(), step1, auth_token.size(), step2);

    // SHA1(password) needs to be copied to the shared data structure as it is required during
    // backend authentication. */
    session->backend_token.assign(step2, step2 + SHA_DIGEST_LENGTH);

    // Finally, calculate the SHA1(SHA1(password). */
    uint8_t final_step[SHA_DIGEST_LENGTH];
    gw_sha1_str(step2, SHA_DIGEST_LENGTH, final_step);

    // If the two values match, the client has sent the correct password.
    bool match = (memcmp(final_step, stored_pw_hash2_bin, stored_hash_len) == 0);
    AuthRes rval;
    rval.status = match ? AuthRes::Status::SUCCESS : AuthRes::Status::FAIL_WRONG_PW;
    if (!match && m_log_pw_mismatch)
    {
        // Convert the SHA1(SHA1(password)) from client to hex before printing.
        char received_pw[2 * SHA_DIGEST_LENGTH + 1];
        mxs::bin2hex(final_step, SHA_DIGEST_LENGTH, received_pw);
        rval.msg = mxb::string_printf("Client gave wrong password. Got hash %s, expected %s",
                                      received_pw, stored_pw_hash2.c_str());
    }
    return rval;
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
            // The server scramble should be null-terminated, don't copy the null.
            if (parse_res.success && parse_res.plugin_data.size() >= MYSQL_SCRAMBLE_LEN)
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
    auto& sha_pw = m_shared_data.client_data->backend_token;
    const uint8_t* curr_passwd = sha_pw.empty() ? null_client_sha1 : sha_pw.data();
    mxs_mysql_calculate_hash(m_shared_data.scramble, curr_passwd, data + MYSQL_HEADER_LEN);
    return buffer;
}

MariaDBBackendSession::MariaDBBackendSession(mariadb::BackendAuthData& shared_data)
    : m_shared_data(shared_data)
{
}

/**
 * Get MariaDBAuth module info
 *
 * @return The module object
 */
MXS_MODULE* mariadbauthenticator_info()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::AUTHENTICATOR,
        mxs::ModuleStatus::GA,
        MXS_AUTHENTICATOR_VERSION,
        "Standard MySQL/MariaDB authentication (mysql_native_password)",
        "V2.1.0",
        MXS_NO_MODULE_CAPABILITIES,         // Authenticator capabilities are in the instance object
        &mxs::AuthenticatorApiGenerator<MariaDBAuthenticatorModule>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
