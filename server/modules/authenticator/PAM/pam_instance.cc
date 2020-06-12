/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <maxscale/config_common.hh>
#include <maxscale/jansson.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/secrets.hh>
#include "pam_client_session.hh"
#include "pam_backend_session.hh"

using std::string;

namespace
{
const string opt_cleartext_plugin = "pam_use_cleartext_plugin";
}

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamAuthenticatorModule* PamAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    bool cleartext_plugin = false;
    if (options->contains(opt_cleartext_plugin))
    {
        cleartext_plugin = options->get_bool(opt_cleartext_plugin);
        options->remove(opt_cleartext_plugin);
    }
    return new(std::nothrow) PamAuthenticatorModule(cleartext_plugin);
}

uint64_t PamAuthenticatorModule::capabilities() const
{
    return CAP_ANON_USER;
}

std::string PamAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

mariadb::SClientAuth PamAuthenticatorModule::create_client_authenticator()
{
    return mariadb::SClientAuth(new(std::nothrow) PamClientAuthenticator(m_cleartext_plugin));
}

mariadb::SBackendAuth
PamAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new(std::nothrow) PamBackendAuthenticator(auth_data));
}

std::string PamAuthenticatorModule::name() const
{
    return MXS_MODULE_NAME;
}

const std::unordered_set<std::string>& PamAuthenticatorModule::supported_plugins() const
{
    static const std::unordered_set<std::string> plugins = {"pam"};
    return plugins;
}

PamAuthenticatorModule::PamAuthenticatorModule(bool cleartext_plugin)
    : m_cleartext_plugin(cleartext_plugin)
{
}

extern "C"
{
/**
 * Module handle entry point
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_AUTHENTICATOR,
        MXS_MODULE_GA,
        MXS_AUTHENTICATOR_VERSION,
        "PAM authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<PamAuthenticatorModule>::s_api,
        NULL,           /* Process init. */
        NULL,           /* Process finish. */
        NULL,           /* Thread init. */
        NULL,           /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}
