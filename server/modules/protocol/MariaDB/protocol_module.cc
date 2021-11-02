/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
#include <maxscale/built_in_modules.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/listener.hh>
#include <maxscale/modutil.hh>
#include "user_data.hh"

using std::string;

MySQLProtocolModule* MySQLProtocolModule::create(const mxs::ConfigParameters& params)
{
    MySQLProtocolModule* self = nullptr;

    if (params.empty())
    {
        self = new MySQLProtocolModule();
    }
    else
    {
        MXS_ERROR("MariaDB protocol does not support any parameters.");
    }

    return self;
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

        auto def_sqlmode = session->listener_data()->m_default_sql_mode;
        mdb_session->is_autocommit = (def_sqlmode != QC_SQL_MODE_ORACLE);

        mdb_session->remote = session->client_remote();

        session->set_protocol_data(std::move(mdb_session));

        new_client_proto = std::make_unique<MariaDBClientConnection>(session, component);
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

bool MySQLProtocolModule::read_authentication_options(mxs::ConfigParameters* params)
{
    bool error = false;
    if (!params->empty())
    {
        // Read any values recognized by the protocol itself and remove them. The leftovers are given to
        // authenticators.
        const string opt_cachedir = "cache_dir";
        const string opt_inject = "inject_service_user";
        const string opt_skip_auth = "skip_authentication";
        const string opt_match_host = "match_host";
        const string opt_lower_case = "lower_case_table_names";
        const char option_is_ignored[] = "Authenticator option '%s' is no longer supported and "
                                         "its value is ignored.";

        if (params->contains(opt_cachedir))
        {
            MXB_WARNING(option_is_ignored, opt_cachedir.c_str());
            params->remove(opt_cachedir);
        }
        if (params->contains(opt_inject))
        {
            MXB_WARNING(option_is_ignored, opt_inject.c_str());
            params->remove(opt_inject);
        }
        if (params->contains(opt_skip_auth))
        {
            m_user_search_settings.check_password = !params->get_bool(opt_skip_auth);
            params->remove(opt_skip_auth);
        }
        if (params->contains(opt_match_host))
        {
            m_user_search_settings.match_host_pattern = params->get_bool(opt_match_host);
            params->remove(opt_match_host);
        }

        if (params->contains(opt_lower_case))
        {
            // To match the server, the allowed values should be 0, 1 or 2. For backwards compatibility,
            // "true" and "false" should also apply, with "true" mapping to 1 and "false" to 0.
            long lower_case_mode = -1;
            auto lower_case_mode_str = params->get_string(opt_lower_case);
            if (lower_case_mode_str == "true")
            {
                lower_case_mode = 1;
            }
            else if (lower_case_mode_str == "false")
            {
                lower_case_mode = 0;
            }
            else if (!mxb::get_long(lower_case_mode_str, 10, &lower_case_mode))
            {
                lower_case_mode = -1;
            }

            switch (lower_case_mode)
            {
            case 0:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::CASE_SENSITIVE;
                break;

            case 1:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::LOWER_CASE;
                break;

            case 2:
                m_user_search_settings.db_name_cmp_mode = UserDatabase::DBNameCmpMode::CASE_INSENSITIVE;
                break;

            default:
                error = true;
                MXB_ERROR("Invalid authenticator option value for '%s': '%s'. Expected 0, 1, or 2.",
                          opt_lower_case.c_str(), lower_case_mode_str.c_str());
            }
            params->remove(opt_lower_case);
        }

        if (!read_custom_user_options(*params))
        {
            error = true;
        }
    }
    return !error;
}

void MySQLProtocolModule::user_account_manager_created(mxs::UserAccountManager& manager)
{
    if (m_custom_entry)
    {
        auto& maria_manager = static_cast<MariaDBUserManager&>(manager);
        maria_manager.add_custom_user(*m_custom_entry);
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

    // Contains protocol-level authentication options + plugin options.
    mxs::ConfigParameters auth_config;
    // Parse all options. Then read in authentication settings which affect the entire listener.
    if (!parse_auth_options(auth_opts, &auth_config) || !read_authentication_options(&auth_config))
    {
        return {};      // error
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

bool MySQLProtocolModule::read_custom_user_options(mxs::ConfigParameters& params)
{
    const string opt_custom_user_un = "custom_user_name";
    const string opt_custom_user_pw = "custom_user_pw";
    const string opt_custom_user_host = "custom_user_host";
    const string opt_custom_user_plugin = "custom_user_plugin";
    const string opt_custom_user_authstr = "custom_user_auth_str";

    string name, pw, host, plugin, authstr;
    auto read_str = [&params](const string& opt_name, string* target) {
            if (params.contains(opt_name))
            {
                *target = params.get_string(opt_name);
                params.remove(opt_name);
            }
        };

    read_str(opt_custom_user_un, &name);
    read_str(opt_custom_user_pw, &pw);
    read_str(opt_custom_user_host, &host);
    read_str(opt_custom_user_plugin, &plugin);
    read_str(opt_custom_user_authstr, &authstr);

    bool rval = true;
    // If using a custom user, at least host must be given. Other three settings default to empty.
    // Not giving host while giving any other setting is an error.
    if (host.empty())
    {
        auto check_is_empty = [&](const string& opt, const string& val) {
                if (!val.empty())
                {
                    MXB_ERROR("'%s' is not set when '%s' is set. Set '%s' when using custom user.",
                              opt_custom_user_host.c_str(), opt.c_str(), opt_custom_user_host.c_str());
                    rval = false;
                }
            };

        check_is_empty(opt_custom_user_un, name);
        check_is_empty(opt_custom_user_pw, pw);
        check_is_empty(opt_custom_user_plugin, plugin);
        check_is_empty(opt_custom_user_authstr, authstr);
    }
    else
    {
        auto entry = std::make_unique<mariadb::UserEntry>();
        entry->username = name;
        entry->password = pw;
        entry->host_pattern = host;
        entry->plugin = plugin;
        entry->auth_string = authstr;
        entry->global_db_priv = true;
        entry->proxy_priv = true;
        entry->super_priv = true;
        m_custom_entry = move(entry);
    }
    return rval;
}

namespace
{
int module_init()
{
    return MariaDBClientConnection::module_init() ? 0 : 1;
}
}

/**
 * Get MariaDBProtocol module info
 *
 * @return The module object
 */
MXS_MODULE* mariadbprotocol_info()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::PROTOCOL,
        mxs::ModuleStatus::GA,
        MXS_PROTOCOL_VERSION,
        "The client to MaxScale MySQL protocol implementation",
        "V1.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ProtocolApiGenerator<MySQLProtocolModule>::s_api,
        module_init,
        nullptr,
        nullptr,
        nullptr,
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
