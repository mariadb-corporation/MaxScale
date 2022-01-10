/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <maxscale/config_common.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include "pam_client_session.hh"
#include "pam_backend_session.hh"

using std::string;

namespace
{
const string opt_cleartext_plugin = "pam_use_cleartext_plugin";
const string opt_pam_mode = "pam_mode";
const string pam_mode_pw = "password";
const string pam_mode_pw_2fa = "password_2FA";
}

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamAuthenticatorModule* PamAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    bool error = false;

    bool cleartext_plugin = false;
    if (options->contains(opt_cleartext_plugin))
    {
        cleartext_plugin = options->get_bool(opt_cleartext_plugin);
        options->remove(opt_cleartext_plugin);
    }

    auto pam_mode = AuthMode::PW;
    if (options->contains(opt_pam_mode))
    {
        auto user_pam_mode = options->get_string(opt_pam_mode);
        options->remove(opt_pam_mode);

        if (user_pam_mode == pam_mode_pw_2fa)
        {
            pam_mode = AuthMode::PW_2FA;
        }
        else if (user_pam_mode != pam_mode_pw)
        {
            MXB_ERROR("Invalid value '%s' for authenticator option '%s'. Valid values are '%s' and '%s'.",
                      user_pam_mode.c_str(), opt_pam_mode.c_str(),
                      pam_mode_pw.c_str(), pam_mode_pw_2fa.c_str());
            error = true;
        }
    }

    PamAuthenticatorModule* rval = nullptr;
    if (!error)
    {
        rval = new PamAuthenticatorModule(cleartext_plugin, pam_mode);
    }
    return rval;
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
    return mariadb::SClientAuth(new(std::nothrow) PamClientAuthenticator(m_cleartext_plugin, m_mode));
}

mariadb::SBackendAuth
PamAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return mariadb::SBackendAuth(new(std::nothrow) PamBackendAuthenticator(auth_data, m_mode));
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

PamAuthenticatorModule::PamAuthenticatorModule(bool cleartext_plugin, AuthMode auth_mode)
    : m_cleartext_plugin(cleartext_plugin)
    , m_mode(auth_mode)
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
