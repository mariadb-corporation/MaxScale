/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "protocolmodule.hh"
#include <maxscale/protocol/mariadb/backend_connection.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "../MariaDB/user_data.hh"
#include "clientconnection.hh"

using namespace std;

//static
ProtocolModule* ProtocolModule::create()
{
    TRACE();
    return new ProtocolModule;
}

unique_ptr<mxs::ClientConnection>
ProtocolModule::create_client_protocol(MXS_SESSION* pSession, mxs::Component* pComponent)
{
    TRACE();

    unique_ptr<MYSQL_session> sSession_data(new MYSQL_session());
    pSession->set_protocol_data(std::move(sSession_data));

    return unique_ptr<mxs::ClientConnection>(new ClientConnection(pSession, pComponent));
}

unique_ptr<mxs::BackendConnection>
ProtocolModule::create_backend_protocol(MXS_SESSION* pSession,
                                        SERVER* pServer,
                                        mxs::Component* pComponent)
{
    TRACE();
    return MariaDBBackendConnection::create(pSession, pComponent, *pServer);
}

string ProtocolModule::auth_default() const
{
    TRACE();
    mxb_assert(!true);
    return "";
}

GWBUF* ProtocolModule::reject(const string& host)
{
    TRACE();
    mxb_assert(!true);
    return nullptr;
}

uint64_t ProtocolModule::capabilities() const
{
    TRACE();
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

string ProtocolModule::name() const
{
    TRACE();
    return MXS_MODULE_NAME;
}

unique_ptr<mxs::UserAccountManager> ProtocolModule::create_user_data_manager()
{
    TRACE();
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager());
}

ProtocolModule::AuthenticatorList ProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    TRACE();

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
        MXS_ERROR("Failed to initialize authenticator module '%s'.", auth_name.c_str());
    }

    return authenticators;
}
