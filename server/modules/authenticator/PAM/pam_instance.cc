/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <maxscale/jansson.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/secrets.hh>
#include "pam_client_session.hh"
#include "pam_backend_session.hh"

using std::string;

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamAuthenticatorModule* PamAuthenticatorModule::create(char** options)
{
    return new(std::nothrow) PamAuthenticatorModule();
}

/**
 * @brief Populates the internal user database by reading from one of the backend servers
 *
 * @param service The service the users should be read from
 *
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR on error
 */
int PamAuthenticatorModule::load_users(SERVICE* service)
{
    return MXS_AUTH_LOADUSERS_OK;
}

json_t* PamAuthenticatorModule::diagnostics()
{
    json_t* rval = json_array();
    return rval;
}

uint64_t PamAuthenticatorModule::capabilities() const
{
    return CAP_BACKEND_AUTH | CAP_ANON_USER;
}

std::string PamAuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

mariadb::SClientAuth PamAuthenticatorModule::create_client_authenticator()
{
    return mariadb::SClientAuth(new(std::nothrow) PamClientAuthenticator());
}

mariadb::SBackendAuth PamAuthenticatorModule::create_backend_authenticator()
{
    return mariadb::SBackendAuth(new(std::nothrow) PamBackendAuthenticator());
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
}