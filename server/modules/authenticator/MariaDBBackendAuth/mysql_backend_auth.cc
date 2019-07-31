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
 * @file mysql_backend_auth.c - MySQL backend authenticator
 *
 * Backend authentication module for the MySQL protocol. Implements the
 * client side of the 'mysql_native_password' authentication plugin.
 *
 * The "heavy lifting" of the authentication is done by the protocol module so
 * the only thing left for this module is to read the final OK packet from the
 * server.
 */

#define MXS_MODULE_NAME "MariaDBBackendAuth"

#include <maxbase/alloc.h>
#include <maxscale/authenticator.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/server.hh>
#include <maxscale/utils.h>
#include <maxscale/authenticator2.hh>

/** Structure representing the authentication state */
class MariaDBBackendSession : public mxs::AuthenticatorBackendSession
{
public:
    static MariaDBBackendSession* newSession()
    {
        return new MariaDBBackendSession();
    }

    ~MariaDBBackendSession() = default;

    bool extract(DCB* backend, GWBUF* buffer) override
    {
        bool rval = false;

        switch (state)
        {
        case State::NEED_OK:
            if (mxs_mysql_is_ok_packet(buffer))
            {
                rval = true;
                state = State::AUTH_OK;
            }
            else
            {
                state = State::AUTH_FAILED;
            }
            break;

        default:
            MXS_ERROR("Unexpected call to MySQLBackendAuth::extract");
            mxb_assert(false);
            break;
        }

        return rval;
    }

    bool ssl_capable(DCB* backend) override
    {
        return backend->m_server->ssl().context() != nullptr;
    }

    int authenticate(DCB* backend) override
    {
        int rval = MXS_AUTH_FAILED;
        if (state == State::AUTH_OK)
        {
            /** Authentication completed successfully */
            rval = MXS_AUTH_SUCCEEDED;
        }
        return rval;
    }

private:
    /** Authentication states */
    enum class State
    {
        NEED_OK,                /**< Waiting for server's OK packet */
        AUTH_OK,                /**< Authentication completed successfully */
        AUTH_FAILED             /**< Authentication failed */
    };

    State state {State::NEED_OK};   /**< Authentication state */
};

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
        "The MySQL MaxScale to backend server authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::BackendAuthenticatorApi<MariaDBBackendSession>::s_api,
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
