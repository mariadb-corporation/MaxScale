/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/authenticator2.hh>

#include <maxscale/modutil.hh>
#include <maxbase/alloc.h>

#include "internal/modules.hh"

/**
 * @file authenticator.c - Authenticator module functions
 */

using mxs::AuthenticatorModule;

namespace maxscale
{

/**
 * @brief Initialize an authenticator module
 *
 * Process the options into an array and pass them to the authenticator
 * initialization function
 *
 * @param authenticator Authenticator name
 * @param options Authenticator options
 * @return Authenticator instance or NULL on error
 */
std::unique_ptr<AuthenticatorModule> authenticator_init(const char* authenticator, const char* options)
{
    std::unique_ptr<AuthenticatorModule> rval;
    auto func = (mxs::AUTHENTICATOR_API*)load_module(authenticator, MODULE_AUTHENTICATOR);

    // Client authenticator modules must have an init-entrypoint.
    if (func && func->initialize)
    {
        char* optarray[AUTHENTICATOR_MAX_OPTIONS + 1];
        size_t optlen = options && *options ? strlen(options) : 0;
        char optcopy[optlen + 1];
        int optcount = 0;

        if (options && *options)
        {
            strcpy(optcopy, options);
            char* opt = optcopy;

            while (opt && optcount < AUTHENTICATOR_MAX_OPTIONS)
            {
                char* end = strnchr_esc(opt, ',', sizeof(optcopy) - (opt - optcopy));

                if (end)
                {
                    *end++ = '\0';
                }

                optarray[optcount++] = opt;
                opt = end;
            }
        }

        optarray[optcount] = NULL;

        rval.reset(func->initialize(optarray));
    }
    return rval;
}

const char* to_string(mxs_auth_state_t state)
{
    const char* rval = "UNKNOWN AUTH STATE";
    switch (state)
    {
    case MXS_AUTH_STATE_INIT:
        rval = "MXS_AUTH_STATE_INIT";
        break;

    case MXS_AUTH_STATE_PENDING_CONNECT:
        rval = "MXS_AUTH_STATE_PENDING_CONNECT";
        break;

    case MXS_AUTH_STATE_CONNECTED:
        rval = "MXS_AUTH_STATE_CONNECTED";
        break;

    case MXS_AUTH_STATE_MESSAGE_READ:
        rval = "MXS_AUTH_STATE_MESSAGE_READ";
        break;

    case MXS_AUTH_STATE_RESPONSE_SENT:
        rval = "MXS_AUTH_STATE_RESPONSE_SENT";
        break;

    case MXS_AUTH_STATE_FAILED:
        rval = "MXS_AUTH_STATE_FAILED";
        break;

    case MXS_AUTH_STATE_HANDSHAKE_FAILED:
        rval = "MXS_AUTH_STATE_HANDSHAKE_FAILED";
        break;

    case MXS_AUTH_STATE_COMPLETE:
        rval = "MXS_AUTH_STATE_COMPLETE";
        break;

    default:
        mxb_assert(!true);
        break;
    }

    return rval;
}

uint64_t AuthenticatorModule::capabilities() const
{
    return 0;
}

int ClientAuthenticator::reauthenticate(DCB* client, uint8_t* scramble, size_t scramble_len,
                                        const ByteVec& auth_token, uint8_t* output)
{
    return MXS_AUTH_STATE_FAILED;
}

}
