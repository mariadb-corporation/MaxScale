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
        const auto& service_config = *session->service->config();
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
    return MariaDBBackendConnection::create(session, component, *server);
}

uint64_t MySQLProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTHDATA
           | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

void MySQLProtocolModule::read_authentication_options(mxs::ConfigParameters* params)
{
    if (!params->empty())
    {
        // Read any values recognized by the protocol itself and remove them. The leftovers are given to
        // authenticators.

        const string inject = "inject_service_user";
        const string skip_auth = "skip_authentication";
        const string lower_case = "lower_case_table_names";

        params->remove("cache_dir"); // ignored
        if (params->contains(inject))
        {
            m_user_search_settings.allow_service_user = params->get_bool(inject);
            params->remove(inject);
        }
        if (params->contains(skip_auth))
        {
            m_user_search_settings.match_host_pattern = !params->get_bool(skip_auth);
            params->remove(skip_auth);
        }
        if (params->contains(lower_case))
        {
            m_user_search_settings.case_sensitive_db = !params->get_bool(lower_case);
            params->remove(lower_case);
        }
    }
}

mxs::ProtocolModule::AuthenticatorList
MySQLProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    // If no authenticator is set, the default authenticator will be loaded.
    auto auth_names = params.get_string(CN_AUTHENTICATOR);
    auto auth_opts = params.get_string(CN_AUTHENTICATOR_OPTIONS);

    if (auth_names.empty())
    {
        auth_names = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    }

    mxs::ConfigParameters auth_config;
    if (parse_auth_options(auth_opts, &auth_config))
    {
        // Read authentication settings which affect the entire listener.
        read_authentication_options(&auth_config);
    }
    else
    {
        return {}; // error
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
            auto new_auth_module = mxs::authenticator_init(auth_name, &auth_config);
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
            MXB_ERROR("'%s' is an invalid value for '%s'. The value should be a comma-separated "
                      "list of authenticators or a single authenticator.",
                      auth_names.c_str(), CN_AUTHENTICATOR);
            error = true;
        }
    }

    // All authenticators have been created. Any remaining settings in the config object are unrecognized.
    if (!error && !auth_config.empty())
    {
        error = true;
        for (const auto& elem : auth_config)
        {
            MXB_ERROR("Unrecognized authenticator option: '%s'", elem.first.c_str());
        }
    }

    if (!error)
    {
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
    }

    if (error)
    {
        authenticators.clear();
    }
    return authenticators;
}

/**
 * Parse the authenticator options string to config parameters object.
 *
 * @param opts Options string
 * @param params_out Config object to write to
 * @return True on success
 */
bool MySQLProtocolModule::parse_auth_options(const std::string& opts, mxs::ConfigParameters* params_out)
{
    bool error = false;
    auto opt_list = mxb::strtok(opts, ",");

    for (const auto& opt : opt_list)
    {
        auto equals_pos = opt.find('=');
        if (equals_pos != string::npos && equals_pos > 0 && opt.length() > equals_pos + 1)
        {
            string opt_name = opt.substr(0, equals_pos);
            mxb::trim(opt_name);
            string opt_value = opt.substr(equals_pos + 1);
            mxb::trim(opt_value);
            params_out->set(opt_name, opt_value);
        }
        else
        {
            MXB_ERROR("Invalid authenticator option setting: %s", opt.c_str());
            error = true;
            break;
        }
    }
    return !error;
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
