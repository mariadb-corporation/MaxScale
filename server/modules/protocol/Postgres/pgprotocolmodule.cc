/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pgprotocolmodule.hh"

#include <maxscale/cn_strings.hh>
#include <maxscale/listener.hh>
#include <maxscale/service.hh>
#include "pgauthenticatormodule.hh"
#include "pgclientconnection.hh"
#include "pgbackendconnection.hh"
#include "pgprotocoldata.hh"
#include "postgresprotocol.hh"
#include "pgusermanager.hh"
#include "authenticators/password.hh"
#include "authenticators/trust.hh"
#include "authenticators/scram-sha-256.hh"

PgProtocolModule::PgProtocolModule(std::string name, SERVICE* pService)
    : m_config(name, this)
    , m_service(*pService)
{
}

// static
PgProtocolModule* PgProtocolModule::create(const std::string& name, mxs::Listener* pListener)
{
    return new PgProtocolModule(name, pListener->service());
}

std::unique_ptr<mxs::ClientConnection>
PgProtocolModule::create_client_protocol(MXS_SESSION* pSession, mxs::Component* pComponent)
{
    const auto& cnf = *pSession->service->config();
    auto sProtocol_data = std::make_unique<PgProtocolData>(cnf.max_sescmd_history,
                                                           cnf.prune_sescmd_history,
                                                           cnf.disable_sescmd_history);

    pSession->set_protocol_data(std::move(sProtocol_data));

    PgClientConnection::UserAuthSettings auth_settings;
    auth_settings.check_password = m_check_password;
    auth_settings.match_host_pattern = m_match_host_pattern;
    auth_settings.allow_root_user = pSession->service->config()->enable_root;

    return std::make_unique<PgClientConnection>(pSession, pComponent, auth_settings);
}

std::unique_ptr<mxs::BackendConnection>
PgProtocolModule::create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
{
    return std::make_unique<PgBackendConnection>(session, server, component);
}

std::string PgProtocolModule::auth_default() const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return "";
}

GWBUF PgProtocolModule::make_error(int errnum, const std::string& sqlstate, const std::string& msg) const
{
    return pg::make_error(pg::Severity::ERROR, sqlstate, msg);
}

std::string_view PgProtocolModule::get_sql(const GWBUF& packet) const
{
    return pg::get_sql(packet);
}

std::string PgProtocolModule::describe(const GWBUF& packet, int max_len) const
{
    return pg::describe(packet, max_len);
}

GWBUF PgProtocolModule::make_query(std::string_view sql) const
{
    return pg::create_query_packet(sql);
}

uint64_t PgProtocolModule::capabilities() const
{
    return CAP_BACKEND | CAP_AUTHDATA | CAP_AUTH_MODULES;
}

std::string PgProtocolModule::name() const
{
    return MXB_MODULE_NAME;
}

std::string PgProtocolModule::protocol_name() const
{
    return MXS_POSTGRESQL_PROTOCOL_NAME;
}

std::unique_ptr<mxs::UserAccountManager> PgProtocolModule::create_user_data_manager()
{
    return std::make_unique<PgUserManager>();
}

PgProtocolModule::AuthenticatorList
PgProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    // If no authenticator is set, the default authenticator will be loaded.
    auto auth_names = params.get_string(CN_AUTHENTICATOR);
    auto auth_opts = params.get_string(CN_AUTHENTICATOR_OPTIONS);

    const std::string auth_trust = "trust";
    const std::string auth_pw = "password";
    const std::string auth_scram = "scram-sha-256";
    if (auth_names.empty())
    {
        auth_names = auth_scram;
    }
    else if (auth_names == "all")
    {
        auth_names = mxb::cat(auth_trust, ",", auth_pw, ",", auth_scram);
    }

    AuthenticatorList authenticators;
    auto auth_names_list = mxb::strtok(auth_names, ",");
    bool error = false;

    for (auto iter = auth_names_list.begin(); iter != auth_names_list.end() && !error; ++iter)
    {
        std::string auth_name = *iter;
        mxb::trim(auth_name);
        if (!auth_name.empty())
        {
            std::unique_ptr<mxs::AuthenticatorModule> new_auth_module;
            if (auth_name == auth_pw)
            {
                new_auth_module = std::make_unique<PasswordAuthModule>();
            }
            else if (auth_name == auth_trust)
            {
                new_auth_module = std::make_unique<TrustAuthModule>();
            }
            else if (auth_name == auth_scram)
            {
                new_auth_module = std::make_unique<ScramAuthModule>();
            }

            if (new_auth_module)
            {
                mxb_assert(new_auth_module->supported_protocol() == MXS_POSTGRESQL_PROTOCOL_NAME);
                authenticators.push_back(std::move(new_auth_module));
            }
            else
            {
                MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_name.c_str());
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

    if (error)
    {
        authenticators.clear();
    }
    return authenticators;
}
