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

#include "mxsmongodatabase.hh"
#include <bsoncxx/builder/stream/document.hpp>

using namespace std;

mxsmongo::Database::Database(const std::string& name, Mongo::Context* pContext)
    : m_name(name)
    , m_context(*pContext)
{
}

mxsmongo::Database::~Database()
{
    mxb_assert(m_state == READY);
}

//static
unique_ptr<mxsmongo::Database> mxsmongo::Database::create(const std::string& name, Mongo::Context* pContext)
{
    return unique_ptr<Database>(new Database(name, pContext));
}

GWBUF* mxsmongo::Database::handle_query(const mxsmongo::Query& req, mxs::Component& downstream)
{
    mxb_assert(is_ready());

    GWBUF* pResponse = nullptr;

    switch (mxsmongo::get_command(req.query()))
    {
    case mxsmongo::Command::ISMASTER:
        pResponse = create_ismaster_response(req);
        break;

    // TODO: More commands

    case mxsmongo::Command::UNKNOWN:
        MXS_ERROR("Command not recognized: %s", req.to_string().c_str());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* mxsmongo::Database::handle_command(const mxsmongo::Msg& req,
                                          const bsoncxx::document::view& doc,
                                          mxs::Component& downstream)
{
    mxb_assert(is_ready());

    GWBUF* pResponse = nullptr;

    switch (mxsmongo::get_command(doc))
    {
    case mxsmongo::Command::ISMASTER:
        {
            if (m_name != "admin")
            {
                MXS_WARNING("ismaster command issued on '%s' and not expected 'admin.",
                            m_name.c_str());
            }

            pResponse = create_ismaster_response(req);
        }
        break;

    // TODO: More commands

    case mxsmongo::Command::UNKNOWN:
        MXS_ERROR("Command not recognized: %s", req.to_string().c_str());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* mxsmongo::Database::translate(GWBUF* pMariaDB_response)
{
    mxb_assert(is_pending());

    mxb_assert(!true);
    gwbuf_free(pMariaDB_response);
    return nullptr;
}

GWBUF* mxsmongo::Database::create_ismaster_response(const mxsmongo::Packet& req)
{
    // TODO: Do not simply return a hardwired response.

    auto builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value doc_value = builder
        << "ismaster" << true
        << "topologyVersion" << mxsmongo::topology_version()
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
    pRes_hdr->request_id = m_context.next_request_id();
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
