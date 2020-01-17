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

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>
#include "user_data.hh"

using std::string;

MySQLProtocolModule* MySQLProtocolModule::create()
{
    return new MySQLProtocolModule();
}

std::unique_ptr<mxs::ClientConnection>
MySQLProtocolModule::create_client_protocol(MXS_SESSION* session, mxs::Component* component)
{
    std::unique_ptr<mxs::ClientConnection> new_client_proto;
    std::unique_ptr<MYSQL_session> mdb_session(new(std::nothrow) MYSQL_session());
    if (mdb_session)
    {
        auto& search_sett = mdb_session->user_search_settings;
        search_sett.listener = m_user_search_settings;
        auto& service_config = session->service->config();
        search_sett.service.allow_root_user = service_config.enable_root;
        search_sett.service.localhost_match_wildcard_host = service_config.localhost_match_wildcard_host;

        mdb_session->remote = session->client_remote();
        session->set_protocol_data(std::move(mdb_session));

        new_client_proto = std::unique_ptr<mxs::ClientConnection>(
            new(std::nothrow) MariaDBClientConnection(session, component));
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

std::unique_ptr<mxs::UserAccountManager> MySQLProtocolModule::create_user_data_manager()
{
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager());
}

std::unique_ptr<mxs::BackendConnection>
MySQLProtocolModule::create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
{
    // Allocate DCB specific backend-authentication data from the client session.
    mariadb::SBackendAuth new_backend_auth;
    auto mariases = static_cast<MYSQL_session*>(session->protocol_data());
    auto auth_module = mariases->m_current_authenticator;
    if (auth_module->capabilities() & mariadb::AuthenticatorModule::CAP_BACKEND_AUTH)
    {
        new_backend_auth = auth_module->create_backend_authenticator();
        if (!new_backend_auth)
        {
            MXS_ERROR("Failed to create backend authenticator session.");
        }
    }
    else
    {
        MXS_ERROR("Authenticator '%s' does not support backend authentication. "
                  "Cannot create backend connection.", auth_module->name().c_str());
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
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTHDATA
           | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

bool MySQLProtocolModule::parse_authenticator_opts(const std::string& opts,
                                                   const AuthenticatorList& authenticators)
{
    bool error = false;
    // Check if any of the authenticators support anonymous users.
    for (const auto& auth_module : authenticators)
    {
        auto mariadb_auth = static_cast<mariadb::AuthenticatorModule*>(auth_module.get());
        if (mariadb_auth->capabilities() & mariadb::AuthenticatorModule::CAP_ANON_USER)
        {
            m_user_search_settings.allow_anon_user = true;
            break;
        }
    }

    auto opt_list = mxb::strtok(opts, ",");
    for (auto opt : opt_list)
    {
        auto equals_pos = opt.find('=');
        if (equals_pos != string::npos && equals_pos > 0 && opt.length() > equals_pos + 1)
        {
            string opt_name = opt.substr(0, equals_pos);
            mxb::trim(opt_name);
            string opt_value = opt.substr(equals_pos + 1);
            mxb::trim(opt_value);
            // TODO: add better parsing, check for invalid values
            int val = config_truth_value(opt_value.c_str());
            if (opt_name == "cache_dir")
            {
                // ignored
            }
            else if (opt_name == "inject_service_user")
            {
                m_user_search_settings.allow_service_user = val;
            }
            else if (opt_name == "skip_authentication")
            {
                m_user_search_settings.match_host_pattern = !val;
            }
            else if (opt_name == "lower_case_table_names")
            {
                m_user_search_settings.case_sensitive_db = !val;
            }
            else
            {
                MXB_ERROR("Unknown authenticator option: %s", opt_name.c_str());
                error = true;
            }
        }
        else
        {
            MXB_ERROR("Invalid authenticator option setting: %s", opt.c_str());
            error = true;
        }
    }
    return !error;
}

mxs::ProtocolModule::AuthenticatorList
MySQLProtocolModule::create_authenticators(const MXS_CONFIG_PARAMETER& params)
{
    // If no authenticator is set, the default authenticator will be loaded.
    auto auth_names = params.get_string(CN_AUTHENTICATOR);
    auto auth_opts = params.get_string(CN_AUTHENTICATOR_OPTIONS);

    if (auth_names.empty())
    {
        auth_names = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    }

    AuthenticatorList authenticators;
    auto auth_names_list = mxb::strtok(auth_names, ",");
    bool error = false;

    for (auto iter = auth_names_list.begin(); iter != auth_names_list.end() && !error; ++iter)
    {
        string auth_name = *iter;
        mxb::trim(auth_name);
        if (!auth_name.empty())
        {
            const char* auth_namez = auth_name.c_str();
            auto new_auth_module = mxs::authenticator_init(auth_namez, auth_opts.c_str());
            if (new_auth_module)
            {
                // Check that the authenticator supports the protocol. Use case-insensitive comparison.
                auto supported_protocol = new_auth_module->supported_protocol();
                if (strcasecmp(MXS_MODULE_NAME, supported_protocol.c_str()) == 0)
                {
                    authenticators.push_back(move(new_auth_module));
                }
                else
                {
                    // When printing protocol name, print the name user gave in configuration file,
                    // not the effective name.
                    MXB_ERROR("Authenticator module '%s' expects to be paired with protocol '%s', "
                              "not with '%s'.",
                              auth_namez, supported_protocol.c_str(), MXS_MODULE_NAME);
                    error = true;
                }
            }
            else
            {
                MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_namez);
                error = true;
            }
        }
        else
        {
            MXB_ERROR("'%s' has an invalid value '%s'. The value should be a comma-separated "
                      "list of authenticators, a single authenticator or empty.",
                      CN_AUTHENTICATOR, auth_names.c_str());
            error = true;
        }
    }

    // Also parse authentication settings which affect the entire listener.
    if (!error && !parse_authenticator_opts(auth_opts, authenticators))
    {
        error = true;
    }

    if (error)
    {
        authenticators.clear();
    }
    return authenticators;
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
