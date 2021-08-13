/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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

/**
 * @brief Report GSSAPI errors
 *
 * @param major GSSAPI major error number
 * @param minor GSSAPI minor error number
 */
void report_error(OM_uint32 major, OM_uint32 minor)
{
    OM_uint32 status_maj = major;
    OM_uint32 status_min = minor;
    OM_uint32 res = 0;
    gss_buffer_desc buf = {0, 0};

    major = gss_display_status(&minor, status_maj, GSS_C_GSS_CODE, NULL, &res, &buf);

    {
        char sbuf[buf.length + 1];
        memcpy(sbuf, buf.value, buf.length);
        sbuf[buf.length] = '\0';
        MXS_ERROR("GSSAPI Major Error: %s", sbuf);
    }

    major = gss_display_status(&minor, status_min, GSS_C_MECH_CODE, NULL, &res, &buf);

    {
        char sbuf[buf.length + 1];
        memcpy(sbuf, buf.value, buf.length);
        sbuf[buf.length] = '\0';
        MXS_ERROR("GSSAPI Minor Error: %s", sbuf);
    }
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
    const std::string default_princ_name = "mariadb/localhost.localdomain";

    auto instance = new(std::nothrow) GSSAPIAuthenticatorModule();
    if (instance)
    {
        const std::string princ_option = "principal_name";
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
