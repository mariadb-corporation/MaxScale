/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
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
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "nosqldatabase.hh"
#include "config.hh"

using namespace std;
using namespace nosql;

ClientConnection::ClientConnection(const GlobalConfig& config,
                                   nosql::UserManager* pUm,
                                   MXS_SESSION* pSession,
                                   mxs::Component* pDownstream)
    : m_config(config)
    , m_session(*pSession)
    , m_session_data(*static_cast<MYSQL_session*>(pSession->protocol_data()))
    , m_nosql(pSession, this, pDownstream, &m_config, pUm)
{
    prepare_session();
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

void ClientConnection::ready_for_reading(DCB* dcb)
{
    if (m_session.listener_data()->m_ssl.config().enabled
        && dcb->ssl_state() == DCB::SSLState::HANDSHAKE_UNKNOWN)
    {
        if (dcb->ssl_handshake() != 1)
        {
            // The handshake is in progress or it failed, in both cases we just return.
            return;
        }
    }

    DCB::ReadResult read_res = m_pDcb->read(protocol::HEADER_LEN, protocol::MAX_MSG_SIZE);
    if (!read_res)
    {
        return;
    }

    // Got the header, the full packet may be available.
    GWBUF* pBuffer = read_res.data.release();
    auto link_len = gwbuf_link_length(pBuffer);

    if (link_len < protocol::HEADER_LEN)
    {
        pBuffer = gwbuf_make_contiguous(pBuffer);
    }

    protocol::HEADER* pHeader = reinterpret_cast<protocol::HEADER*>(gwbuf_link_data(pBuffer));

    int buffer_len = gwbuf_length(pBuffer);
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
            pPacket = gwbuf_split(&pBuffer, pHeader->msg_len);
            mxb_assert((int)gwbuf_length(pPacket) == pHeader->msg_len);

            m_pDcb->readq_prepend(pBuffer);
            m_pDcb->trigger_read_event();
        }

        // We are not going to be able to parse bson unless the data is
        // contiguous.
        if (!gwbuf_is_contiguous(pPacket))
        {
            pPacket = gwbuf_make_contiguous(pPacket);
        }

        GWBUF* pResponse = handle_one_packet(pPacket);

        if (pResponse)
        {
            m_pDcb->writeq_append(pResponse);
        }
    }
    else
    {
        MXB_INFO("%d bytes received, still need %d bytes for the package.",
                 buffer_len, pHeader->msg_len - buffer_len);
        m_pDcb->readq_prepend(pBuffer);
    }
}

void ClientConnection::write_ready(DCB* pDcb)
{
    mxb_assert(m_pDcb == pDcb);
    mxb_assert(m_pDcb->state() != DCB::State::DISCONNECTED);

    if (m_pDcb->state() != DCB::State::DISCONNECTED)
    {
        // TODO: Probably some state management will be needed.
        m_pDcb->writeq_drain();
    }
}

void ClientConnection::error(DCB* pDcb)
{
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
}

void ClientConnection::hangup(DCB* pDcb)
{
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
}

const char* dbg_decode_response(GWBUF* pPacket);

int32_t ClientConnection::write(GWBUF* pMariaDB_response)
{
    int32_t rv = 1;

    if (m_nosql.is_busy())
    {
        rv = m_nosql.clientReply(pMariaDB_response, m_pDcb);
    }
    else
    {
        ComResponse response(pMariaDB_response);

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXS_ERROR("OK packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::EOF_PACKET:
            MXS_ERROR("EOF packet received from server when no request was in progress, ignoring.");
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                switch (err.code())
                {
                case ER_ACCESS_DENIED_ERROR:
                case ER_CONNECTION_KILLED:
                    // Errors should have been logged already.
                    MXS_INFO("ERR packet received from server when no request was in progress: (%d) %s",
                             err.code(), err.message().c_str());
                    break;

                default:
                    MXS_ERROR("ERR packet received from server when no request was in progress: (%d) %s",
                              err.code(), err.message().c_str());
                }
            }
            break;

        default:
            MXS_ERROR("Unexpected %d bytes received from server when no request was in progress, ignoring.",
                      gwbuf_length(pMariaDB_response));
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

bool ClientConnection::setup_session(const string& user, const string& password)
{
    mxb_assert(!is_ready());

    auto& auth_data = *m_session_data.auth_data;
    auth_data.user = user;
    m_session.set_user(auth_data.user);

    if (!password.empty())
    {
        const uint8_t* pPassword = reinterpret_cast<const uint8_t*>(password.data());
        auto nPassword = m_config.password.length();
        uint8_t auth_token[SHA_DIGEST_LENGTH];

        gw_sha1_str(pPassword, nPassword, auth_token);

        // This will be used when authenticating with the backend.
        auth_data.backend_token.assign(auth_token, auth_token + SHA_DIGEST_LENGTH);
    }

    bool ready = m_session.start();

    if (ready)
    {
        m_state = READY;
    }

    return ready;
}

void ClientConnection::prepare_session()
{
    mxb_assert(!is_ready());

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
    GWBUF* pStmt = modutil_create_query("set names utf8mb4 collate utf8mb4_bin");
    gwbuf_set_id(pStmt, id);

    m_session_data.history.push_back(mxs::Buffer(pStmt));
    m_session_data.history_responses.insert(std::make_pair(id, true));
}

GWBUF* ClientConnection::handle_one_packet(GWBUF* pPacket)
{
    mxb_assert(gwbuf_is_contiguous(pPacket));
    mxb_assert(gwbuf_length(pPacket) >= protocol::HEADER_LEN);

    return m_nosql.handle_request(pPacket);
}

bool ClientConnection::clientReply(GWBUF* pBuffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    int32_t rv = 0;

    if (m_nosql.is_busy())
    {
        rv = write(pBuffer);
    }
    else
    {
        // If there is not a pending command, this is likely to be a server hangup
        // caused e.g. by an authentication error.
        // TODO: However, currently 'reply' does not contain anything, but the information
        // TODO: has to be digged out from 'pBuffer'.

        if (mxs_mysql_is_ok_packet(pBuffer))
        {
            MXB_WARNING("Unexpected OK packet received when none was expected.");
        }
        else if (mxs_mysql_is_err_packet(pBuffer))
        {
            MXB_ERROR("Error received from backend, session is likely to be closed: %s",
                      mxs::extract_error(pBuffer).c_str());
        }
        else
        {
            MXB_WARNING("Unexpected response received.");
        }

        gwbuf_free(pBuffer);
    }

    return rv;
}
