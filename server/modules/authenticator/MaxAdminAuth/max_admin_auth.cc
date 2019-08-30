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

#define MXS_MODULE_NAME "MaxAdminAuth"

#include <maxscale/authenticator2.hh>
#include <maxbase/alloc.h>
#include <maxscale/modinfo.hh>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/adminusers.h>
#include <maxscale/users.h>

static bool max_admin_auth_set_protocol_data(DCB* dcb, GWBUF* buf);
static bool max_admin_auth_is_client_ssl_capable(DCB* dcb);
static int  max_admin_auth_authenticate(DCB* dcb);
static void max_admin_auth_free_client_data(DCB* dcb);

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

class MaxAdminClientAuthenticator : public mxs::ClientAuthenticatorT<MaxAdminAuthenticatorModule>
{
public:
    MaxAdminClientAuthenticator(MaxAdminAuthenticatorModule* module)
        : ClientAuthenticatorT(module)
    {
    }

    ~MaxAdminClientAuthenticator() override = default;
    bool extract(DCB* client, GWBUF* buffer) override
    {
        return max_admin_auth_set_protocol_data(client, buffer);
    }

    bool ssl_capable(DCB* client) override
    {
        return max_admin_auth_is_client_ssl_capable(client);
    }

    int authenticate(DCB* client) override
    {
        return max_admin_auth_authenticate(client);
    }

    void free_data(DCB* client) override
    {
        max_admin_auth_free_client_data(client);
    }

    // No fields, data is contained in protocol.
};

std::unique_ptr<mxs::ClientAuthenticator> MaxAdminAuthenticatorModule::create_client_authenticator()
{
    return std::unique_ptr<mxs::ClientAuthenticator>(new(std::nothrow) MaxAdminClientAuthenticator(this));
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
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
/*lint +e14 */
}

/**
 * @brief Authentication of a user/password combination.
 *
 * The validation is already done, the result is returned.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Authentication status - always 0 to denote success
 */
static int max_admin_auth_authenticate(DCB* generic_dcb)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    return (dcb->protocol_data() != NULL && ((ADMIN_session*)dcb->protocol_data())->validated) ? 0 : 1;
}

/**
 * @brief Transfer data from the authentication request to the DCB.
 *
 * Expects a chain of two buffers as the second parameters, with the
 * username in the first buffer and the password in the second buffer.
 *
 * @param dcb Request handler DCB connected to the client
 * @param buffer Pointer to pointer to buffers containing data from client
 * @return Authentication status - true for success, false for failure
 */
static bool max_admin_auth_set_protocol_data(DCB* generic_dcb, GWBUF* buf)
{
    mxb_assert(generic_dcb->role() == DCB::Role::CLIENT);
    auto dcb = static_cast<ClientDCB*>(generic_dcb);

    ADMIN_session* session_data;

    max_admin_auth_free_client_data(dcb);

    if ((session_data = (ADMIN_session*)MXS_CALLOC(1, sizeof(ADMIN_session))) != NULL)
    {
        int user_len = (GWBUF_LENGTH(buf) > ADMIN_USER_MAXLEN) ? ADMIN_USER_MAXLEN : GWBUF_LENGTH(buf);
        memcpy(session_data->user, GWBUF_DATA(buf), user_len);
        session_data->validated = false;
        dcb->protocol_data_set((void*)session_data);

        /* Check for existance of the user */
        if (admin_linux_account_enabled(session_data->user))
        {
            session_data->validated = true;
            return true;
        }
    }
    return false;
}

/**
 * @brief Determine whether the client is SSL capable
 *
 * Always say that client is not SSL capable. Support for SSL is not yet
 * available.
 *
 * @param dcb Request handler DCB connected to the client
 * @return Boolean indicating whether client is SSL capable - false
 */
static bool max_admin_auth_is_client_ssl_capable(DCB* dcb)
{
    return false;
}

/**
 * @brief Free the client data pointed to by the passed DCB.
 *
 * The max_admin authenticator uses a simple structure that can be freed with
 * a single call to MXS_FREE().
 *
 * @param dcb Request handler DCB connected to the client
 */
static void max_admin_auth_free_client_data(DCB* dcb)
{
    mxb_assert(dcb->role() == DCB::Role::CLIENT);
    MXS_FREE(static_cast<ClientDCB*>(dcb)->protocol_data_release());
}
