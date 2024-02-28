/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXB_MODULE_NAME MXS_MARIADBAUTH_AUTHENTICATOR_NAME

#include "mysql_auth.hh"
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/built_in_modules.hh>
#include <maxscale/config_common.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/utils.hh>
#include <openssl/sha.h>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;
using std::string;
using TokenType = mariadb::AuthenticationData::TokenType;

namespace
{
const string clearpw_plugin = "mysql_clear_password";
const string native_plugin = DEFAULT_MYSQL_AUTH_PLUGIN;
// Support the empty plugin as well, as that means default.
const std::unordered_set<std::string> plugins = {native_plugin, "caching_sha2_password", clearpw_plugin, ""};
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
    bool passthrough_mode = false;
    const std::string opt_log_mismatch = "log_password_mismatch";
    if (options->contains(opt_log_mismatch))
    {
        log_pw_mismatch = options->get_bool(opt_log_mismatch);
        options->remove(opt_log_mismatch);
    }
    const std::string opt_passthrough = "clear_pw_passthrough";
    if (options->contains(opt_passthrough))
    {
        passthrough_mode = options->get_bool(opt_passthrough);
        options->remove(opt_passthrough);
    }

    return new MariaDBAuthenticatorModule(log_pw_mismatch, passthrough_mode);
}

MariaDBAuthenticatorModule::MariaDBAuthenticatorModule(bool log_pw_mismatch, bool passthrough_mode)
    : m_log_pw_mismatch(log_pw_mismatch)
    , m_passthrough_mode(passthrough_mode)
{
}

uint64_t MariaDBAuthenticatorModule::capabilities() const
{
    return m_passthrough_mode ? CAP_PASSTHROUGH : 0;
}

std::string MariaDBAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::string MariaDBAuthenticatorModule::name() const
{
    return MXB_MODULE_NAME;
}

const std::unordered_set<std::string>& MariaDBAuthenticatorModule::supported_plugins() const
{
    return plugins;
}

mariadb::SClientAuth MariaDBAuthenticatorModule::create_client_authenticator(MariaDBClientConnection& client)
{
    return std::make_unique<MariaDBClientAuthenticator>(m_log_pw_mismatch, m_passthrough_mode);
}

mariadb::SBackendAuth
MariaDBAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new MariaDBBackendSession(auth_data));
}

mariadb::AuthByteVec MariaDBAuthenticatorModule::generate_token(std::string_view password)
{
    mariadb::AuthByteVec rval;
    if (!password.empty())
    {
        rval.resize(SHA_DIGEST_LENGTH);
        gw_sha1_str((const uint8_t*)password.data(), password.length(), rval.data());
    }
    return rval;
}

namespace
{
// Helper function for generating an AuthSwitchRequest packet.
GWBUF gen_auth_switch_request_packet(TokenType token_type, const uint8_t* scramble)
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * string[NUL] - Scramble (if native plugin)
     */
    bool native = token_type == TokenType::PW_HASH;
    const auto& plugin = native ? native_plugin : clearpw_plugin;

    /* When sending an AuthSwitchRequest for "mysql_native_password", the scramble data needs an extra
     * byte in the end. */
    int plen = 1 + plugin.length() + 1 + (native ? (MYSQL_SCRAMBLE_LEN + 1) : 0);
    int buflen = MYSQL_HEADER_LEN + plen;

    GWBUF buffer(buflen);
    auto bufdata = mariadb::write_header(buffer.data(), plen, 0);       // protocol will set sequence
    *bufdata++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    bufdata = mariadb::copy_chars(bufdata, plugin.c_str(), plugin.length() + 1);
    if (native)
    {
        bufdata = mariadb::copy_bytes(bufdata, scramble, MYSQL_SCRAMBLE_LEN);
        *bufdata++ = '\0';
    }
    mxb_assert(bufdata - buffer.data() == buflen);
    return buffer;
}
}

MariaDBClientAuthenticator::MariaDBClientAuthenticator(bool log_pw_mismatch, bool passthrough_mode)
    : m_log_pw_mismatch(log_pw_mismatch)
    , m_passthrough_mode(passthrough_mode)
{
}

mariadb::ClientAuthenticator::ExchRes
MariaDBClientAuthenticator::exchange(GWBUF&& buf, MYSQL_session* session, AuthenticationData& auth_data)
{
    ExchRes rval;

    auto complete_exchange = [this, &auth_data, &rval]() {
        auth_data.client_token_type = m_passthrough_mode ? TokenType::CLEARPW : TokenType::PW_HASH;
        if (m_passthrough_mode)
        {
            // Best to calculate sha1(pw) even before we know what backend will ask for, so that
            // protocol code can send the hash in the handshake response.
            // TODO: add protocol-level support for detecting/sending authenticator-aware handshake resp.
            std::string_view pw;
            const auto& cli_token = auth_data.client_token;
            if (!cli_token.empty())
            {
                // According to protocol, clear password should always be 0-terminated.
                pw = std::string_view((const char*)cli_token.data(), cli_token.size() - 1);
            }
            auth_data.backend_token = auth_data.client_auth_module->generate_token(pw);
        }
        rval.status = ExchRes::Status::READY;
        m_state = State::CHECK_TOKEN;
    };

    switch (m_state)
    {
    case State::INIT:
        {
            // First, check if client is already using correct plugin. The handshake response was parsed in
            // protocol code. Some old clients may send an empty plugin name, interpret it as
            // "mysql_native_password".
            // Passthrough-mode requires cleartext password from client since we may not have the double
            // hash available.
            bool correct_plugin = m_passthrough_mode ? (auth_data.plugin == clearpw_plugin) :
                (auth_data.plugin == native_plugin || auth_data.plugin.empty());
            if (correct_plugin)
            {
                // Correct plugin, token should have been read by protocol code.
                complete_exchange();
            }
            else
            {
                // Client is attempting to use wrong authenticator, send switch request packet.
                auto req_token = m_passthrough_mode ? TokenType::CLEARPW : TokenType::PW_HASH;
                rval.packet = gen_auth_switch_request_packet(req_token, session->scramble);
                rval.status = ExchRes::Status::INCOMPLETE;
                m_state = State::AUTHSWITCH_SENT;
            }
        }
        break;

    case State::AUTHSWITCH_SENT:
        {
            // Client is replying to an AuthSwitch request. The packet should contain
            // the authentication token or be empty if trying to log in without pw.
            auth_data.client_token.assign(buf.data() + MYSQL_HEADER_LEN, buf.end());
            complete_exchange();
        }
        break;

    default:
        mxb_assert(!true);
        break;
    }

    return rval;
}

AuthRes MariaDBClientAuthenticator::authenticate(MYSQL_session* session, AuthenticationData& auth_data)
{
    mxb_assert(m_state == State::CHECK_TOKEN && !m_passthrough_mode);
    const auto& stored_pw_hash2 = auth_data.user_entry.entry.password;
    const auto& auth_token = auth_data.client_token;        // Binary-form token sent by client.

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

    mxb_assert(auth_data.client_token_type == TokenType::PW_HASH);
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
    auth_data.backend_token.assign(step2, step2 + SHA_DIGEST_LENGTH);

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
MariaDBBackendSession::exchange(GWBUF&& input)
{
    AuthRes rval;
    // Protocol catches Ok and Error-packets, so the only expected packet here is AuthSwitch-request.
    switch (m_state)
    {
    case State::EXPECT_AUTHSWITCH:
        {
            auto parse_res = mariadb::parse_auth_switch_request(input);
            if (parse_res.success)
            {
                auto new_seqno = MYSQL_GET_PACKET_NO(input.data()) + 1;
                const auto& auth_data = *m_shared_data.client_data->auth_data;
                bool have_clearpw = auth_data.client_token_type == TokenType::CLEARPW;

                if (parse_res.plugin_name == native_plugin)
                {
                    // The server scramble should be null-terminated, don't copy the null.
                    if (parse_res.plugin_data.size() >= MYSQL_SCRAMBLE_LEN)
                    {
                        // Looks ok. The server has sent a new scramble. Save it and generate a response.
                        memcpy(m_shared_data.scramble, parse_res.plugin_data.data(), MYSQL_SCRAMBLE_LEN);

                        // Backend token should have been calculated if password is used, regardless of
                        // passthrough-setting.
                        rval.output = gen_native_auth_response(auth_data.backend_token, new_seqno);
                        rval.success = true;
                        m_state = State::PW_SENT;
                    }
                    else
                    {
                        MXB_ERROR(MALFORMED_AUTH_SWITCH, m_shared_data.servername);
                    }
                }
                else if (parse_res.plugin_name == clearpw_plugin)
                {
                    // Can answer this if client token is cleartext pw.
                    if (have_clearpw)
                    {
                        rval.output = gen_clearpw_auth_response(auth_data.client_token, new_seqno);
                        rval.success = true;
                        m_state = State::PW_SENT;
                    }
                    else
                    {
                        MXB_ERROR(WRONG_PLUGIN_REQ, m_shared_data.servername, parse_res.plugin_name.c_str(),
                                  m_shared_data.client_data->user_and_host().c_str(), native_plugin.c_str());
                    }
                }
                else
                {
                    if (have_clearpw)
                    {
                        MXB_ERROR("'%s' asked for authentication plugin '%s' when authenticating %s. "
                                  "Only '%s' or '%s' are supported.",
                                  m_shared_data.servername, parse_res.plugin_name.c_str(),
                                  m_shared_data.client_data->user_and_host().c_str(),
                                  native_plugin.c_str(), clearpw_plugin.c_str());
                    }
                    else
                    {
                        MXB_ERROR(WRONG_PLUGIN_REQ, m_shared_data.servername, parse_res.plugin_name.c_str(),
                                  m_shared_data.client_data->user_and_host().c_str(), native_plugin.c_str());
                    }
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

GWBUF MariaDBBackendSession::gen_native_auth_response(const mariadb::ByteVec& sha_pw, uint8_t seqno)
{
    mxb_assert(sha_pw.empty() || sha_pw.size() == SHA_DIGEST_LENGTH);
    size_t pload_len = sha_pw.empty() ? 0 : SHA_DIGEST_LENGTH;
    size_t total_len = MYSQL_HEADER_LEN + pload_len;
    GWBUF rval(total_len);
    auto ptr = mariadb::write_header(rval.data(), pload_len, seqno);
    if (!sha_pw.empty())
    {
        ptr = mxs_mysql_calculate_hash(m_shared_data.scramble, sha_pw, ptr);
    }
    mxb_assert(ptr - rval.data() == (ptrdiff_t)total_len);
    return rval;
}

GWBUF MariaDBBackendSession::gen_clearpw_auth_response(const mariadb::ByteVec& pw, uint8_t seqno)
{
    size_t tok_len = pw.size();
    size_t total_len = MYSQL_HEADER_LEN + tok_len;
    GWBUF rval(total_len);
    auto ptr = mariadb::write_header(rval.data(), tok_len, seqno);
    if (tok_len > 0)
    {
        mariadb::copy_bytes(ptr, pw.data(), tok_len);
    }
    return rval;
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
        MXB_MODULE_NAME,
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
    };

    return &info;
}
