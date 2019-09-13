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

/**
 * @file max_admin_auth.c
 *
 * MaxScale Admin Authentication module for checking of clients credentials
 * for access to MaxAdmin.  Might be usable for other purposes.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 14/03/2016   Martin Brampton         Initial version
 * 17/05/2016   Massimiliano Pinto      New version authenticates UNIX user
 *
 * @endverbatim
 */

#include <maxscale/protocol/maxscaled/module_names.hh>
#define MXS_MODULE_NAME MXS_MAXADMINAUTH_AUTHENTICATOR_NAME

#include <maxscale/authenticator2.hh>
#include <maxbase/alloc.h>
#include <maxscale/modinfo.hh>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/adminusers.hh>
#include <maxscale/users.hh>

class MaxAdminAuthenticatorModule : public mxs::AuthenticatorModule
{
public:
    static MaxAdminAuthenticatorModule* create(char** options)
    {
        return new(std::nothrow) MaxAdminAuthenticatorModule();
    }

    ~MaxAdminAuthenticatorModule() override = default;

    std::unique_ptr<mxs::ClientAuthenticator> create_client_authenticator() override;

    int load_users(Listener* listener) override
    {
        // User account data is handled by core.
        return MXS_AUTH_LOADUSERS_OK;
    }

    void diagnostics(DCB* output) override
    {
        // TODO: print unix users
    }

    json_t* diagnostics_json() override
    {
        return json_array(); // TODO: Unix users
    }

    std::string supported_protocol() const override
    {
        return MXS_MAXSCALED_PROTOCOL_NAME;
    }
};

class MaxAdminClientAuthenticator : public mxs::ClientAuthenticatorT<MaxAdminAuthenticatorModule>
{
public:
    MaxAdminClientAuthenticator(MaxAdminAuthenticatorModule* module)
        : ClientAuthenticatorT(module)
    {
    }

    ~MaxAdminClientAuthenticator() override = default;
    bool extract(DCB* dcb, GWBUF* buffer) override;

    bool ssl_capable(DCB* client) override
    {
        return false;
    }

    int authenticate(DCB* client) override;

    void free_data(DCB* client) override
    {
    }

private:
    std::string m_user; /**< username */
};

std::unique_ptr<mxs::ClientAuthenticator> MaxAdminAuthenticatorModule::create_client_authenticator()
{
    return std::unique_ptr<mxs::ClientAuthenticator>(new(std::nothrow) MaxAdminClientAuthenticator(this));
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "The MaxScale Admin client authenticator implementation",
        "V2.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApi<MaxAdminAuthenticatorModule>::s_api,
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

/**
 * @brief Authentication of a user/password combination.
 *
 * @param client Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
int MaxAdminClientAuthenticator::authenticate(DCB* client)
{
    /* Check for existence of the user */
    return admin_linux_account_enabled(m_user.c_str()) ? 0 : 1;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Expects a chain of two buffers as the second parameters, with the
 * username in the first buffer and the password in the second buffer.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buf Pointer to pointer to buffers containing data from client
 * @return Authentication status - true for success, false for failure
 */
bool MaxAdminClientAuthenticator::extract(DCB* client, GWBUF* buf)
{
    // Buffer may not be 0-terminated.
    int used_buf_len = (GWBUF_LENGTH(buf) > ADMIN_USER_MAXLEN) ? ADMIN_USER_MAXLEN : GWBUF_LENGTH(buf);
    auto user_name = (const char*)(GWBUF_DATA(buf));
    int user_name_len = strnlen(user_name, used_buf_len);
    m_user.assign(user_name, user_name_len);
    return true;
}
