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

void ClientConnection::error(DCB* pDcb)
{
    TRACE();
    mxb_assert(m_pDcb == pDcb);

    m_session.kill();
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

    mxb_assert(gwbuf_is_contiguous(pPacket));
    mxb_assert(gwbuf_length(pPacket) >= MXSMONGO_HEADER_LEN);

    mxsmongo::Packet packet(pPacket);

    mxb_assert(packet.msg_len() == (int)gwbuf_length(pPacket));

    switch (packet.opcode())
    {
    case MONGOC_OPCODE_COMPRESSED:
    case MONGOC_OPCODE_DELETE:
    case MONGOC_OPCODE_GET_MORE:
    case MONGOC_OPCODE_INSERT:
    case MONGOC_OPCODE_KILL_CURSORS:
    case MONGOC_OPCODE_REPLY:
    case MONGOC_OPCODE_UPDATE:
        MXS_ERROR("Packet %s not handled (yet).", mxsmongo::opcode_to_string(packet.opcode()));
        mxb_assert(!true);
        break;

    case MONGOC_OPCODE_MSG:
        pResponse = handle_msg(mxsmongo::Msg(packet));
        break;

    case MONGOC_OPCODE_QUERY:
        pResponse = handle_query(mxsmongo::Query(packet));
        break;

    default:
        MXS_ERROR("Unknown opcode %d.", packet.opcode());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* ClientConnection::handle_query(const mxsmongo::Query& req)
{
    GWBUF* pResponse = nullptr;

    // TODO: The assumption here is that the collection is the primary entity,
    // TODO: which decides what follows. Current assumption is that but for predefined
    // TODO: collections, a collection will be mapped to an existing table.

    if (req.collection() == "admin.$cmd")
    {
        switch (mxsmongo::get_command(req.query()))
        {
        case mxsmongo::Command::ISMASTER:
            pResponse = create_handshake_response(req);
            break;

        case mxsmongo::Command::UNKNOWN:
            MXS_ERROR("Query not recognized: %s", req.to_string().c_str());
            mxb_assert(!true);
        }
    }
    else
    {
        MXS_ERROR("Don't know what to do with collection '%s'.", req.zCollection());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* ClientConnection::handle_msg(const mxsmongo::Msg& req)
{
    GWBUF* pResponse = nullptr;

    for (const auto& doc : req.documents())
    {
        // TODO: Somewhat unclear in what order things should be done here.
        // TODO: As we know all clients will make regular ismaster queries,
        // TODO: we start by checking for that.

        switch (mxsmongo::get_command(doc))
        {
        case mxsmongo::Command::ISMASTER:
            {
                auto element = doc["$db"];

                if (element)
                {
                    if (element.type() == bsoncxx::type::k_utf8)
                    {
                        auto utf8 = element.get_utf8();

                        if (utf8.value == bsoncxx::stdx::string_view("admin"))
                        {
                            pResponse = create_handshake_response(req);
                        }
                        else
                        {
                            MXS_ERROR("Key '$db' found but value not 'admin' but '%s'.",
                                      utf8.value.data());
                            mxb_assert(!true);
                        }
                    }
                    else
                    {
                        MXS_ERROR("Key '$db' found, but value is not utf8.");
                        mxb_assert(!true);
                    }
                }
                else
                {
                    MXS_ERROR("Document did not contain the expected key '$db': %s",
                              req.to_string().c_str());
                    mxb_assert(!true);
                }

            }
            break;

        case mxsmongo::Command::UNKNOWN:
            MXS_ERROR("Dont know what to do:\n%s", req.to_string().c_str());
            mxb_assert(!true);
        }
    }

    return pResponse;
}

GWBUF* ClientConnection::create_handshake_response(const mxsmongo::Packet& req)
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
    pRes_hdr->response_to = req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

    pData += mxsmongo::set_byte4(pData, response_flags);
    pData += mxsmongo::set_byte8(pData, cursor_id);
    pData += mxsmongo::set_byte4(pData, starting_from);
    pData += mxsmongo::set_byte4(pData, number_returned);
    memcpy(pData, doc_view.data(), doc_view.length());

    return pResponse;
}

int32_t ClientConnection::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return write(buffer);
}
