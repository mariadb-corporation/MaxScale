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
const size_t AUTH_SWITCH_PLEN = 1 + sizeof(CLIENT_PLUGIN_NAME) + Ed25519AuthenticatorModule::scramble_len;
const size_t AUTH_SWITCH_BUFLEN = MYSQL_HEADER_LEN + AUTH_SWITCH_PLEN;
const size_t SCRAMBLE_LEN = Ed25519AuthenticatorModule::scramble_len;
}

Ed25519AuthenticatorModule* Ed25519AuthenticatorModule::create(mxs::ConfigParameters* options)
{
    return new Ed25519AuthenticatorModule();
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
    return std::make_unique<Ed25519ClientAuthenticator>();
}

mariadb::SBackendAuth
Ed25519AuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return std::make_unique<Ed25519BackendAuthenticator>(auth_data);
}

mariadb::ClientAuthenticator::ExchRes
Ed25519ClientAuthenticator::exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    using ExchRes = mariadb::ClientAuthenticator::ExchRes;
    ExchRes rval;

    switch (m_state)
    {
    case State::INIT:
        rval.packet = create_auth_change_packet();
        if (!rval.packet.empty())
        {
            rval.status = ExchRes::Status::INCOMPLETE;
            m_state = State::AUTHSWITCH_SENT;
        }
        break;

    case State::AUTHSWITCH_SENT:
        // Client should have responded with signed scramble.
        if (read_signature(session, buffer))
        {
            rval.status = ExchRes::Status::READY;
            m_state = State::CHECK_SIGNATURE;
        }
        else
        {
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
    mxb_assert(m_state == State::CHECK_SIGNATURE);
    AuthRes rval;
    if (auth_data.client_token.size() == ED::SIGNATURE_LEN)
    {
        // Apparently the signature check function wants the signature and scramble in the same buffer.
        const size_t total_mlen = ED::SIGNATURE_LEN + ED::SCRAMBLE_LEN;
        uint8_t sign_and_scramble[total_mlen];
        auto ptr = mempcpy(sign_and_scramble, auth_data.client_token.data(), ED::SIGNATURE_LEN);
        memcpy(ptr, m_scramble, ED::SCRAMBLE_LEN);
        // Public keys are 32 bytes -> 44 chars when base64-encoded. The server stores the encoding in 43
        // bytes in the mysql.user-table, so add the last '=' before decoding.
        // TODO: Could save the decoding result so that it's only done once per user.
        // TODO: Could also optimize out the allocations.
        string encoding = auth_data.user_entry.entry.auth_string;
        encoding.push_back('=');
        auto pubkey_bytes = mxs::from_base64(encoding);
        if (pubkey_bytes.size() == ED::PUBKEY_LEN)
        {
            uint8_t message[total_mlen];
            unsigned long long int decr_mlen = 0;
            if (crypto_sign_open(message, &decr_mlen, sign_and_scramble, total_mlen,
                                 pubkey_bytes.data()) == 0)
            {
                rval.status = AuthRes::Status::SUCCESS;
            }
            else
            {
                rval.status = AuthRes::Status::FAIL_WRONG_PW;
            }
        }
        else
        {
            MXB_ERROR("Public key of user is wrong length. Expected %zu bytes, got %zu.",
                      ED::PUBKEY_LEN, pubkey_bytes.size());
        }
    }
    else
    {
        MXB_ERROR("Client sent wrong signature size. Expected %zu bytes, got %zu.",
                  ED::SIGNATURE_LEN, auth_data.client_token.size());
    }

    m_state = State::DONE;
    return rval;
}

GWBUF Ed25519ClientAuthenticator::create_auth_change_packet()
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

bool Ed25519ClientAuthenticator::read_signature(MYSQL_session* session, const GWBUF& buffer)
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
    return rval;
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
