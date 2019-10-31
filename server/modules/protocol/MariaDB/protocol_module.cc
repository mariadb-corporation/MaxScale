/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#include <maxscale/protocol/mariadb/module_names.hh>
#define MXS_MODULE_NAME MXS_MARIADB_PROTOCOL_NAME

#include "protocol_module.hh"
#include <maxscale/protocol/mariadb/client_connection.hh>
#include <maxscale/protocol/mariadb/backend_connection.hh>

#include <maxscale/modutil.hh>
#include "user_data.hh"

MySQLProtocolModule* MySQLProtocolModule::create(const std::string& auth_name, const std::string& auth_opts)
{
    MySQLProtocolModule* protocol_module = nullptr;
    // TODO: Add support for multiple authenticators.

    const char* auth_namez {nullptr};
    const char* auth_optsz {nullptr};
    if (!auth_name.empty())
    {
        auth_namez = auth_name.c_str();
        auth_optsz = auth_opts.c_str();
    }
    else
    {
        auth_namez = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    }

    auto new_auth_module = mxs::authenticator_init(auth_namez, auth_optsz);
    if (new_auth_module)
    {
        // Check that the authenticator supports the protocol. Use case-insensitive comparison.
        auto supported_protocol = new_auth_module->supported_protocol();
        if (strcasecmp(MXS_MODULE_NAME, supported_protocol.c_str()) == 0)
        {
            protocol_module = new(std::nothrow) MySQLProtocolModule();
            if (protocol_module)
            {
                auto mdb_auth_module = static_cast<mariadb::AuthenticatorModule*>(new_auth_module.release());
                protocol_module->m_auth_module.reset(mdb_auth_module);
            }
        }
        else
        {
            // When printing protocol name, print the name user gave in configuration file,
            // not the effective name.
            MXB_ERROR("Authenticator module '%s' expects to be paired with protocol '%s', "
                      "not with '%s'.",
                      auth_namez, supported_protocol.c_str(), MXS_MODULE_NAME);
        }
    }
    else
    {
        MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_namez);
    }
    return protocol_module;
}

std::unique_ptr<mxs::ClientConnection>
MySQLProtocolModule::create_client_protocol(MXS_SESSION* session, mxs::Component* component)
{
    std::unique_ptr<mxs::ClientConnection> new_client_proto;
    auto authenticator = m_auth_module->create_client_authenticator();
    std::unique_ptr<MXS_SESSION::ProtocolData> session_data(new(std::nothrow) MYSQL_session());
    if (authenticator && session_data)
    {
        session->set_protocol_data(std::move(session_data));
        new_client_proto = std::unique_ptr<mxs::ClientConnection>(
            new(std::nothrow) MariaDBClientConnection(session, component, std::move(authenticator)));
    }
    return new_client_proto;
}

std::string MySQLProtocolModule::auth_default() const
{
    return MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
}

GWBUF* MySQLProtocolModule::reject(const std::string& host)
{
    std::string message = "Host '" + host
        + "' is temporarily blocked due to too many authentication failures.";
    return modutil_create_mysql_err_msg(0, 0, 1129, "HY000", message.c_str());
}

std::string MySQLProtocolModule::name() const
{
    return MXS_MODULE_NAME;
}

int MySQLProtocolModule::load_auth_users(SERVICE* service)
{
    return m_auth_module->load_users(service);
}

void MySQLProtocolModule::print_auth_users(DCB* output)
{
    m_auth_module->diagnostics(output);
}

json_t* MySQLProtocolModule::print_auth_users_json()
{
    return m_auth_module->diagnostics_json();
}

std::unique_ptr<mxs::UserAccountManager>
MySQLProtocolModule::create_user_data_manager(const std::string& service_name)
{
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager(service_name));
}

std::unique_ptr<mxs::BackendConnection>
MySQLProtocolModule::create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
{
    // Allocate DCB specific backend-authentication data from the client session.
    std::unique_ptr<mariadb::BackendAuthenticator> new_backend_auth;
    if (m_auth_module->capabilities() & mariadb::AuthenticatorModule::CAP_BACKEND_AUTH)
    {
        new_backend_auth = m_auth_module->create_backend_authenticator();
        if (!new_backend_auth)
        {
            MXS_ERROR("Failed to create backend authenticator session.");
        }
    }
    else
    {
        MXS_ERROR("Authenticator '%s' does not support backend authentication. "
                  "Cannot create backend connection.", m_auth_module->name().c_str());
    }

    std::unique_ptr<mxs::BackendConnection> rval;
    if (new_backend_auth)
    {
        rval = MariaDBBackendConnection::create(session, component, std::move(new_backend_auth));
    }
    return rval;
}

uint64_t MySQLProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTHDATA;
}

/**
 * mariadbclient module entry point.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_GA,
        MXS_PROTOCOL_VERSION,
        "The client to MaxScale MySQL protocol implementation",
        "V1.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ProtocolApiGenerator<MySQLProtocolModule>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
