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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

class Ed25519AuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static Ed25519AuthenticatorModule* create(mxs::ConfigParameters* options);

    mariadb::SClientAuth  create_client_authenticator() override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;

    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;
};

class Ed25519ClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    ExchRes exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    enum class State {INIT, AUTHSWITCH_SENT, CHECK_SIGNATURE, DONE};
    State m_state {State::INIT};

    GWBUF create_auth_change_packet();
    bool  read_signature(MYSQL_session* session, const GWBUF& buffer);
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
};
