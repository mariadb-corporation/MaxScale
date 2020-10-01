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

#include "clientconnection.hh"
#include <maxscale/dcb.hh>
#include <maxscale/session.hh>
#include "mxsmongo.hh"

using namespace std;

namespace mxsmongo
{

ClientConnection::ClientConnection(MXS_SESSION* pSession, mxs::Component* pComponent)
    : m_state(State::CONNECTED)
    , m_session(*pSession)
    , m_component(*pComponent)
{
    TRACE();
}

ClientConnection::~ClientConnection()
{
    TRACE();
}

bool ClientConnection::init_connection()
{
    TRACE();
    // TODO: If we need to initially send something to the MongoDB client,
    // TODO: that should be done here.
    return true;
}

void ClientConnection::finish_connection()
{
    TRACE();

    // TODO: Does something need to be cleaned up?
}

ClientDCB* ClientConnection::dcb()
{
    TRACE();
    return static_cast<ClientDCB*>(m_pDcb);
}

const ClientDCB* ClientConnection::dcb() const
{
    TRACE();
    return static_cast<const ClientDCB*>(m_pDcb);
}

void ClientConnection::ready_for_reading(DCB* dcb)
{
    TRACE();

    GWBUF* pBuffer = nullptr;
    int buffer_len = m_pDcb->read(&pBuffer, MONGOC_DEFAULT_MAX_MSG_SIZE);

    if (buffer_len < 0)
    {
        return;
    }

    if (buffer_len >= MXSMONGO_HEADER_LEN)
    {
        // Got the header, the full packet may be available.

        auto link_len = gwbuf_link_length(pBuffer);

        if (link_len < MXSMONGO_HEADER_LEN)
        {
            pBuffer = gwbuf_make_contiguous(pBuffer);
        }

        mongoc_rpc_header_t* pHeader = reinterpret_cast<mongoc_rpc_header_t*>(gwbuf_link_data(pBuffer));

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
                auto* pPacket = gwbuf_split(&pBuffer, pHeader->msg_len);
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
    else if (buffer_len > 0)
    {
        // Not enough data to do anything at all. Save and wait for more.
        m_pDcb->readq_prepend(pBuffer);
    }
    else
    {
        // No data, can this happen? In MariaDB this may happen due to manually triggered reads.
    }
}

void ClientConnection::write_ready(DCB* pDcb)
{
    TRACE();
    mxb_assert(m_pDcb == pDcb);
    mxb_assert(m_pDcb->state() != DCB::State::DISCONNECTED);

    if (m_pDcb->state() != DCB::State::DISCONNECTED)
    {
        // TODO: Probably some state management will be needed.
        m_pDcb->writeq_drain();
    }
}

void ClientConnection::error(DCB* dcb)
{
    TRACE();
    mxb_assert(!true);
}

void ClientConnection::hangup(DCB* pDcb)
{
    TRACE();
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
}

int32_t ClientConnection::write(GWBUF* buffer)
{
    TRACE();
    mxb_assert(!true);
    return 0;
}

json_t* ClientConnection::diagnostics() const
{
    TRACE();
    mxb_assert(!true);
    return nullptr;
}

void ClientConnection::set_dcb(DCB* dcb)
{
    TRACE();
    mxb_assert(!m_pDcb);
    m_pDcb = dcb;
}

bool ClientConnection::is_movable() const
{
    TRACE();
    mxb_assert(!true);
    return true; // Ok?
}

GWBUF* ClientConnection::handle_one_packet(GWBUF* pPacket)
{
    GWBUF* pResponse = nullptr;

    mongoc_rpc_header_t* pHeader = reinterpret_cast<mongoc_rpc_header_t*>(gwbuf_link_data(pPacket));

    mxb_assert(gwbuf_is_contiguous(pPacket));
    mxb_assert(gwbuf_length(pPacket) >= MXSMONGO_HEADER_LEN);
    mxb_assert(pHeader->msg_len == (int)gwbuf_length(pPacket));

    switch (pHeader->opcode)
    {
    case MONGOC_OPCODE_COMPRESSED:
    case MONGOC_OPCODE_DELETE:
    case MONGOC_OPCODE_GET_MORE:
    case MONGOC_OPCODE_INSERT:
    case MONGOC_OPCODE_KILL_CURSORS:
    case MONGOC_OPCODE_MSG:
    case MONGOC_OPCODE_REPLY:
    case MONGOC_OPCODE_UPDATE:
        MXS_ERROR("Packet %s not handled (yet).", opcode_to_string(pHeader->opcode));
        mxb_assert(!true);
        break;

    case MONGOC_OPCODE_QUERY:
        pResponse = handle_packet_query(pPacket);
        break;

    default:
        MXS_ERROR("Unknown opcode %d.", pHeader->opcode);
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* ClientConnection::handle_packet_query(GWBUF* pPacket)
{
    mxb_assert(!true);
    return nullptr;
}

}
