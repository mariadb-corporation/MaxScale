/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "HTTPAuth"

#include <maxscale/authenticator2.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/users.h>

class HTTPAuthenticatorSession : public mxs::AuthenticatorSession
{
public:
    ~HTTPAuthenticatorSession() override = default;
    bool extract(DCB* client, GWBUF* buffer) override
    {
        return true;
    }

    bool ssl_capable(DCB* client) override
    {
        return false;
    }

    int authenticate(DCB* client) override
    {
        return 0;
    }

    void free_data(DCB* client) override
    {
    }

    // No fields, authenticator does nothing.
};

class HTTPAuthenticator : public mxs::Authenticator
{
public:
    static HTTPAuthenticator* create(char** options)
    {
        return new(std::nothrow) HTTPAuthenticator();
    }

    ~HTTPAuthenticator() override = default;

    std::unique_ptr<mxs::AuthenticatorSession> createSession() override
    {
        return std::unique_ptr<mxs::AuthenticatorSession>(new(std::nothrow) HTTPAuthenticatorSession());
    }

    int load_users(Listener* listener) override
    {
        return users_default_loadusers(listener);
    }

    void diagnostics(DCB* output, Listener* listener) override
    {
        users_default_diagnostic(output, listener);

    }

    json_t* diagnostics_json(const Listener* listener) override
    {
        return users_default_diagnostic_json(listener);
    }
};

extern "C"
{
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The MaxScale HTTP authenticator (does nothing)",
        "V2.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApi<HTTPAuthenticator>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
