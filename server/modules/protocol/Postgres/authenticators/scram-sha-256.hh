/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
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
    GWBUF create_authentication_sasl_continue(const GWBUF& buffer, std::string& client_first_message,
                                              std::string& server_first_message);
    GWBUF create_authentication_sasl_final(const GWBUF& buffer,
                                           std::string_view client_first_message_bare,
                                           std::string_view server_first_message,
                                           Digest& client_key);

    bool verify_authentication_sasl_final(const GWBUF& buffer,
                                          std::string_view client_first_message_bare,
                                          std::string_view server_first_message,
                                          std::string_view client_final_message_without_proof);
};

class ScramBackendAuth : public PgBackendAuthenticator
{
public:
    GWBUF exchange(GWBUF&& input, PgProtocolData& session) override;

private:
    GWBUF create_sasl_initial_response(std::string& client_first_message_bare);
    GWBUF create_sasl_response(const GWBUF& buffer,
                               std::string_view client_first_message_bare,
                               std::string& server_first_message,
                               std::string& client_final_message_without_proof,
                               const Digest& client_key);
};

class ScramAuthModule : public PgAuthenticatorModule
{
public:
    virtual std::unique_ptr<PgClientAuthenticator>  create_client_authenticator() const override;
    virtual std::unique_ptr<PgBackendAuthenticator> create_backend_authenticator() const override;
    virtual std::string                             name() const override;
};
