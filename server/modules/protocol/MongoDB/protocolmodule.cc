/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "protocolmodule.hh"
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
    return unique_ptr<mxs::ClientConnection>(new ClientConnection(pSession, pComponent));
}

unique_ptr<mxs::BackendConnection>
ProtocolModule::create_backend_protocol(MXS_SESSION* pSession,
                                        SERVER* pServer,
                                        mxs::Component* pComponent)
{
    TRACE();
    mxb_assert(!true);
    return nullptr;
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
    return 0;
}

string ProtocolModule::name() const
{
    TRACE();
    return MXS_MODULE_NAME;
}

unique_ptr<mxs::UserAccountManager> ProtocolModule::create_user_data_manager()
{
    TRACE();
    mxb_assert(!true);
    return nullptr;
}

ProtocolModule::AuthenticatorList ProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    TRACE();
    mxb_assert(!true);
    return {};
}
