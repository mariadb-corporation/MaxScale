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

namespace mxsmongo
{

//static
ProtocolModule* ProtocolModule::create()
{
    return new ProtocolModule;
}

std::unique_ptr<mxs::ClientConnection>
ProtocolModule::create_client_protocol(MXS_SESSION* pSession, mxs::Component* pComponent)
{
    mxb_assert(!true);
    return nullptr;
}

std::unique_ptr<mxs::BackendConnection>
ProtocolModule::create_backend_protocol(MXS_SESSION* pSession, SERVER* pServer, mxs::Component* pComponent)
{
    mxb_assert(!true);
    return nullptr;
}

std::string ProtocolModule::auth_default() const
{
    mxb_assert(!true);
    return "";
}

GWBUF* ProtocolModule::reject(const std::string& host)
{
    mxb_assert(!true);
    return nullptr;
}

uint64_t ProtocolModule::capabilities() const
{
    mxb_assert(!true);
    return 0;
}

std::string ProtocolModule::name() const
{
    return MXS_MODULE_NAME;
}

std::unique_ptr<mxs::UserAccountManager> ProtocolModule::create_user_data_manager()
{
    mxb_assert(!true);
    return nullptr;
}

ProtocolModule::AuthenticatorList ProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    mxb_assert(!true);
    return {};
}

}
