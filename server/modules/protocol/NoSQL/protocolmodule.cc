/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "protocolmodule.hh"
#include <maxscale/cn_strings.hh>
#include <maxscale/listener.hh>
#include <maxscale/protocol/mariadb/backend_connection.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/service.hh>
#include "../MariaDB/user_data.hh"
#include "../MariaDB/protocol_module.hh"
#include "clientconnection.hh"
#include "nosqlcursor.hh"
#include "../../filter/cache/cachefilter.hh"

using namespace std;
namespace config = mxs::config;

ProtocolModule::ProtocolModule(std::string name, SERVICE* pService)
    : m_config(name, this)
    , m_service(*pService)
{
}

bool ProtocolModule::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool rv = false;

    if (m_config.authentication_shared)
    {
        m_sUm = nosql::UserManagerMariaDB::create(m_config.name(), &m_service, &m_config);
    }
    else
    {
        m_sUm = nosql::UserManagerSqlite3::create(m_config.name(), &m_service, &m_config);
    }

    if (m_sUm)
    {
        if (m_config.authentication_required && m_config.authorization_enabled)
        {
            m_sUm->ensure_initial_user();
        }

        nosql::NoSQLCursor::start_purging_idle_cursors(m_config.cursor_timeout);
        rv = true;
    }

    if (rv)
    {
        if (m_config.pInternal_cache)
        {
            MXB_NOTICE("Nosqlprotocol configured to use a cache.");

            mxs::ConfigParameters cache_config;

            if (auto it = nested_params.find("cache"); it != nested_params.end())
            {
                cache_config = it->second;
            }

            // Let's use a unique name, even though the filter will not end up
            // in the general book-keeping.
            string name("@@Cache-for-");
            name += m_config.name();

            m_sCache_filter.reset(CacheFilter::create(name.c_str()));

            rv = m_sCache_filter->getConfiguration().configure(cache_config);
        }
        else
        {
            MXB_INFO("Nosqlprotocol not configured to use a cache.");
        }
    }

    return rv;
}

// static
ProtocolModule* ProtocolModule::create(const std::string& name, mxs::Listener* pListener)
{
    return new ProtocolModule(name, pListener->service());
}

unique_ptr<mxs::ClientConnection>
ProtocolModule::create_client_protocol(MXS_SESSION* pSession, mxs::Component* pComponent)
{
    const auto& cnf = *pSession->service->config();
    unique_ptr<MYSQL_session> sSession_data(new MYSQL_session(cnf.max_sescmd_history,
                                                              cnf.prune_sescmd_history,
                                                              cnf.disable_sescmd_history));
    // TODO: Drop this, operate on whatever data is delivered to clientReply() and
    // TODO: send documents to the client in multiple packets.
    sSession_data->set_client_protocol_capabilities(RCAP_TYPE_RESULTSET_OUTPUT);
    pSession->set_protocol_data(std::move(sSession_data));

    Cache* pCache = m_sCache_filter ? &m_sCache_filter->cache() : nullptr;

    return unique_ptr<mxs::ClientConnection>(new ClientConnection(m_config,
                                                                  m_sUm.get(),
                                                                  pSession,
                                                                  pComponent,
                                                                  pCache));
}

unique_ptr<mxs::BackendConnection>
ProtocolModule::create_backend_protocol(MXS_SESSION* pSession,
                                        SERVER* pServer,
                                        mxs::Component* pComponent)
{
    return MariaDBBackendConnection::create(pSession, pComponent, *pServer);
}

string ProtocolModule::auth_default() const
{
    mxb_assert(!true);
    return "";
}

GWBUF ProtocolModule::make_error(int errnum, const std::string& sqlstate, const std::string& message) const
{
    return mariadb::create_error_packet(0, errnum, sqlstate.c_str(), message.c_str());
}

std::string_view ProtocolModule::get_sql(const GWBUF& packet) const
{
    // By the time this function may be called, 'packet' is a
    // MariaDB protocol packet, and not a NoSQL protocol packet.
    return mariadb::get_sql(packet);
}

std::string ProtocolModule::describe(const GWBUF& packet, int body_max_len) const
{
    // By the time this function may be called, 'packet' is a
    // MariaDB protocol packet, and not a NoSQL protocol packet.
    return MySQLProtocolModule::get_description(packet, body_max_len);
}

uint64_t ProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

string ProtocolModule::name() const
{
    return MXB_MODULE_NAME;
}

string ProtocolModule::protocol_name() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

unique_ptr<mxs::UserAccountManager> ProtocolModule::create_user_data_manager()
{
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager());
}

ProtocolModule::AuthenticatorList ProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    // TODO: For now we just load the default MariaDB authenticator.

    AuthenticatorList authenticators;

    string auth_name = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    mxs::ConfigParameters auth_config;
    unique_ptr<mxs::AuthenticatorModule> sAuth_module = mxs::authenticator_init(auth_name, &auth_config);

    if (sAuth_module)
    {
        mxb_assert(strcasecmp(MXS_MARIADB_PROTOCOL_NAME,
                              sAuth_module->supported_protocol().c_str()) == 0);

        authenticators.push_back(move(sAuth_module));
    }
    else
    {
        MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_name.c_str());
    }

    return authenticators;
}
