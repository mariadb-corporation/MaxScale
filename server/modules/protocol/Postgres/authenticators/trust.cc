/*
 * Copyright (c) 2023 MariaDB plc
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

#include "trust.hh"

GWBUF TrustClientAuth::authentication_request()
{
    return GWBUF();
}

PgClientAuthenticator::ExchRes TrustClientAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    // Should not get here.
    mxb_assert(!true);
    return ExchRes();
}

PgClientAuthenticator::AuthRes
TrustClientAuth::authenticate(PgProtocolData& session)
{
    return AuthRes {AuthRes::Status::SUCCESS};
}

std::optional<GWBUF> TrustBackendAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    // Getting here means backend does not consider us trusted.
    return {};
}

std::unique_ptr<PgClientAuthenticator> TrustAuthModule::create_client_authenticator() const
{
    return std::make_unique<TrustClientAuth>();
}

std::unique_ptr<PgBackendAuthenticator> TrustAuthModule::create_backend_authenticator() const
{
    return std::make_unique<TrustBackendAuth>();
}

std::string TrustAuthModule::name() const
{
    return "trust";
}
