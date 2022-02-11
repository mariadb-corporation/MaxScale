/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <maxbase/json.hh>
#include <maxscale/config_common.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/secrets.hh>
#include "pam_client_session.hh"
#include "pam_backend_session.hh"
#include "../MariaDBAuth/mysql_auth.hh"

using std::string;

namespace
{
const string opt_cleartext_plugin = "pam_use_cleartext_plugin";

const string opt_pam_mode = "pam_mode";
const string pam_mode_pw = "password";
const string pam_mode_pw_2fa = "password_2FA";

const string opt_be_map = "pam_backend_mapping";
const string be_map_none = "none";
const string be_map_mariadb = "mariadb";

const string opt_pam_user_map = "pam_mapped_pw_file";

bool load_backend_passwords(const string& filepath, PasswordMap* output_map)
{
    bool rval = false;
    mxb::Json js;
    if (!js.load(filepath))
    {
        MXB_ERROR("Failed to load backend user passwords: %s", js.error_msg().c_str());
    }
    else
    {
        const char errmsg_fmt[] = "Malformed entry in backend passwords file: %s";
        bool all_elems_ok = false;
        auto obj = js.get_object("users_and_passwords");
        if (obj)
        {
            all_elems_ok = true;
            auto arr = obj.get_array_elems();

            for (const auto& elem : arr)
            {
                string user = elem.get_string("user");
                string pw_encr = elem.get_string("password");
                if (elem.ok())
                {
                    // Store the password in the expected SHA1 form.
                    string pw_clear = mxs::decrypt_password(pw_encr);
                    PasswordHash password;
                    gw_sha1_str((const uint8_t*)pw_clear.c_str(), pw_clear.length(), password.pw_hash);
                    (*output_map)[user] = password;
                }
                else
                {
                    MXB_ERROR(errmsg_fmt, elem.error_msg().c_str());
                    all_elems_ok = false;
                }
            }
        }
        else
        {
            MXB_ERROR(errmsg_fmt, js.error_msg().c_str());
        }
        rval = all_elems_ok;
    }
    return rval;
}
}

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamAuthenticatorModule* PamAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    const char errmsg[] = "Invalid value '%s' for authenticator option '%s'. Valid values are '%s' and '%s'.";
    bool error = false;

    AuthSettings settings;
    if (options->contains(opt_cleartext_plugin))
    {
        settings.cleartext_plugin = options->get_bool(opt_cleartext_plugin);
        options->remove(opt_cleartext_plugin);
    }

    if (options->contains(opt_pam_mode))
    {
        auto user_pam_mode = options->get_string(opt_pam_mode);
        options->remove(opt_pam_mode);

        if (user_pam_mode == pam_mode_pw_2fa)
        {
            settings.mode = AuthMode::PW_2FA;
        }
        else if (user_pam_mode != pam_mode_pw)
        {
            MXB_ERROR(errmsg, user_pam_mode.c_str(), opt_pam_mode.c_str(),
                      pam_mode_pw.c_str(), pam_mode_pw_2fa.c_str());
            error = true;
        }
    }

    if (options->contains(opt_be_map))
    {
        string user_be_map = options->get_string(opt_be_map);
        options->remove(opt_be_map);

        if (user_be_map == be_map_mariadb)
        {
            settings.be_mapping = BackendMapping::MARIADB;
        }
        else if (user_be_map != be_map_none)
        {
            MXB_ERROR(errmsg,
                      user_be_map.c_str(), opt_be_map.c_str(),
                      be_map_none.c_str(), be_map_mariadb.c_str());
            error = true;
        }
    }


    PasswordMap backend_pwds;
    if (options->contains(opt_pam_user_map))
    {
        string passwords_file = options->get_string(opt_pam_user_map);
        options->remove(opt_pam_user_map);
        if (load_backend_passwords(passwords_file, &backend_pwds))
        {
            MXB_INFO("Read %zu backend passwords from '%s'.", backend_pwds.size(), passwords_file.c_str());
        }
        else
        {
            error = true;
        }
    }

    PamAuthenticatorModule* rval = nullptr;
    if (!error)
    {
        rval = new PamAuthenticatorModule(settings, move(backend_pwds));
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
    return std::make_unique<PamClientAuthenticator>(m_settings, m_backend_pwds);
}

mariadb::SBackendAuth
PamAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    mariadb::SBackendAuth rval;
    switch (m_settings.be_mapping)
    {
    case BackendMapping::NONE:
        rval = std::make_unique<PamBackendAuthenticator>(auth_data, m_settings.mode);
        break;

    case BackendMapping::MARIADB:
        rval = std::make_unique<MariaDBBackendSession>(auth_data);
        break;
    }
    return rval;
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

PamAuthenticatorModule::PamAuthenticatorModule(AuthSettings& settings, PasswordMap&& backend_pwds)
    : m_settings(settings)
    , m_backend_pwds(move(backend_pwds))
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
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::AUTHENTICATOR,
        mxs::ModuleStatus::GA,
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
