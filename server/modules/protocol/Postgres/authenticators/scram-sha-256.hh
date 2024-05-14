/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "../pgauthenticatormodule.hh"
#include "common.hh"

class ScramClientAuth : public PgClientAuthenticator
{
public:
    GWBUF   authentication_request() override;
    ExchRes exchange(GWBUF&& input, PgProtocolData& session) override;
    AuthRes authenticate(PgProtocolData& session) override;

private:
    enum class State {INIT, INIT_CONT, SALT_SENT, READY};
    State m_state {State::INIT};

    std::string m_client_first_message_bare;
    std::string m_server_first_message;

    std::string m_client_nonce;
    std::string m_server_nonce;

    char m_cbind_flag {0};

    Digest m_stored_key;
    Digest m_server_key;

    struct InitialResponse
    {
        std::string_view mech;
        std::string_view client_data;
    };
    std::optional<InitialResponse> read_sasl_initial_response(const GWBUF& input);
    std::string_view               read_sasl_response(const GWBUF& input);

    GWBUF                   sasl_handle_client_first_msg(std::string_view sasl_data, PgProtocolData& session);
    std::tuple<bool, GWBUF> sasl_handle_client_proof(std::string_view sasl_data, PgProtocolData& session);

    GWBUF create_sasl_continue(std::string_view response);
    bool  nonces_match(std::string_view client_final_nonce);
    GWBUF sasl_verify_proof(const Digest& proof, std::string_view client_final_message_without_proof,
                            PgProtocolData& session);
};

class ScramBackendAuth : public PgBackendAuthenticator
{
public:
    std::optional<GWBUF> exchange(GWBUF&& input, PgProtocolData& session) override;

private:
    enum class State {AUTH_REQ, SALT, FINAL, DONE};
    State m_state {State::AUTH_REQ};

    std::string m_client_first_message_bare;
    std::string m_auth_message;
    std::string m_client_nonce;

    std::optional<GWBUF> handle_auth_request(const GWBUF& input);
    std::optional<GWBUF> handle_sasl_continue(const GWBUF& input, PgProtocolData& session);
    bool                 check_sasl_final(const GWBUF& input, PgProtocolData& session);
    GWBUF                create_sasl_initial_response();
    GWBUF                create_scram_proof(std::string_view full_nonce,
                                            std::string_view server_first_message, PgProtocolData& session);

    std::tuple<int, std::string_view> read_scram_data(const GWBUF& input);
};

class ScramAuthModule : public PgAuthenticatorModule
{
public:
    virtual std::unique_ptr<PgClientAuthenticator>  create_client_authenticator() const override;
    virtual std::unique_ptr<PgBackendAuthenticator> create_backend_authenticator() const override;
    virtual std::string                             name() const override;
};
