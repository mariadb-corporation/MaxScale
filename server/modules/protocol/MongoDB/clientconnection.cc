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

#include "clientconnection.hh"
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "mxsmongodatabase.hh"
#include "config.hh"

using namespace std;
using namespace mxsmongo;

ClientConnection::ClientConnection(const Config* pConfig, MXS_SESSION* pSession, mxs::Component* pDownstream)
    : m_config(*pConfig)
    , m_session(*pSession)
    , m_session_data(*static_cast<MYSQL_session*>(pSession->protocol_data()))
    , m_mongo(this, pDownstream, pConfig)
{
}

ClientConnection::~ClientConnection()
{
}

bool ClientConnection::init_connection()
{
    // TODO: If we need to initially send something to the MongoDB client,
    // TODO: that should be done here.
    return true;
}

void ClientConnection::finish_connection()
{
    // TODO: Does something need to be cleaned up?
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
    DCB::ReadResult read_res = m_pDcb->read(mongo::HEADER_LEN, mongo::MAX_MSG_SIZE);
    if (!read_res)
    {
        return;
    }

    // Got the header, the full packet may be available.
    GWBUF* pBuffer = read_res.data.release();
    auto link_len = gwbuf_link_length(pBuffer);

    if (link_len < mongo::HEADER_LEN)
    {
        pBuffer = gwbuf_make_contiguous(pBuffer);
    }

    mongo::HEADER* pHeader = reinterpret_cast<mongo::HEADER*>(gwbuf_link_data(pBuffer));

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
        MXS_NOTICE("%d bytes received, still need %d bytes for the package.",
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
    mxb_assert(m_mongo.is_pending());

    return m_mongo.clientReply(pMariaDB_response, m_pDcb);
}

json_t* ClientConnection::diagnostics() const
{
    mxb_assert(!true);
    return nullptr;
}

void ClientConnection::set_dcb(DCB* dcb)
{
    mxb_assert(!m_pDcb);
    m_pDcb = dcb;
}

bool ClientConnection::is_movable() const
{
    mxb_assert(!true);
    return true; // Ok?
}

bool ClientConnection::setup_session()
{
    bool rv = false;

    mxb_assert(!is_ready());

    m_session_data.user = m_config.user;
    m_session.set_user(m_session_data.user);
    m_session_data.db = "";
    m_session_data.current_db = "";
    m_session_data.plugin = "mysql_native_password";

    if (!m_config.password.empty())
    {
        const uint8_t* pPassword = reinterpret_cast<const uint8_t*>(m_config.password.data());
        auto nPassword = m_config.password.length();
        uint8_t auth_token[SHA_DIGEST_LENGTH];

        gw_sha1_str(pPassword, nPassword, auth_token);

        // This is actually not needed, but as MariaDBAuth (of which we only use the backend part)
        // uses both of these, better to assign anyway in case someone assumes something to be present.
        m_session_data.auth_token.assign(auth_token, auth_token + SHA_DIGEST_LENGTH);

        // This will be used when authenticating with the backend.
        m_session_data.auth_token_phase2.assign(auth_token, auth_token + SHA_DIGEST_LENGTH);
    }

    const auto& authenticators = m_session.listener_data()->m_authenticators;
    mxb_assert(authenticators.size() == 1);
    auto* pAuthenticator = static_cast<mariadb::AuthenticatorModule*>(authenticators.front().get());

    m_session_data.m_current_authenticator = pAuthenticator;
    m_session_data.client_info.m_client_capabilities = CLIENT_LONG_FLAG
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
    m_session_data.client_info.m_extra_capabilities = MXS_MARIA_CAP_STMT_BULK_OPERATIONS;
    m_session_data.client_info.m_charset = 33; // UTF8

    return session_start(&m_session);
}

GWBUF* ClientConnection::handle_one_packet(GWBUF* pPacket)
{
    bool ready = true;
    GWBUF* pResponse = nullptr;

    if (!is_ready())
    {
        ready = setup_session();

        if (ready)
        {
            set_ready();
        }
        else
        {
            MXS_ERROR("Could not start session, closing client connection.");
            gwbuf_free(pPacket);
            m_session.kill();
        }
    }

    if (ready)
    {
        mxb_assert(gwbuf_is_contiguous(pPacket));
        mxb_assert(gwbuf_length(pPacket) >= mongo::HEADER_LEN);

        pResponse = m_mongo.handle_request(pPacket);
    }

    return pResponse;
}

int32_t ClientConnection::clientReply(GWBUF* pBuffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    int32_t rv = 0;

    if (m_mongo.is_pending())
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
            MXS_WARNING("Unexpected OK packet received when none was expected.");
        }
        else if (mxs_mysql_is_err_packet(pBuffer))
        {
            MXS_ERROR("Error received from backend, session is likely to be closed: %s",
                      mxs::extract_error(pBuffer).c_str());
        }
        else
        {
            MXS_WARNING("Unexpected response received.");
        }

        gwbuf_free(pBuffer);
    }

    return rv;
}
