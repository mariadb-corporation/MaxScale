/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXB_MODULE_NAME "Ed25519Auth"

#include "ed25519_auth.hh"
#include <maxbase/filesystem.hh>
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/utils.hh>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <fstream>
#include "ref10/exports/crypto_sign.h"
#include "ref10/exports/api.h"

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;
using mariadb::ByteVec;
using std::string;

namespace
{
const std::unordered_set<std::string> plugins = {"ed25519"};    // Name of plugin in mysql.user

std::tuple<unsigned long, string> get_openssl_error()
{
    auto eno = ERR_get_error();
    size_t len = 256;
    char buf[len];
    ERR_error_string_n(eno, buf, len);
    return {eno, buf};
}
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
const string OPT_RSA_PUBKEY = "ed_rsa_pubkey_path";
const string OPT_RSA_PRIVKEY = "ed_rsa_privkey_path";
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

    ByteVec rsa_privkey;
    ByteVec rsa_pubkey;

    bool rsa_privkey_found = options->contains(SHA2::OPT_RSA_PRIVKEY);
    bool rsa_pubkey_found = options->contains(SHA2::OPT_RSA_PUBKEY);
    if (rsa_privkey_found != rsa_pubkey_found)
    {
        const string& found = rsa_privkey_found ? SHA2::OPT_RSA_PRIVKEY : SHA2::OPT_RSA_PUBKEY;
        const string& missing = rsa_privkey_found ? SHA2::OPT_RSA_PUBKEY : SHA2::OPT_RSA_PRIVKEY;
        MXB_ERROR("'%s' is set in authenticator options, '%s' must also be set.",
                  found.c_str(), missing.c_str());
        error = true;
    }
    else if (rsa_privkey_found)
    {
        auto load_keydata = [](const string& path, ByteVec& out) {
            bool success = false;
            auto [data, err] = mxb::load_file<ByteVec>(path);
            if (err.empty() && !data.empty())
            {
                out = std::move(data);
                success = true;
            }
            else if (err.empty())
            {
                MXB_ERROR("Couldn't read any data from RSA keyfile '%s'.", path.c_str());
            }
            else
            {
                MXB_ERROR("Failed to open RSA keyfile. %s", err.c_str());
            }
            return success;
        };

        string privkey_path = options->get_string(SHA2::OPT_RSA_PRIVKEY);
        options->remove(SHA2::OPT_RSA_PRIVKEY);
        string pubkey_path = options->get_string(SHA2::OPT_RSA_PUBKEY);
        options->remove(SHA2::OPT_RSA_PUBKEY);

        if (load_keydata(privkey_path, rsa_privkey) && load_keydata(pubkey_path, rsa_pubkey))
        {
            // Check that the data can actually be used by OpenSSL.
            ERR_clear_error();
            const char errmsg_fmt[] = "Could not read RSA key from '%s'. OpenSSL %s failed. Error %lu: %s";
            BIO* bio = BIO_new_mem_buf(rsa_privkey.data(), rsa_privkey.size());
            EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
            if (key)
            {
                EVP_PKEY_free(key);
            }
            else
            {
                auto [eno, msg] = get_openssl_error();
                MXB_ERROR("Could not read RSA key from '%s'. OpenSSL PEM_read_bio_PrivateKey() failed. "
                          "Error %lu: %s", privkey_path.c_str(), eno, msg.c_str());
                error = true;
            }
            BIO_free(bio);

            ERR_clear_error();
            bio = BIO_new_mem_buf(rsa_pubkey.data(), rsa_pubkey.size());
            key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            if (key)
            {
                EVP_PKEY_free(key);
            }
            else
            {
                auto [eno, msg] = get_openssl_error();
                MXB_ERROR("Could not read RSA key from '%s'. OpenSSL PEM_read_bio_PUBKEY() failed. "
                          "Error %lu: %s", pubkey_path.c_str(), eno, msg.c_str());
                error = true;
            }
            BIO_free(bio);
        }
        else
        {
            error = true;
        }
    }
    return error ? nullptr : new Ed25519AuthenticatorModule(mode, std::move(rsa_privkey),
                                                            std::move(rsa_pubkey));
}

Ed25519AuthenticatorModule::Ed25519AuthenticatorModule(Ed25519Authenticator::Mode mode,
                                                       ByteVec&& rsa_privkey, ByteVec&& rsa_pubkey)
    : m_mode(mode)
    , m_rsa_privkey(std::move(rsa_privkey))
    , m_rsa_pubkey(std::move(rsa_pubkey))
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

mariadb::SClientAuth Ed25519AuthenticatorModule::create_client_authenticator(MariaDBClientConnection& client)
{
    return std::make_unique<Ed25519ClientAuthenticator>(m_mode, m_rsa_privkey, m_rsa_pubkey);
}

mariadb::SBackendAuth
Ed25519AuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return std::make_unique<Ed25519BackendAuthenticator>(auth_data);
}

Ed25519ClientAuthenticator::Ed25519ClientAuthenticator(Ed25519Authenticator::Mode mode,
                                                       const ByteVec& rsa_privkey, const ByteVec& rsa_pubkey)
    : m_mode(mode)
    , m_rsa_privkey(rsa_privkey)
    , m_rsa_pubkey(rsa_pubkey)
{
}

mariadb::ClientAuthenticator::ExchRes
Ed25519ClientAuthenticator::exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    ExchRes rval;

    const size_t pubkey_req_buflen = MYSQL_HEADER_LEN + 1;
    const size_t rsa_pw_buflen = MYSQL_HEADER_LEN + 256;

    switch (m_state)
    {
    case State::INIT:
        if (m_mode == Ed25519Authenticator::Mode::SHA256)
        {
            rval.packet = sha_create_auth_change_packet(session->scramble);
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
        if (ed_read_signature(buffer, session, auth_data.client_token))
        {
            rval.status = ExchRes::Status::READY;
            m_state = State::ED_CHECK_SIGNATURE;
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
            // Client should have sent the password.
            sha_read_client_pw(buffer);
            rval.status = ExchRes::Status::READY;
            m_state = State::SHA_CHECK_PW;
        }
        else if (m_rsa_privkey.empty())
        {
            MXB_ERROR("Cannot authenticate client %s with %s via an unencrypted connection. Either "
                      "configure the listener for SSL or configure RSA keypair with authenticator settings "
                      "'%s' and '%s'.", session->user_and_host().c_str(), SHA2::CLIENT_PLUGIN_NAME,
                      SHA2::OPT_RSA_PRIVKEY.c_str(), SHA2::OPT_RSA_PUBKEY.c_str());
        }
        else if (buffer.length() == pubkey_req_buflen)
        {
            // Looks like client is asking for public key.
            if (buffer[MYSQL_HEADER_LEN] == 2)
            {
                rval.packet = sha_create_pubkey_packet();
                rval.status = ExchRes::Status::INCOMPLETE;
                m_state = State::SHA_PUBKEY_SENT;
            }
            else
            {
                MXB_ERROR("Client %s sent an invalid public key request packet.",
                          session->user_and_host().c_str());
            }
        }
        else if (buffer.length() == rsa_pw_buflen)
        {
            // Looks like an RSA-encrypted pw. The client must have known the pubkey in advance.
            if (sha_decrypt_rsa_pw(buffer, session))
            {
                rval.status = ExchRes::Status::READY;
                m_state = State::SHA_CHECK_PW;
            }
        }
        else
        {
            MXB_ERROR("Unrecognized packet from client %s. Expected length %zu or %zu, got %zu.",
                      session->user_and_host().c_str(), pubkey_req_buflen, rsa_pw_buflen, buffer.length());
        }
        break;

    case State::SHA_PUBKEY_SENT:
        if (buffer.length() == rsa_pw_buflen)
        {
            if (sha_decrypt_rsa_pw(buffer, session))
            {
                rval.status = ExchRes::Status::READY;
                m_state = State::SHA_CHECK_PW;
            }
        }
        else
        {
            MXB_ERROR("Unrecognized packet from client %s. Expected length %zu (encrypted password), "
                      "got %zu.", session->user_and_host().c_str(), rsa_pw_buflen, buffer.length());
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
        rval = mariadb::create_error_packet(0, 1105, "HY000", "Unknown error");
    }
    return rval;
}

bool Ed25519ClientAuthenticator::ed_read_signature(const GWBUF& buffer, MYSQL_session* session,
                                                   mariadb::AuthByteVec& out)
{
    // Buffer is known to be complete.
    bool rval = false;
    size_t plen = mariadb::get_header(buffer.data()).pl_length;
    if (plen == ED::SIGNATURE_LEN)
    {
        out.resize(ED::SIGNATURE_LEN);
        buffer.copy_data(MYSQL_HEADER_LEN, ED::SIGNATURE_LEN, out.data());
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

GWBUF Ed25519ClientAuthenticator::sha_create_auth_change_packet(const uint8_t* scramble)
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
    ptr = mariadb::copy_bytes(ptr, scramble, MYSQL_SCRAMBLE_LEN);
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

GWBUF Ed25519ClientAuthenticator::sha_create_pubkey_packet() const
{
    /**
     * Public key:
     * 4 bytes     - Header
     * 1           - Fixed
     * byte<EOF>   - Public key data
     */
    size_t plen = 1 + m_rsa_pubkey.size();
    size_t total_len = MYSQL_HEADER_LEN + plen;
    GWBUF rval;
    auto [ptr, _] = rval.prepare_to_write(total_len);
    ptr = mariadb::write_header(ptr, plen, 0);
    *ptr++ = 1;
    mariadb::copy_bytes(ptr, m_rsa_pubkey.data(), m_rsa_pubkey.size());
    rval.write_complete(total_len);
    return rval;
}

bool Ed25519ClientAuthenticator::sha_decrypt_rsa_pw(const GWBUF& buffer, MYSQL_session* session)
{
    const char* failed_func = nullptr;
    unsigned long open_ssl_eno = 0;
    string openssl_errmsg;

    bool rval = false;
    ERR_clear_error();
    BIO* bio = BIO_new_mem_buf(m_rsa_privkey.data(), m_rsa_privkey.size());
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    if (key)
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(key, nullptr);
        if (ctx)
        {
            if (EVP_PKEY_decrypt_init(ctx) == 1)
            {
                if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) >= 1)
                {
                    const uint8_t* in = buffer.data() + MYSQL_HEADER_LEN;
                    size_t in_len = buffer.length() - MYSQL_HEADER_LEN;
                    size_t decrypted_len = EVP_PKEY_size(key);
                    uint8_t decrypted[decrypted_len];
                    if (EVP_PKEY_decrypt(ctx, decrypted, &decrypted_len, in, in_len) == 1)
                    {
                        // The pw was XORred with the original scramble before encryption, so XOR again.
                        uint8_t unscrambled[decrypted_len];
                        const uint8_t* scramble = session->scramble;
                        for (size_t i = 0; i < decrypted_len; i++)
                        {
                            unscrambled[i] = decrypted[i] ^ scramble[i % MYSQL_SCRAMBLE_LEN];
                        }
                        // Decrypted data includes the 0-byte.
                        if (decrypted_len > 1)
                        {
                            m_client_passwd.assign(unscrambled, unscrambled + decrypted_len - 1);
                        }
                        rval = true;
                    }
                    else
                    {
                        failed_func = "EVP_PKEY_decrypt()";
                    }
                }
                else
                {
                    failed_func = "EVP_PKEY_CTX_set_rsa_padding()";
                }
            }
            else
            {
                failed_func = "EVP_PKEY_decrypt_init()";
            }

            if (failed_func)
            {
                std::tie(open_ssl_eno, openssl_errmsg) = get_openssl_error();
            }
            EVP_PKEY_CTX_free(ctx);
        }
        else
        {
            failed_func = "EVP_PKEY_CTX_new()";
            std::tie(open_ssl_eno, openssl_errmsg) = get_openssl_error();
        }
        EVP_PKEY_free(key);
    }
    else
    {
        failed_func = "PEM_read_bio_PrivateKey()";
        std::tie(open_ssl_eno, openssl_errmsg) = get_openssl_error();
    }
    BIO_free(bio);

    if (!rval)
    {
        MXB_ERROR("OpenSSL %s failed for client %s. Error %lu: %s", failed_func,
                  session->user_and_host().c_str(), open_ssl_eno, openssl_errmsg.c_str());
    }

    return rval;
}

AuthRes Ed25519ClientAuthenticator::sha_check_cleartext_pw(AuthenticationData& auth_data)
{
    // Need to check the cleartext password against the public key. Generate a
    // public key from the password (same as during CREATE USER ...) and compare to the public
    // key entry.
    unsigned char pk[ED::PUBKEY_LEN];
    crypto_sign_keypair(pk, m_client_passwd.data(), m_client_passwd.size());
    string pk64 = mxs::to_base64(pk, sizeof(pk));
    // Server doesn't pad with '=', OpenSSL base64 encoder does.
    mxb_assert(pk64.back() == '=');
    pk64.pop_back();
    AuthRes res;
    if (pk64 == auth_data.user_entry.entry.auth_string)
    {
        // Password is correct, copy to backend token so that MaxScale can impersonate the client.
        auth_data.backend_token = std::move(m_client_passwd);
        res.status = AuthRes::Status::SUCCESS;
    }
    else
    {
        res.status = AuthRes::Status::FAIL_WRONG_PW;
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
