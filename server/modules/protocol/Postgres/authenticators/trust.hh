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

#include "../pgauthenticatormodule.hh"

class TrustClientAuth : public PgClientAuthenticator
{
public:
    GWBUF   authentication_request() override;
    ExchRes exchange(GWBUF&& input, PgProtocolData& session) override;
    AuthRes authenticate(PgProtocolData& session) override;
};

class TrustBackendAuth : public PgBackendAuthenticator
{
public:
    std::optional<GWBUF> exchange(GWBUF&& input, PgProtocolData& session) override;
};

class TrustAuthModule : public PgAuthenticatorModule
{
public:
    virtual std::unique_ptr<PgClientAuthenticator>  create_client_authenticator() const override;
    virtual std::unique_ptr<PgBackendAuthenticator> create_backend_authenticator() const override;
    virtual std::string                             name() const override;
};
