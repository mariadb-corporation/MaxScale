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
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include "mxsmongo.hh"

using namespace std;

ClientConnection::ClientConnection(MXS_SESSION* pSession, mxs::Component* pComponent)
    : m_session(*pSession)
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
    case MONGOC_OPCODE_REPLY:
    case MONGOC_OPCODE_UPDATE:
        MXS_ERROR("Packet %s not handled (yet).", mxsmongo::opcode_to_string(pHeader->opcode));
        mxb_assert(!true);
        break;

    case MONGOC_OPCODE_MSG:
        pResponse = handle_op_msg(pPacket);
        break;

    case MONGOC_OPCODE_QUERY:
        pResponse = handle_op_query(pPacket);
        break;

    default:
        MXS_ERROR("Unknown opcode %d.", pHeader->opcode);
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* ClientConnection::handle_op_query(GWBUF* pPacket)
{
    GWBUF* pResponse = nullptr;

    switch (m_state)
    {
    case State::CONNECTED:
    case State::HANDSHAKING:
        pResponse = handshake(pPacket);
        break;

    case State::READY:
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* ClientConnection::handle_op_msg(GWBUF* pPacket)
{
    GWBUF* pResponse = nullptr;

    uint8_t* pData = gwbuf_link_data(pPacket);
    mongoc_rpc_header_t* pHeader = reinterpret_cast<mongoc_rpc_header_t*>(pData);
    uint8_t* pEnd = pData + pHeader->msg_len;

    pData += MXSMONGO_HEADER_LEN;

    uint32_t flag_bits;
    pData += mxsmongo::get_byte4(pData, &flag_bits);

    bool checksum_present = mxsmongo::checksum_present(flag_bits);
    bool exhaust_allowed = mxsmongo::exhaust_allowed(flag_bits);
    bool more_to_come = mxsmongo::more_to_come(flag_bits);

    mxb_assert(!more_to_come); // We can't handle this yet.

    uint8_t* pSections_end = pEnd - (checksum_present ? sizeof(uint32_t) : 0);
    size_t sections_size = pSections_end - pData;

    while (pData < pSections_end)
    {
        uint8_t kind;
        pData += mxsmongo::get_byte1(pData, &kind);

        switch (kind)
        {
        case 0:
            // Body section encoded as a single BSON object.
            {
                uint32_t size;
                mxsmongo::get_byte4(pData, &size);
                bsoncxx::document::view doc(pData, size);
                pData += size;

                string s = bsoncxx::to_json(doc);

                MXS_NOTICE("DOC: %s", s.c_str());
            }
            break;

        case 1:
            mxb_assert(!true);
            break;

        default:
            mxb_assert(!true);
        }
    }

    mxb_assert(!true);

    return nullptr;
}

GWBUF* ClientConnection::handshake(GWBUF* pPacket)
{
    mxb_assert(gwbuf_is_contiguous(pPacket));

    // TODO: Actually do something with the provided data.

    auto link_len = gwbuf_link_length(pPacket);

    uint8_t* pData = gwbuf_link_data(pPacket);
    uint8_t* pEnd = pData + link_len;
    mongoc_rpc_header_t* pReq_hdr = reinterpret_cast<mongoc_rpc_header_t*>(gwbuf_link_data(pPacket));

    pData += MXSMONGO_HEADER_LEN;

    uint32_t flags;
    const char* zCollection;
    uint32_t nSkip;
    uint32_t nReturn;

    pData += mxsmongo::get_byte4(pData, &flags);
    pData += mxsmongo::get_zstring(pData, &zCollection);
    pData += mxsmongo::get_byte4(pData, &nSkip);
    pData += mxsmongo::get_byte4(pData, &nReturn);

    while (pData < pEnd)
    {
        size_t bson_len = mxsmongo::get_byte4(pData);
        bson_t bson;
        bson_init_static(&bson, pData, bson_len);

        string s = mxsmongo::to_string(bson);

        MXS_NOTICE("%s", s.c_str());

        pData += bson_len;
    }

    mxb_assert(pData == pEnd);

    return create_handshake_response(pReq_hdr);
}

GWBUF* ClientConnection::create_handshake_response(const mongoc_rpc_header_t* pReq_hdr)
{
    // TODO: Do not simply return a hardwired response.

    auto topologyVersion_builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value topologyVersion_value = topologyVersion_builder
        << "processId" << bsoncxx::oid()
        << "counter" << (int64_t)0
        << bsoncxx::builder::stream::finalize;

    auto builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value doc_value = builder
        << "ismaster" << true
        << "topologyVersion" << topologyVersion_value
        << "maxBsonObjectSize" << (int32_t)16777216
        << "maxMessageSizeBytes" << (int32_t)48000000
        << "maxWriteBatchSize" << (int32_t)100000
        << "localTime" << bsoncxx::types::b_date(std::chrono::system_clock::now())
        << "logicalSessionTimeoutMinutes" << (int32_t)30
        << "connectionId" << (int32_t)4
        << "minWireVersion" << (int32_t)0
        << "maxWireVersion" << (int32_t)9
        << "readOnly" << false
        << "ok" << (double)1
        << bsoncxx::builder::stream::finalize;

    auto doc_view = doc_value.view();
    size_t doc_len = doc_view.length();

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
    int64_t cursor_id = 0;
    int32_t starting_from = 0;
    int32_t number_returned = 1;

    size_t response_size = MXSMONGO_HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + doc_len;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_request_id++;
    pRes_hdr->response_to = pReq_hdr->request_id;
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

    pData += mxsmongo::set_byte4(pData, response_flags);
    pData += mxsmongo::set_byte8(pData, cursor_id);
    pData += mxsmongo::set_byte4(pData, starting_from);
    pData += mxsmongo::set_byte4(pData, number_returned);
    memcpy(pData, doc_view.data(), doc_view.length());

    return pResponse;
}
