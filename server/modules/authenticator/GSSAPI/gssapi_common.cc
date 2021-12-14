/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "gssapi_common.hh"
#include "gssapi_client_auth.hh"
#include "gssapi_backend_auth.hh"

#include <maxbase/alloc.h>
#include <maxscale/protocol/mariadb/module_names.hh>

using std::string;

/**
 * @brief Report GSSAPI errors
 *
 * @param major GSSAPI major error number
 * @param minor GSSAPI minor error number
 */
void report_error(OM_uint32 major, OM_uint32 minor, const char* failed_func)
{
    OM_uint32 res = 0;
    gss_buffer_desc major_msg = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc minor_msg = GSS_C_EMPTY_BUFFER;
    OM_uint32 minor_status = 0;
    gss_display_status(&minor_status, major, GSS_C_GSS_CODE, nullptr, &res, &major_msg);
    gss_display_status(&minor_status, minor, GSS_C_MECH_CODE, nullptr, &res, &minor_msg);
    MXS_ERROR("%s failed. Major error %u: '%.*s' Minor error %u: '%.*s'",
              failed_func, major, (int)major_msg.length, (const char*)major_msg.value,
              minor, (int)minor_msg.length, (const char*)minor_msg.value);
    gss_release_buffer(&minor_status, &major_msg);
    gss_release_buffer(&minor_status, &minor_msg);
}

uint64_t GSSAPIAuthenticatorModule::capabilities() const
{
    return 0;
}

std::string GSSAPIAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

/**
 * @brief Initialize the GSSAPI authenticator
 *
 * This function processes the service principal name that is given to the client.
 *
 * @param options Listener options
 * @return Authenticator instance
 */
GSSAPIAuthenticatorModule* GSSAPIAuthenticatorModule::create(mxs::ConfigParameters* options)
{
    /** This is mainly for testing purposes */
    const string default_princ_name = "mariadb/localhost.localdomain";

    auto instance = new(std::nothrow) GSSAPIAuthenticatorModule();
    if (instance)
    {
        const string princ_option = "principal_name";
        if (options->contains(princ_option))
        {
            instance->m_service_principal = options->get_string(princ_option);
            options->remove(princ_option);
        }
        else
        {
            instance->m_service_principal = default_princ_name;
            MXS_NOTICE("Using default principal name: %s", instance->m_service_principal.c_str());
        }

        const string keytab_option = "gssapi_keytab_path";
        if (options->contains(keytab_option))
        {
            string keytab_path = options->get_string(keytab_option);
            MXS_INFO("Setting default krb5 keytab environment variable to '%s'.", keytab_path.c_str());
            setenv("KRB5_KTNAME", keytab_path.c_str(), 1);
            options->remove(keytab_option);
        }
    }
    return instance;
}

mariadb::SClientAuth GSSAPIAuthenticatorModule::create_client_authenticator()
{
    return std::make_unique<GSSAPIClientAuthenticator>(m_service_principal);
}

mariadb::SBackendAuth
GSSAPIAuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return std::make_unique<GSSAPIBackendAuthenticator>(auth_data);
}

std::string GSSAPIAuthenticatorModule::name() const
{
    return MXS_MODULE_NAME;
}

const std::unordered_set<std::string>& GSSAPIAuthenticatorModule::supported_plugins() const
{
    static const std::unordered_set<std::string> plugins = {"gssapi"};
    return plugins;
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
        "GSSAPI authenticator",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<GSSAPIAuthenticatorModule>::s_api,
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
