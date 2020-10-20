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

#include "mxsmongo.hh"
#include <sstream>
#include <map>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include "mxsmongodatabase.hh"

using namespace std;

namespace
{

struct ThisUnit
{
    const map<const char*, mxsmongo::Command> commands_by_key =
    {
        {
            mxsmongo::keys::ISMASTER,  mxsmongo::Command::ISMASTER
        }
    };
} this_unit;

}

const char* mxsmongo::opcode_to_string(int code)
{
    switch (code)
    {
    case MONGOC_OPCODE_REPLY:
        return "MONGOC_OPCODE_REPLY";

    case MONGOC_OPCODE_UPDATE:
        return "MONGOC_OPCODE_UPDATE";

    case MONGOC_OPCODE_INSERT:
        return "MONGOC_OPCODE_INSERT";

    case MONGOC_OPCODE_QUERY:
        return "MONGOC_OPCODE_QUERY";

    case MONGOC_OPCODE_GET_MORE:
        return "OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UKNOWN";
    }
}

mxsmongo::Command mxsmongo::get_command(const bsoncxx::document::view& doc)
{
    mxsmongo::Command command = mxsmongo::Command::UNKNOWN;

    // TODO: At some point it might be good to apply some kind of heuristic for
    // TODO: deciding whether to loop over the keys of the document or over
    // TODO: the keys in the map. Or, can we be certain that e.g. the first
    // TODO: field in the document is the command?

    for (const auto& kv : this_unit.commands_by_key)
    {
        if (doc.find(kv.first) != doc.cend())
        {
            command = kv.second;
            break;
        }
    }

    return command;
}

mxsmongo::Mongo::Mongo()
{
}

mxsmongo::Mongo::~Mongo()
{
}

GWBUF* mxsmongo::Mongo::handle_request(const mxsmongo::Packet& req, mxs::Component& downstream)
{
    GWBUF* pResponse = nullptr;

    switch (req.opcode())
    {
    case MONGOC_OPCODE_COMPRESSED:
    case MONGOC_OPCODE_DELETE:
    case MONGOC_OPCODE_GET_MORE:
    case MONGOC_OPCODE_INSERT:
    case MONGOC_OPCODE_KILL_CURSORS:
    case MONGOC_OPCODE_REPLY:
    case MONGOC_OPCODE_UPDATE:
        MXS_ERROR("Packet %s not handled (yet).", mxsmongo::opcode_to_string(req.opcode()));
        mxb_assert(!true);
        break;

    case MONGOC_OPCODE_MSG:
        pResponse = handle_msg(mxsmongo::Msg(req), downstream);
        break;

    case MONGOC_OPCODE_QUERY:
        pResponse = handle_query(mxsmongo::Query(req), downstream);
        break;

    default:
        MXS_ERROR("Unknown opcode %d.", req.opcode());
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* mxsmongo::Mongo::translate(GWBUF* pResponse)
{
    mxb_assert(!true);
    gwbuf_free(pResponse);
    return nullptr;
}

GWBUF* mxsmongo::Mongo::handle_query(const mxsmongo::Query& req, mxs::Component& downstream)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

    GWBUF* pResponse = nullptr;

    // TODO: The assumption here is that the collection is the primary entity,
    // TODO: which decides what follows. Current assumption is that but for predefined
    // TODO: collections, a collection will be mapped to an existing table.

    if (req.collection() == "admin.$cmd")
    {
        switch (mxsmongo::get_command(req.query()))
        {
        case mxsmongo::Command::ISMASTER:
            pResponse = create_ismaster_response(req);
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

GWBUF* mxsmongo::Mongo::handle_msg(const mxsmongo::Msg& req, mxs::Component& downstream)
{
    MXS_NOTICE("\n%s\n", req.to_string().c_str());

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
                            pResponse = create_ismaster_response(req);
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

GWBUF* mxsmongo::Mongo::create_ismaster_response(const mxsmongo::Packet& req)
{
    // TODO: Do not simply return a hardwired response.

    auto topologyVersion_builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value topologyVersion_value = topologyVersion_builder
        << "processId" << m_context.oid()
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
