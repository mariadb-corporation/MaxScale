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

#include "password.hh"
#include "../pgprotocoldata.hh"

namespace
{
std::array<uint8_t, 9> password_request = {'R', 0, 0, 0, 8, 0, 0, 0, 3};
}

GWBUF PasswordClientAuth::authentication_request()
{
    return GWBUF(password_request.data(), password_request.size());
}

PasswordClientAuth::ExchRes PasswordClientAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    ExchRes rval;
    mxb_assert(input.length() >= 5);    // Protocol code should have checked.
    if (input[0] == 'p')
    {
        // The client packet should work as a password token as is.
        session.auth_data().client_token.assign(input.data(), input.end());
        rval.status = ExchRes::Status::READY;
    }
    return rval;
}

PgClientAuthenticator::AuthRes
PasswordClientAuth::authenticate(PgProtocolData& session)
{
    AuthRes rval;
    // TODO: Parse and check password against password hash. For now, just accept it.
    rval.status = AuthRes::Status::SUCCESS;
    return rval;
}

GWBUF PasswordBackendAuth::exchange(GWBUF&& input, PgProtocolData& session)
{
    GWBUF rval;
    if (input.length() == password_request.size()
        && memcmp(input.data(), password_request.data(), password_request.size()) == 0)
    {
        const auto& token = session.auth_data().client_token;
        rval.append(token.data(), token.size());
    }
    return rval;
}

std::unique_ptr<PgClientAuthenticator> PasswordAuthModule::create_client_authenticator() const
{
    return std::make_unique<PasswordClientAuth>();
}

std::unique_ptr<PgBackendAuthenticator> PasswordAuthModule::create_backend_authenticator() const
{
    return std::make_unique<PasswordBackendAuth>();
}

std::string PasswordAuthModule::name() const
{
    return "password";
}
