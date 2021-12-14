/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <string>

/**
 * The MXS_AUTHENTICATOR version data. The following should be updated whenever
 * the MXS_AUTHENTICATOR structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_AUTHENTICATOR_VERSION {3, 0, 0}

namespace maxscale
{
class ConfigParameters;

/**
 * Base class off all authenticator modules.
 */
class AuthenticatorModule
{
public:
    virtual ~AuthenticatorModule() = default;

    /**
     * Get name of supported protocol module.
     *
     * @return Supported protocol
     */
    virtual std::string supported_protocol() const = 0;

    /**
     * Get the module name.
     *
     * @return Module name
     */
    virtual std::string name() const = 0;
};

using SAuthenticatorModule = std::unique_ptr<AuthenticatorModule>;

/**
 * This struct contains the authenticator entrypoint in a shared library.
 */
struct AUTHENTICATOR_API
{
    /**
     * Create an authenticator module instance.
     *
     * @param options Authenticator options
     * @return Authenticator object, or null on error
     */
    mxs::AuthenticatorModule* (*create)(ConfigParameters* options);
};

template<class AuthenticatorImplementation>
class AuthenticatorApiGenerator
{
public:
    AuthenticatorApiGenerator() = delete;
    AuthenticatorApiGenerator(const AuthenticatorApiGenerator&) = delete;
    AuthenticatorApiGenerator& operator=(const AuthenticatorApiGenerator&) = delete;

    static AuthenticatorModule* createInstance(mxs::ConfigParameters* options)
    {
        AuthenticatorModule* instance = nullptr;
        MXS_EXCEPTION_GUARD(instance = AuthenticatorImplementation::create(options));
        return instance;
    }

    static AUTHENTICATOR_API s_api;
};

template<class AuthenticatorImplementation>
AUTHENTICATOR_API AuthenticatorApiGenerator<AuthenticatorImplementation>::s_api =
{
    &AuthenticatorApiGenerator<AuthenticatorImplementation>::createInstance
};
}

namespace maxscale
{
std::unique_ptr<mxs::AuthenticatorModule> authenticator_init(const std::string& authenticator,
                                                             mxs::ConfigParameters* options);
}
