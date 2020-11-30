/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/authenticator.hh>
#include "internal/modules.hh"

namespace maxscale
{

/**
 * Initialize an authenticator module
 *
 * @param authenticator Authenticator name
 * @param options Authenticator options
 * @return Authenticator instance or NULL on error
 */
std::unique_ptr<AuthenticatorModule>
authenticator_init(const std::string& authenticator, mxs::ConfigParameters* options)
{
    std::unique_ptr<AuthenticatorModule> rval;
    auto func = (mxs::AUTHENTICATOR_API*)load_module(authenticator.c_str(), MODULE_AUTHENTICATOR);
    if (func)
    {
        rval.reset(func->create(options));
    }
    return rval;
}

}
