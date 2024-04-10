/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

namespace Ed25519Authenticator
{
constexpr size_t ED_SCRAMBLE_LEN = 32;
enum class Mode {ED, SHA256};
}

class Ed25519AuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static Ed25519AuthenticatorModule* create(mxs::ConfigParameters* options);

    mariadb::SClientAuth  create_client_authenticator(MariaDBClientConnection& client) override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;

    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;

private:
    explicit Ed25519AuthenticatorModule(Ed25519Authenticator::Mode mode, mariadb::ByteVec&& rsa_privkey,
                                        mariadb::ByteVec&& rsa_pubkey);

    const Ed25519Authenticator::Mode m_mode {Ed25519Authenticator::Mode::ED};

    mariadb::ByteVec m_rsa_privkey;
    mariadb::ByteVec m_rsa_pubkey;
};

class Ed25519ClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    explicit Ed25519ClientAuthenticator(Ed25519Authenticator::Mode, const mariadb::ByteVec& rsa_privkey,
                                        const mariadb::ByteVec& rsa_pubkey);

    ExchRes exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    enum class State {INIT, ED_AUTHSWITCH_SENT, ED_CHECK_SIGNATURE, SHA_AUTHSWITCH_SENT, SHA_PW_REQUESTED,
                      SHA_PUBKEY_SENT, SHA_CHECK_PW, DONE};
    State m_state {State::INIT};

    const Ed25519Authenticator::Mode m_mode {Ed25519Authenticator::Mode::ED};
    const mariadb::ByteVec&          m_rsa_privkey;
    const mariadb::ByteVec&          m_rsa_pubkey;

    uint8_t m_scramble[Ed25519Authenticator::ED_SCRAMBLE_LEN];      /**< ed25519 scramble sent to client */

    mariadb::ByteVec m_client_passwd;   /**< Cleartext password received from client */

    GWBUF   ed_create_auth_change_packet();
    bool    ed_read_signature(const GWBUF& buffer, MYSQL_session* session, mariadb::AuthByteVec& out);
    AuthRes ed_check_signature(const AuthenticationData& auth_data, const uint8_t* signature,
                               const uint8_t* message, size_t message_len);

    GWBUF   sha_create_auth_change_packet(const uint8_t* scramble);
    bool    sha_read_client_token(const GWBUF& buffer);
    GWBUF   sha_create_request_encrypted_pw_packet() const;
    void    sha_read_client_pw(const GWBUF& buffer);
    GWBUF   sha_create_pubkey_packet() const;
    bool    sha_decrypt_rsa_pw(const GWBUF& buffer, MYSQL_session* session);
    AuthRes sha_check_cleartext_pw(AuthenticationData& auth_data);
};

class Ed25519BackendAuthenticator : public mariadb::BackendAuthenticator
{
public:
    explicit Ed25519BackendAuthenticator(mariadb::BackendAuthData& shared_data);
    AuthRes exchange(GWBUF&& input) override;

private:
    enum class State {EXPECT_AUTHSWITCH, SIGNATURE_SENT, ERROR};
    State m_state {State::EXPECT_AUTHSWITCH};

    const mariadb::BackendAuthData& m_shared_data;  /**< Data shared with backend connection */
    uint8_t                         m_sequence {0}; /**< Next packet sequence number */

    GWBUF generate_auth_token_packet(const mariadb::ByteVec& scramble) const;
};
