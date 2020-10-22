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
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/modutil.hh>
#include "../../filter/masking/mysql.hh"

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
        pResponse = command_ismaster(req, req.query(), downstream);
        break;

    case mxsmongo::Command::FIND:
        pResponse = command_find(req, req.query(), downstream);
        break;

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

            pResponse = command_ismaster(req, doc, downstream);
        }
        break;

    case mxsmongo::Command::FIND:
        pResponse = command_find(req, doc, downstream);
        break;

    case mxsmongo::Command::UNKNOWN:
        MXS_ERROR("Command not recognized: %s", req.to_string().c_str());
        mxb_assert(!true);
    }

    if (!pResponse)
    {
        set_pending();
    }

    return pResponse;
}

GWBUF* mxsmongo::Database::translate(GWBUF* pMariaDB_response)
{
    // TODO: Update will be needed when DEPRECATE_EOF it turned on.

    mxb_assert(is_pending());

    GWBUF* pResponse = nullptr;

    ComResponse response(GWBUF_DATA(pMariaDB_response));

    switch (response.type())
    {
    case ComResponse::ERR_PACKET:
        // TODO: Handle this in a sensible manner.
        mxb_assert(!true);
        break;

    case ComResponse::OK_PACKET:
        break;

    case ComResponse::LOCAL_INFILE_PACKET:
        // This should not happen as the respon
        mxb_assert(!true);
        break;

    default:
        // Must be a result set.
        pResponse = translate_resultset(pMariaDB_response);
    }

    gwbuf_free(pMariaDB_response);

    set_ready();

    return pResponse;
}

GWBUF* mxsmongo::Database::translate_resultset(GWBUF* pMariaDB_response)
{
    bsoncxx::builder::basic::document builder;

    uint8_t* pBuffer = GWBUF_DATA(pMariaDB_response);

    // A result set, so first we get the number of fields...
    ComQueryResponse cqr(&pBuffer);

    auto nFields = cqr.nFields();

    vector<string> names;
    vector<enum_field_types> types;

    for (size_t i = 0; i < nFields; ++i)
    {
        // ... and then as many column definitions.
        ComQueryResponse::ColumnDef column_def(&pBuffer);

        names.push_back(column_def.name().to_string());
        types.push_back(column_def.type());
    }

    // The there should be an EOF packet, which should be bypassed.
    ComResponse eof(&pBuffer);
    mxb_assert(eof.type() == ComResponse::EOF_PACKET);

    vector<bsoncxx::document::value> documents;
    uint32_t size_of_documents = 0;

    // Then there will be an arbitrary number of rows. After all rows
    // (of which there obviously may be 0), there will be an EOF packet.
    while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
    {
        CQRTextResultsetRow row(&pBuffer, types);

        auto it = names.begin();
        auto jt = row.begin();

        while (it != names.end())
        {
            const string& name = *it;
            const auto& value = *jt;

            if (value.is_string())
            {
                builder.append(bsoncxx::builder::basic::kvp(name, value.as_string().to_string()));
            }
            else
            {
                // TODO: Handle other types as well.
                builder.append(bsoncxx::builder::basic::kvp(name, ""));
            }

            auto doc = builder.extract();
            size_of_documents += doc.view().length();

            documents.push_back(doc);

            ++it;
            ++jt;
        }
    }

    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a Mongo packet.

    bsoncxx::document::value doc_value = builder.extract();

    auto doc_view = doc_value.view();
    size_t doc_len = doc_view.length();

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
    int64_t cursor_id = 0;
    int32_t starting_from = 0;
    int32_t number_returned = documents.size();

    size_t response_size = MXSMONGO_HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_context.next_request_id();
    pRes_hdr->response_to = m_request_id;
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

    pData += mxsmongo::set_byte4(pData, response_flags);
    pData += mxsmongo::set_byte8(pData, cursor_id);
    pData += mxsmongo::set_byte4(pData, starting_from);
    pData += mxsmongo::set_byte4(pData, number_returned);

    for (const auto& doc : documents)
    {
        auto view = doc.view();
        size_t size = view.length();

        memcpy(pData, view.data(), view.length());
        pData += view.length();
    }

    return pResponse;

}

GWBUF* mxsmongo::Database::command_find(const mxsmongo::Packet& req,
                                        const bsoncxx::document::view& doc,
                                        mxs::Component& downstream)
{
    auto db = m_name;
    auto element = doc["find"];

    mxb_assert(element.type() == bsoncxx::type::k_utf8);

    auto utf8 = element.get_utf8();

    string table(utf8.value.data(), utf8.value.size());

    stringstream ss;
    ss << "SELECT * FROM " << db << "." << table;

    GWBUF* pRequest = modutil_create_query(ss.str().c_str());

    downstream.routeQuery(pRequest);

    m_request_id = req.request_id();

    return nullptr;
}

GWBUF* mxsmongo::Database::command_ismaster(const mxsmongo::Packet& req,
                                            const bsoncxx::document::view& doc,
                                            mxs::Component& downstream)
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
