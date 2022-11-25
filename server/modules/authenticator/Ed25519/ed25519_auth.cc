/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/module_names.hh>
#define MXB_MODULE_NAME "Ed25519Auth"

#include "ed25519_auth.hh"
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <openssl/rand.h>

using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;
using std::string;

namespace
{
const char client_plugin_name[] = "client_ed25519";
const std::unordered_set<std::string> plugins = {"ed25519"};    // Name of plugin in mysql.user
}

Ed25519AuthenticatorModule* Ed25519AuthenticatorModule::create(mxs::ConfigParameters* options)
{
    return new Ed25519AuthenticatorModule();
}

uint64_t Ed25519AuthenticatorModule::capabilities() const
{
    return 0;
}

std::string Ed25519AuthenticatorModule::supported_protocol() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

std::string Ed25519AuthenticatorModule::name() const
{
    return MXB_MODULE_NAME;
}

const std::unordered_set<std::string>& Ed25519AuthenticatorModule::supported_plugins() const
{
    return plugins;
}

mariadb::SClientAuth Ed25519AuthenticatorModule::create_client_authenticator()
{
    return std::make_unique<Ed25519ClientAuthenticator>();
}

mariadb::SBackendAuth
Ed25519AuthenticatorModule::create_backend_authenticator(mariadb::BackendAuthData& auth_data)
{
    return std::make_unique<Ed25519BackendAuthenticator>(auth_data);
}

mariadb::ClientAuthenticator::ExchRes
Ed25519ClientAuthenticator::exchange(GWBUF&& buffer, MYSQL_session* session, AuthenticationData& auth_data)
{
    using ExchRes = mariadb::ClientAuthenticator::ExchRes;
    ExchRes rval;

    switch (m_state)
    {
    case State::INIT:
        rval.packet = create_auth_change_packet();
        rval.status = ExchRes::Status::INCOMPLETE;
        m_state = State::AUTHSWITCH_SENT;
        break;

    case State::AUTHSWITCH_SENT:
        // Client should have responded with signed scramble.
        if (read_signature(session, buffer))
        {
            rval.status = ExchRes::Status::READY;
            m_state = State::CHECK_SIGNATURE;
        }
        else
        {
            m_state = State::DONE;
        }
        break;

    default:
        mxb_assert(!true);
        break;
    }

    return rval;
}

AuthRes Ed25519ClientAuthenticator::authenticate(MYSQL_session* session, AuthenticationData& auth_data)
{
    mxb_assert(m_state == State::CHECK_SIGNATURE);
    AuthRes rval;
    // Check client signature.
    m_state = State::DONE;
    return rval;
}

GWBUF Ed25519ClientAuthenticator::create_auth_change_packet()
{
    return GWBUF();
}

bool Ed25519ClientAuthenticator::read_signature(MYSQL_session* session, const GWBUF& buffer)
{
    return false;
}

Ed25519BackendAuthenticator::Ed25519BackendAuthenticator(mariadb::BackendAuthData& shared_data)
    : m_shared_data(shared_data)
{
}

mariadb::BackendAuthenticator::AuthRes Ed25519BackendAuthenticator::exchange(GWBUF&& input)
{
    const char* srv_name = m_shared_data.servername;

    auto header = mariadb::get_header(input.data());
    m_sequence = header.seq + 1;

    AuthRes rval;

    switch (m_state)
    {
    case State::EXPECT_AUTHSWITCH:
        // Parse packet, write signature.
        m_state = State::SIGNATURE_SENT;
        break;

    case State::SIGNATURE_SENT:
        // Server is sending more packets than expected. Error.
        MXB_ERROR("Server '%s' sent more packets than expected.", srv_name);
        break;

    case State::ERROR:
        // Should not get here.
        mxb_assert(!true);
        break;
    }

    if (!rval.success)
    {
        m_state = State::ERROR;
    }
    return rval;
}

extern "C"
{
MXS_MODULE* mxs_get_module_object()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::AUTHENTICATOR,
        mxs::ModuleStatus::GA,
        MXS_AUTHENTICATOR_VERSION,
        "Ed25519 authenticator. Backend authentication must be mapped.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::AuthenticatorApiGenerator<Ed25519AuthenticatorModule>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
    };

    return &info;
}
}
