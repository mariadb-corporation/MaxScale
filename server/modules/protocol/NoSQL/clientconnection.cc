/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "clientconnection.hh"
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <mysqld_error.h>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "nosqlconfig.hh"
#include "nosqldatabase.hh"

using namespace std;
using namespace nosql;

ClientConnection::ClientConnection(const Configuration& config,
                                   nosql::UserManager* pUm,
                                   MXS_SESSION* pSession,
                                   mxs::Component* pDownstream)
    : m_config(config)
    , m_session(*pSession)
    , m_session_data(*static_cast<MYSQL_session*>(pSession->protocol_data()))
    , m_nosql(pSession, this, pDownstream, &m_config, pUm)
    , m_ssl_required(m_session.listener_data()->m_ssl.config().enabled)
{
    prepare_session(m_config.user, m_config.password);
}

ClientConnection::~ClientConnection()
{
}

bool ClientConnection::init_connection()
{
    // Nothing need to be done.
    return true;
}

void ClientConnection::finish_connection()
{
    // Nothing need to be done.
}

ClientDCB* ClientConnection::dcb()
{
    return static_cast<ClientDCB*>(m_pDcb);
}

const ClientDCB* ClientConnection::dcb() const
{
    return static_cast<const ClientDCB*>(m_pDcb);
}

bool ClientConnection::ssl_is_ready()
{
    mxb_assert(m_ssl_required);

    return m_pDcb->ssl_state() == DCB::SSLState::ESTABLISHED ? true : setup_ssl();
}

bool ClientConnection::setup_ssl()
{
    auto state = m_pDcb->ssl_state();
    mxb_assert(state != DCB::SSLState::ESTABLISHED);

    if (state == DCB::SSLState::HANDSHAKE_UNKNOWN)
    {
        m_pDcb->set_ssl_state(DCB::SSLState::HANDSHAKE_REQUIRED);
    }

    auto rv = m_pDcb->ssl_handshake();

    const char* zRemote = m_pDcb->remote().c_str();
    const char* zService = m_session.service->name();

    if (rv == 1)
    {
        MXB_INFO("NoSQL client from '%s' connected to service '%s' with SSL.",
                 zRemote, zService);
    }
    else
    {
        if (rv < 0)
        {
            MXB_INFO("NoSQL client from '%s' failed to connect to service '%s' with SSL.",
                     zRemote, zService);
        }
        else
        {
            MXB_INFO("NoSQL client from '%s' is in progress of connecting to service '%s' with SSL.",
                     zRemote, zService);
        }
    }

    return rv == 1;
}

void ClientConnection::ready_for_reading(GWBUF* pBuffer)
{
    // Got the header, the full packet may be available.
    protocol::HEADER* pHeader = reinterpret_cast<protocol::HEADER*>(pBuffer->data());

    int buffer_len = pBuffer->length();
    if (buffer_len >= pHeader->msg_len)
    {
        // Ok, we have at least one full packet.

        GWBUF* pPacket = nullptr;

        if (buffer_len == pHeader->msg_len)
        {
            // Exactly one.
            pPacket = pBuffer;
        }
        else
        {
            // More than one.
            pPacket = mxs::gwbuf_to_gwbufptr(pBuffer->split(pHeader->msg_len));
            mxb_assert((int)pPacket->length() == pHeader->msg_len);

            m_pDcb->unread(mxs::gwbufptr_to_gwbuf(pBuffer));
            m_pDcb->trigger_read_event();
        }

        GWBUF* pResponse = handle_one_packet(pPacket);
        if (pResponse)
        {
            m_pDcb->writeq_append(mxs::gwbufptr_to_gwbuf(pResponse));
        }
    }
    else
    {
        MXB_INFO("%d bytes received, still need %d bytes for the package.",
                 buffer_len, pHeader->msg_len - buffer_len);
        m_pDcb->unread(mxs::gwbufptr_to_gwbuf(pBuffer));
    }
}

void ClientConnection::ready_for_reading(DCB* pDcb)
{
    mxb_assert(m_pDcb == pDcb);

    if (!m_ssl_required || ssl_is_ready())
    {
        auto [read_ok, buffer] = m_pDcb->read(protocol::HEADER_LEN, protocol::MAX_MSG_SIZE);

        if (!buffer.empty())
        {
            ready_for_reading(new GWBUF(move(buffer)));
        }
    }
}

void ClientConnection::error(DCB* pDcb, const char* errmsg)
{
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
}

const char* dbg_decode_response(GWBUF* pPacket);

bool ClientConnection::write(GWBUF&& buffer)
{
    GWBUF* pMariaDB_response = mxs::gwbuf_to_gwbufptr(std::move(buffer));
    bool rv = true;

    if (m_nosql.is_busy())
    {
        rv = m_nosql.clientReply(mxs::gwbufptr_to_gwbuf(pMariaDB_response), m_pDcb);
    }
    else
    {
        ComResponse response(pMariaDB_response);

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXB_ERROR("OK packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::EOF_PACKET:
            MXB_ERROR("EOF packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_ACCESS_DENIED_ERROR:
                case ER_CONNECTION_KILLED:
                    // Errors should have been logged already.
                    MXB_INFO("ERR packet received from server when no request was in progress: (%d) %s",
                             err.code(), err.message().c_str());
                    break;

                default:
                    MXB_ERROR("ERR packet received from server when no request was in progress: (%d) %s",
                              err.code(), err.message().c_str());
                }
            }
            break;

        default:
            MXB_ERROR("Unexpected %lu bytes received from server when no request was in progress, ignoring.",
                      pMariaDB_response->length());
        }

        gwbuf_free(pMariaDB_response);
    }

    return rv;
}

json_t* ClientConnection::diagnostics() const
{
    return nullptr;
}

void ClientConnection::set_dcb(DCB* dcb)
{
    mxb_assert(!m_pDcb);
    m_pDcb = dcb;
}

bool ClientConnection::is_movable() const
{
    return true;
}

bool ClientConnection::is_idle() const
{
    return !m_nosql.is_busy();
}

size_t ClientConnection::sizeof_buffers() const
{
    return m_pDcb ? m_pDcb->runtime_size() : 0;
}

mxs::Parser* ClientConnection::parser()
{
    return &MariaDBParser::get();
}

void ClientConnection::setup_session(const string& user, const vector<uint8_t>& password)
{
    auto& auth_data = *m_session_data.auth_data;
    auth_data.user = user;
    m_session.set_user(auth_data.user);

    if (!password.empty())
    {
        // This will be used when authenticating with the backend.
        auth_data.backend_token = password;
    }
    else
    {
        auth_data.backend_token.clear();
    }
}

void ClientConnection::prepare_session(const string& user, const vector<uint8_t>& password)
{
    m_session_data.auth_data = std::make_unique<mariadb::AuthenticationData>();
    auto& auth_data = *m_session_data.auth_data;
    auth_data.default_db = "";
    auth_data.plugin = "mysql_native_password";

    const auto& authenticators = m_session.listener_data()->m_authenticators;
    mxb_assert(authenticators.size() == 1);
    auto* pAuthenticator = static_cast<mariadb::AuthenticatorModule*>(authenticators.front().get());

    auth_data.client_auth_module = pAuthenticator;
    auth_data.be_auth_module = pAuthenticator;
    m_session_data.client_caps.basic_capabilities = CLIENT_LONG_FLAG
        | CLIENT_LOCAL_FILES
        | CLIENT_PROTOCOL_41
        | CLIENT_INTERACTIVE
        | CLIENT_TRANSACTIONS
        | CLIENT_SECURE_CONNECTION
        | CLIENT_MULTI_STATEMENTS
        | CLIENT_MULTI_RESULTS
        | CLIENT_PS_MULTI_RESULTS
        | CLIENT_PLUGIN_AUTH
        | CLIENT_SESSION_TRACKING
        | CLIENT_PROGRESS;
    m_session_data.client_caps.ext_capabilities = MXS_MARIA_CAP_STMT_BULK_OPERATIONS;
    auth_data.collation = 33;       // UTF8

    // The statement is injected into the session history before the session
    // is started. That way it will be executed on all servers, irrespective
    // of when a connection to a particular server is created.
    uint32_t id = 1;
    GWBUF stmt = mariadb::create_query("set names utf8mb4 collate utf8mb4_bin");
    stmt.set_id(id);

    m_session_data.history().add(move(stmt), true);

    setup_session(user, password);
}

GWBUF* ClientConnection::handle_one_packet(GWBUF* pPacket)
{
    mxb_assert(pPacket->length() >= protocol::HEADER_LEN);

    return m_nosql.handle_request(pPacket);
}

bool ClientConnection::clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    int32_t rv = 0;

    if (m_nosql.is_busy())
    {
        rv = write(std::move(buffer));
    }
    else
    {
        GWBUF* pBuffer = mxs::gwbuf_to_gwbufptr(std::move(buffer));

        // If there is not a pending command, this is likely to be a server hangup
        // caused e.g. by an authentication error.
        // TODO: However, currently 'reply' does not contain anything, but the information
        // TODO: has to be digged out from 'pBuffer'.

        if (mxs_mysql_is_ok_packet(*pBuffer))
        {
            MXB_WARNING("Unexpected OK packet received when none was expected.");
        }
        else if (mxs_mysql_is_err_packet(*pBuffer))
        {
            MXB_ERROR("Error received from backend, session is likely to be closed: %s",
                      mariadb::extract_error(pBuffer).c_str());
        }
        else
        {
            MXB_WARNING("Unexpected response received.");
        }

        gwbuf_free(pBuffer);
    }

    return rv;
}
