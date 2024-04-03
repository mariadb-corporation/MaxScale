/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

class MariaDBAuthenticatorModule : public mariadb::AuthenticatorModule
{
public:
    static MariaDBAuthenticatorModule* create(mxs::ConfigParameters* options);
    explicit MariaDBAuthenticatorModule(bool log_pw_mismatch, bool passthrough_mode);
    ~MariaDBAuthenticatorModule() override = default;

    mariadb::SClientAuth  create_client_authenticator(MariaDBClientConnection& client) override;
    mariadb::SBackendAuth create_backend_authenticator(mariadb::BackendAuthData& auth_data) override;
    mariadb::AuthByteVec  generate_token(std::string_view password) override;

    uint64_t    capabilities() const override;
    std::string supported_protocol() const override;
    std::string name() const override;

    const std::unordered_set<std::string>& supported_plugins() const override;

private:
    bool m_log_pw_mismatch {false};     /**< Print pw hash when authentication fails */
    bool m_passthrough_mode {false};
};

class MariaDBClientAuthenticator : public mariadb::ClientAuthenticator
{
public:
    MariaDBClientAuthenticator(bool log_pw_mismatch, bool passthrough_mode);
    ~MariaDBClientAuthenticator() override = default;

    ExchRes exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data) override;
    AuthRes authenticate(MYSQL_session* session, AuthenticationData& auth_data) override;

private:
    enum class State
    {
        INIT,
        AUTHSWITCH_SENT,
        CHECK_TOKEN
    };

    State m_state {State::INIT};
    bool  m_log_pw_mismatch {false};/**< Print pw hash when authentication fails */
    bool  m_passthrough_mode {false};
};

/** Structure representing the authentication state */
class MariaDBBackendSession : public mariadb::BackendAuthenticator
{
public:
    MariaDBBackendSession(mariadb::BackendAuthData& shared_data);
    ~MariaDBBackendSession() = default;

    AuthRes exchange(GWBUF&& input) override;

private:
    GWBUF gen_native_auth_response(const mariadb::ByteVec& sha_pw, uint8_t seqno);
    GWBUF gen_clearpw_auth_response(const mariadb::ByteVec& pw, uint8_t seqno);

    /** Authentication states */
    enum class State
    {
        EXPECT_AUTHSWITCH,      /**< Waiting for authentication switch packet */
        PW_SENT,                /**< Hashed password has been sent to backend */
        ERROR                   /**< Authentication failed */
    };

    mariadb::BackendAuthData& m_shared_data;

    State m_state {State::EXPECT_AUTHSWITCH};   /**< Authentication state */
};
