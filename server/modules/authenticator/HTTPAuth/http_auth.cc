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

#include <maxscale/protocol/httpd/module_names.hh>
#define MXS_MODULE_NAME MXS_HTTPAUTH_AUTHENTICATOR_NAME

#include <maxscale/authenticator2.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/users.hh>

class HTTPAuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    static HTTPAuthenticatorModule* create(char** options)
    {
        return new(std::nothrow) HTTPAuthenticatorModule();
    }

    ~HTTPAuthenticatorModule() override = default;

    std::unique_ptr<mxs::ClientAuthenticator> create_client_authenticator() override;

    int load_users(Listener* listener) override
    {
        return MXS_AUTH_LOADUSERS_OK;
    }

    void diagnostics(DCB* output) override
    {
    }

    json_t* diagnostics_json() override
    {
        return json_array();
    }

    std::string supported_protocol() const override
    {
        return MXS_HTTPD_PROTOCOL_NAME;
    }
};

class HTTPClientAuthenticator : public mxs::ClientAuthenticatorT<HTTPAuthenticatorModule>
{
public:
    HTTPClientAuthenticator(HTTPAuthenticatorModule* module)
        : ClientAuthenticatorT(module)
    {
    }

    ~HTTPClientAuthenticator() override = default;
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
        return MXS_AUTH_SUCCEEDED;
    }

    void free_data(DCB* client) override
    {
    }

    // No fields, authenticator does nothing.
};

std::unique_ptr<mxs::ClientAuthenticator> HTTPAuthenticatorModule::create_client_authenticator()
{
    return std::unique_ptr<mxs::ClientAuthenticator>(new(std::nothrow) HTTPClientAuthenticator(this));
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
        &mxs::AuthenticatorApi<HTTPAuthenticatorModule>::s_api,
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
