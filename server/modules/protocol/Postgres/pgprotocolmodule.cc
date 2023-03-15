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
#include <maxscale/listener.hh>
#include "pgauthenticatormodule.hh"
#include "pgclientconnection.hh"
#include "pgbackendconnection.hh"
#include "pgprotocoldata.hh"
#include "postgresprotocol.hh"


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
    auto sProtocol_data = std::make_unique<PgProtocolData>();

    pSession->set_protocol_data(std::move(sProtocol_data));

    auto sClient_connection = std::make_unique<PgClientConnection>(pSession, pComponent);

    return sClient_connection;
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
    // The field type explanations are here
    // https://www.postgresql.org/docs/current/protocol-error-fields.html
    auto old_severity = mxb::cat("S", "ERROR");
    auto new_severity = mxb::cat("V", "ERROR");
    auto code = mxb::cat("C", sqlstate);
    auto message = mxb::cat("M", msg);

    GWBUF buf{pg::HEADER_LEN
              + old_severity.size() + 1
              + new_severity.size() + 1
              + code.size() + 1
              + message.size() + 1};
    auto ptr = buf.data();

    *ptr++ = 'E';
    ptr += pg::set_uint32(ptr, buf.length() - 1);
    ptr += pg::set_string(ptr, old_severity);
    ptr += pg::set_string(ptr, new_severity);
    ptr += pg::set_string(ptr, code);
    ptr += pg::set_string(ptr, message);

    return buf;
}

std::string PgProtocolModule::describe(const GWBUF& packet, int body_max_len) const
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return std::string {};
}

uint64_t PgProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTH_MODULES;
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
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return nullptr;
}

PgProtocolModule::AuthenticatorList
PgProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    MXB_ALERT("Not implemented yet: %s", __func__);

    AuthenticatorList authenticators;

    std::unique_ptr<PgAuthenticatorModule> sAuthenticator(new PgAuthenticatorModule);

    authenticators.push_back(std::move(sAuthenticator));

    return authenticators;
}

bool PgProtocolModule::post_configure()
{
    MXB_ALERT("Not implemented yet: %s", __func__);
    mxb_assert(!true);

    return false;
}
