/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXB_MODULE_NAME "Ed25519Auth"

#include "ed25519_auth.hh"
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "ref10/exports/crypto_sign.h"
#include "ref10/exports/api.h"

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;
using std::string;

namespace
{
const std::unordered_set<std::string> plugins = {"ed25519"};    // Name of plugin in mysql.user
}

namespace ED
{
const char CLIENT_PLUGIN_NAME[] = "client_ed25519";
const size_t SIGNATURE_LEN = CRYPTO_BYTES;
const size_t PUBKEY_LEN = CRYPTO_PUBLICKEYBYTES;
const size_t AUTH_SWITCH_PLEN = 1 + sizeof(CLIENT_PLUGIN_NAME) + Ed25519Authenticator::ED_SCRAMBLE_LEN;
const size_t AUTH_SWITCH_BUFLEN = MYSQL_HEADER_LEN + AUTH_SWITCH_PLEN;
const size_t SCRAMBLE_LEN = Ed25519Authenticator::ED_SCRAMBLE_LEN;
}

namespace SHA2
{
const char CLIENT_PLUGIN_NAME[] = "caching_sha2_password";
}

Ed25519AuthenticatorModule* Ed25519AuthenticatorModule::create(mxs::ConfigParameters* options)
{
    auto mode = Ed25519Authenticator::Mode::ED;
    const string opt_mode = "ed_mode";
    const string val_ed = "ed25519";
    const string val_sha = "sha256";
    bool error = false;

    if (options->contains(opt_mode))
    {
        string mode_str = options->get_string(opt_mode);
        options->remove(opt_mode);
        if (mode_str == val_sha)
        {
            mode = Ed25519Authenticator::Mode::SHA256;
        }
        else if (mode_str != val_ed)
        {
            MXB_ERROR("Invalid value '%s' for authenticator option '%s'. Valid values are '%s' and '%s'.",
                      mode_str.c_str(), opt_mode.c_str(), val_ed.c_str(), val_sha.c_str());
            error = true;
        }
    }
    return error ? nullptr : new Ed25519AuthenticatorModule(mode);
}

Ed25519AuthenticatorModule::Ed25519AuthenticatorModule(Ed25519Authenticator::Mode mode)
    : m_mode(mode)
{
}

uint64_t Ed25519AuthenticatorModule::capabilities() const
{
    return 0;
}

std::string Ed25519AuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::string Ed25519AuthenticatorModule::name() const
{
    return MXB_MODULE_NAME;
}

const std::unordered_set<std::string>& Ed25519AuthenticatorModule::supported_plugins() const
{
    return plugins;
}

mariadb::SClientAuth Ed25519AuthenticatorModule::create_client_authenticator()
{
    return std::make_unique<Ed25519ClientAuthenticator>(m_mode);
}

mariadb::SBackendAuth
Ed25519AuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return std::make_unique<Ed25519BackendAuthenticator>(auth_data);
}

Ed25519ClientAuthenticator::Ed25519ClientAuthenticator(Ed25519Authenticator::Mode mode)
    : m_mode(mode)
{
}

mariadb::ClientAuthenticator::ExchRes
Ed25519ClientAuthenticator::exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    using ExchRes = mariadb::ClientAuthenticator::ExchRes;
    ExchRes rval;

    switch (m_state)
    {
    case State::INIT:
        if (m_mode == Ed25519Authenticator::Mode::SHA256)
        {
            rval.packet = sha_create_auth_change_packet(session);
            rval.status = ExchRes::Status::INCOMPLETE;
            m_state = State::SHA_AUTHSWITCH_SENT;
        }
        else
        {
            rval.packet = ed_create_auth_change_packet();
            if (!rval.packet.empty())
            {
                rval.status = ExchRes::Status::INCOMPLETE;
                m_state = State::ED_AUTHSWITCH_SENT;
            }
        }
        break;

    case State::ED_AUTHSWITCH_SENT:
        // Client should have responded with signed scramble.
        if (ed_read_signature(session, buffer))
        {
            rval.status = ExchRes::Status::READY;
            m_state = State::ED_CHECK_SIGNATURE;
        }
        else
        {
            m_state = State::DONE;
        }
        break;

    case State::SHA_AUTHSWITCH_SENT:
        if (sha_read_client_token(buffer))
        {
            // Signal the client to send encrypted password.
            rval.packet = sha_create_request_encrypted_pw_packet();
            rval.status = ExchRes::Status::INCOMPLETE;
            m_state = State::SHA_PW_REQUESTED;
        }
        break;

    case State::SHA_PW_REQUESTED:
        if (session->client_conn_encrypted)
        {
            sha_read_client_pw(buffer);
            rval.status = ExchRes::Status::READY;
            m_state = State::SHA_CHECK_PW;
        }
        else
        {
            // The client will either ask for the public key or send RSA-encrypted password. In any case,
            // not supported yet.
            MXB_ERROR("Cannot authenticate client %s with %s via an unencrypted connection. Configure the "
                      "listener for SSL.", session->user_and_host().c_str(), SHA2::CLIENT_PLUGIN_NAME);
            m_state = State::DONE;
        }
        break;

    default:
        mxb_assert(!true);
        break;
    }

    return rval;
}

AuthRes Ed25519ClientAuthenticator::authenticate(MYSQL_session* session, AuthenticationData& auth_data)
{
    mxb_assert(m_state == State::ED_CHECK_SIGNATURE || m_state == State::SHA_CHECK_PW);
    AuthRes rval;
    if (m_state == State::ED_CHECK_SIGNATURE)
    {
        mxb_assert(auth_data.client_token.size() == ED::SIGNATURE_LEN);
        rval = ed_check_signature(auth_data, auth_data.client_token.data(), m_scramble, ED::SCRAMBLE_LEN);
    }
    else
    {
        rval = sha_check_cleartext_pw(auth_data);
    }

    m_state = State::DONE;
    return rval;
}

GWBUF Ed25519ClientAuthenticator::ed_create_auth_change_packet()
{
    /**
     * The AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * 32 bytes    - Scramble
     */
    GWBUF rval;
    auto [ptr, _] = rval.prepare_to_write(ED::AUTH_SWITCH_BUFLEN);
    ptr = mariadb::write_header(ptr, ED::AUTH_SWITCH_PLEN, 0);
    *ptr++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    ptr = mariadb::copy_chars(ptr, ED::CLIENT_PLUGIN_NAME, sizeof(ED::CLIENT_PLUGIN_NAME));
    if (RAND_bytes(m_scramble, ED::SCRAMBLE_LEN) == 1)
    {
        mariadb::copy_bytes(ptr, m_scramble, ED::SCRAMBLE_LEN);
        rval.write_complete(ED::AUTH_SWITCH_BUFLEN);
    }
    else
    {
        // Should not really happen unless running on some weird platform.
        MXB_ERROR("OpenSSL RAND_bytes failed when generating scramble.");
    }
    return rval;
}

bool Ed25519ClientAuthenticator::ed_read_signature(MYSQL_session* session, const GWBUF& buffer)
{
    // Buffer is known to be complete.
    bool rval = false;
    size_t plen = mariadb::get_header(buffer.data()).pl_length;
    if (plen == ED::SIGNATURE_LEN)
    {
        auto& token = session->auth_data->client_token;
        token.resize(ED::SIGNATURE_LEN);
        buffer.copy_data(MYSQL_HEADER_LEN, ED::SIGNATURE_LEN, token.data());
        rval = true;
    }
    else
    {
        MXB_ERROR("Client %s sent a malformed ed25519 signature. Expected %zu bytes, got %zu.",
                  session->user_and_host().c_str(), ED::SIGNATURE_LEN, plen);
    }
    return rval;
}

AuthRes
Ed25519ClientAuthenticator::ed_check_signature(const AuthenticationData& auth_data, const uint8_t* signature,
                                               const uint8_t* message, size_t message_len)
{
    AuthRes rval;
    // The signature check function wants the signature and scramble in the same buffer.
    const size_t signed_mlen = ED::SIGNATURE_LEN + message_len;
    uint8_t sign_and_scramble[signed_mlen];
    auto ptr = mempcpy(sign_and_scramble, signature, ED::SIGNATURE_LEN);
    memcpy(ptr, message, message_len);

    // Public keys are 32 bytes -> 44 chars when base64-encoded. The server stores the encoding in 43
    // bytes in the mysql.user-table, so add the last '=' before decoding.
    // TODO: Could save the decoding result so that it's only done once per user.
    // TODO: Could also optimize out the allocations.
    string encoding = auth_data.user_entry.entry.auth_string;
    encoding.push_back('=');
    auto pubkey_bytes = mxs::from_base64(encoding);
    if (pubkey_bytes.size() == ED::PUBKEY_LEN)
    {
        uint8_t work_arr[signed_mlen];
        if (crypto_sign_open(work_arr, sign_and_scramble, signed_mlen, pubkey_bytes.data()) == 0)
        {
            rval.status = AuthRes::Status::SUCCESS;
            // Client logged in but we don't have password. Hopefully DBA has configured user_mapping_file
            // with passwords.
        }
        else
        {
            rval.status = AuthRes::Status::FAIL_WRONG_PW;
        }
    }
    else
    {
        const auto& entry = auth_data.user_entry.entry;
        MXB_ERROR("Authentication string of user account '%s'@'%s' is wrong length. Expected %zu "
                  "characters, found %zu.", entry.username.c_str(), entry.host_pattern.c_str(),
                  ED::PUBKEY_LEN, pubkey_bytes.size());
    }

    return rval;
}

GWBUF Ed25519ClientAuthenticator::sha_create_auth_change_packet(MYSQL_session* session)
{
    /**
     * AuthSwitchRequest packet:
     * 4 bytes     - Header
     * 0xfe        - Command byte
     * string[NUL] - Auth plugin name
     * 20 bytes    - Scramble
     * 1 byte      - Unused?
     */
    const size_t sha256_authswitch_plen = 1 + sizeof(SHA2::CLIENT_PLUGIN_NAME) + MYSQL_SCRAMBLE_LEN + 1;
    const size_t sha256_authswitch_buflen = MYSQL_HEADER_LEN + sha256_authswitch_plen;
    GWBUF rval;
    auto [ptr, _] = rval.prepare_to_write(sha256_authswitch_buflen);
    ptr = mariadb::write_header(ptr, sha256_authswitch_plen, 0);
    *ptr++ = MYSQL_REPLY_AUTHSWITCHREQUEST;
    ptr = mariadb::copy_chars(ptr, SHA2::CLIENT_PLUGIN_NAME, sizeof(SHA2::CLIENT_PLUGIN_NAME));
    // Use mysql_native_password scramble, as it's the same length.
    ptr = mariadb::copy_bytes(ptr, session->scramble, MYSQL_SCRAMBLE_LEN);
    *ptr = 0;
    rval.write_complete(sha256_authswitch_buflen);
    return rval;
}

bool Ed25519ClientAuthenticator::sha_read_client_token(const GWBUF& buffer)
{
    // Client should have replied with: SHA(pw) XOR SHA( SHA(SHA(pw)) | server_scramble )
    // Cannot check this without knowing pw or SHA(pw), neither of which is in mysql.user.
    // TODO: Save the hashes on successful authentications to enable this check.
    bool rval = false;
    size_t plen = mariadb::get_header(buffer.data()).pl_length;
    if (plen == SHA256_DIGEST_LENGTH)
    {
        rval = true;
    }
    return rval;
}

GWBUF Ed25519ClientAuthenticator::sha_create_request_encrypted_pw_packet() const
{
    /**
     * Password request:
     * 4 bytes     - Header
     * 1           - Bytes length
     * 4           - Pw request
     */
    size_t plen = 2;
    size_t total_len = MYSQL_HEADER_LEN + plen;
    GWBUF rval;
    auto [ptr, _] = rval.prepare_to_write(total_len);
    ptr = mariadb::write_header(ptr, plen, 0);
    // The request is given as byte<lenenc>.
    *ptr++ = 1;
    *ptr++ = 4;
    rval.write_complete(total_len);
    return rval;
}

void Ed25519ClientAuthenticator::sha_read_client_pw(const GWBUF& buffer)
{
    // The packet should contain the null-terminated cleartext pw.
    m_client_passwd.assign(buffer.data() + MYSQL_HEADER_LEN, buffer.end() - 1);
}

AuthRes Ed25519ClientAuthenticator::sha_check_cleartext_pw(mariadb::AuthenticationData& auth_data)
{
    // Need to check the cleartext password against the public ed25519 key. Sign a zero-length message
    // with the password and then check signature.
    uint8_t signature_buf[ED::SIGNATURE_LEN] {0};
    crypto_sign(signature_buf, nullptr, 0, m_client_passwd.data(), m_client_passwd.size());
    auto res = ed_check_signature(auth_data, signature_buf, nullptr, 0);
    if (res.status == AuthRes::Status::SUCCESS)
    {
        // Password is correct, copy to backend token so that MaxScale can impersonate the client.
        auth_data.backend_token = std::move(m_client_passwd);
    }
    return res;
}

Ed25519BackendAuthenticator::Ed25519BackendAuthenticator(mariadb::BackendAuthData& shared_data)
    : m_shared_data(shared_data)
{
}

mariadb::BackendAuthenticator::AuthRes Ed25519BackendAuthenticator::exchange(GWBUF&& input)
{
    const char* srv_name = m_shared_data.servername;

    auto header = mariadb::get_header(input.data());
    m_sequence = header.seq + 1;

    AuthRes rval;

    switch (m_state)
    {
    case State::EXPECT_AUTHSWITCH:
        {
            // Backend should be sending an AuthSwitchRequest with a specific length.
            bool auth_switch_valid = false;
            if (input.length() == ED::AUTH_SWITCH_BUFLEN)
            {
                auto parse_res = mariadb::parse_auth_switch_request(input);
                if (parse_res.success)
                {
                    auth_switch_valid = true;
                    if (parse_res.plugin_name != ED::CLIENT_PLUGIN_NAME)
                    {
                        MXB_ERROR(WRONG_PLUGIN_REQ, srv_name, parse_res.plugin_name.c_str(),
                                  m_shared_data.client_data->user_and_host().c_str(), ED::CLIENT_PLUGIN_NAME);
                    }
                    else if (parse_res.plugin_data.size() == ED::SCRAMBLE_LEN)
                    {
                        // Server sent the scramble, form the signature packet.
                        rval.output = generate_auth_token_packet(parse_res.plugin_data);
                        m_state = State::SIGNATURE_SENT;
                        rval.success = true;
                    }
                    else
                    {
                        MXB_ERROR("Backend server %s sent an invalid ed25519 scramble.", srv_name);
                    }
                }
            }

            if (!auth_switch_valid)
            {
                MXB_ERROR(MALFORMED_AUTH_SWITCH, srv_name);
            }
        }
        break;

    case State::SIGNATURE_SENT:
        // Server is sending more packets than expected. Error.
        MXB_ERROR("Server %s sent more packets than expected.", srv_name);
        break;

    case State::ERROR:
        // Should not get here.
        mxb_assert(!true);
        break;
    }

    if (!rval.success)
    {
        m_state = State::ERROR;
    }
    return rval;
}

GWBUF Ed25519BackendAuthenticator::generate_auth_token_packet(const mariadb::ByteVec& scramble) const
{
    // For ed25519 authentication to work, the client password must be known. Assume that manual
    // mapping is in use and the pw is in backend token data.
    const auto& backend_pw = m_shared_data.client_data->auth_data->backend_token;

    // The signature generation function requires some extra storage as it adds the message to the buffer.
    uint8_t signature_buf[ED::SIGNATURE_LEN + ED::SCRAMBLE_LEN];
    crypto_sign(signature_buf, scramble.data(), scramble.size(), backend_pw.data(), backend_pw.size());

    size_t buflen = MYSQL_HEADER_LEN + ED::SIGNATURE_LEN;
    GWBUF rval;
    auto [ptr, _] = rval.prepare_to_write(buflen);
    ptr = mariadb::write_header(ptr, ED::SIGNATURE_LEN, m_sequence);
    mariadb::copy_bytes(ptr, signature_buf, ED::SIGNATURE_LEN);
    rval.write_complete(buflen);
    return rval;
}

extern "C"
{
MXS_MODULE* mxs_get_module_object()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::AUTHENTICATOR,
        mxs::ModuleStatus::GA,
        MXS_AUTHENTICATOR_VERSION,
        "Ed25519 authenticator. Backend authentication must be mapped.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<Ed25519AuthenticatorModule>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
    };

    return &info;
}
}
