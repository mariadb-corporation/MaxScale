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

#include "mxsmongocommand.hh"
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxbase/string.hh>
#include <maxscale/modutil.hh>
#include "mxsmongodatabase.hh"

//
// The include order, which has no impact on the functionality, is the one
// used here: https://docs.mongodb.com/manual/reference/command/
//
// Files that contain no implemented commands are commented out.
//
//#include "commands/aggregation.hh"
//#include "commands/geospatial.hh"
#include "commands/query_and_write_operation.hh"
//#include "commands/query_plan_cache.hh"
//#include "commands/authentication.hh"
//#include "commands/user_management.hh"
//#include "commands/role_management.hh"
#include "commands/replication.hh"
//#include "commands/sharding.hh"
#include "commands/sessions.hh"
//#include "commands/administration.hh"
#include "commands/diagnostic.hh"
#include "commands/free_monitoring.hh"
//#include "commands/system_events_auditing.hh"

using namespace std;

namespace
{

class Unknown : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        GWBUF* pResponse = nullptr;

        stringstream ss;
        ss << "Command not recognized: '" << bsoncxx::to_json(m_doc) << "'";
        auto s = ss.str();

        switch (m_database.config().on_unknown_command)
        {
        case Config::RETURN_ERROR:
            MXS_ERROR("%s", s.c_str());
            pResponse = create_error_response(s, mxsmongo::error::COMMAND_FAILED);
            break;

        case Config::RETURN_EMPTY:
            MXS_WARNING("%s", s.c_str());
            pResponse = create_empty_response();
            break;
        }

        return pResponse;
    }
};

using namespace mxsmongo;

template<class ConcreteCommand>
unique_ptr<Command> create_command(Database* pDatabase,
                                   GWBUF* pRequest,
                                   const Packet& req,
                                   const bsoncxx::document::view& doc)
{
    return unique_ptr<ConcreteCommand>(new ConcreteCommand(pDatabase, pRequest, req, doc));
}

using CreatorFunction = unique_ptr<Command> (*)(Database* pDatabase,
                                                GWBUF* pRequest,
                                                const Packet& req,
                                                const bsoncxx::document::view& doc);
using CreatorsByName = const map<string, CreatorFunction>;

struct ThisUnit
{
    CreatorsByName creators_by_name =
    {
        { mxb::tolower(key::BUILDINFO),               &create_command<command::BuildInfo> },
        { mxb::tolower(key::DELETE),                  &create_command<command::Delete> },
        { mxb::tolower(key::ENDSESSIONS),             &create_command<command::EndSessions> },
        { mxb::tolower(key::FIND),                    &create_command<command::Find> },
        { mxb::tolower(key::GETLOG),                  &create_command<command::GetLog> },
        { mxb::tolower(key::GETCMDLINEOPTS),          &create_command<command::GetCmdLineOpts> },
        { mxb::tolower(key::GETFREEMONITORINGSTATUS), &create_command<command::GetFreeMonitoringStatus> },
        { mxb::tolower(key::INSERT),                  &create_command<command::Insert> },
        { mxb::tolower(key::ISMASTER),                &create_command<command::IsMaster> },
        { mxb::tolower(key::UPDATE),                  &create_command<command::Update> },
        { mxb::tolower(key::REPLSETGETSTATUS),        &create_command<command::ReplSetGetStatus> },
        { mxb::tolower(key::WHATSMYURI),              &create_command<command::WhatsMyUri> },
    };
} this_unit;

}

namespace mxsmongo
{

Command::Command(Database* pDatabase,
                 GWBUF* pRequest,
                 const Packet& req,
                 const bsoncxx::document::view& doc)
    : m_database(*pDatabase)
    , m_pRequest(gwbuf_clone(pRequest))
    , m_req(req)
    , m_doc(doc)
{
}

Command::~Command()
{
    free_request();
}

//static
unique_ptr<Command> Command::get(mxsmongo::Database* pDatabase,
                                 GWBUF* pRequest,
                                 const mxsmongo::Packet& req,
                                 const bsoncxx::document::view& doc)
{
    CreatorFunction create = nullptr;

    if (!doc.empty())
    {
        // The command *must* be the first element,
        auto element = *doc.begin();
        string name(element.key().data(), element.key().length());
        mxb::lower_case(name);

        auto it = this_unit.creators_by_name.find(name);

        if (it != this_unit.creators_by_name.end())
        {
            create = it->second;
        }
    }

    if (!create)
    {
        create = &create_command<Unknown>;
    }

    return create(pDatabase, pRequest, req, doc);
}

Command::State Command::translate(GWBUF& mariadb_response, GWBUF** ppMongo_response)
{
    mxb_assert(!true);
    *ppMongo_response = nullptr;
    return READY;
}

GWBUF* Command::create_empty_response()
{
    auto builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value doc_value = builder << bsoncxx::builder::stream::finalize;

    return create_response(doc_value);
}

GWBUF* Command::create_error_response(const std::string& message, error::Code code)
{
    bsoncxx::builder::basic::document builder;

    builder.append(bsoncxx::builder::basic::kvp("$err", message.c_str()));
    builder.append(bsoncxx::builder::basic::kvp("code", static_cast<int32_t>(code)));

    return create_response(builder.extract());
}

string Command::get_table(const char* zCommand) const
{
    auto utf8 = m_doc[zCommand].get_utf8();
    string table(utf8.value.data(), utf8.value.size());

    return "`" + m_database.name() + "`.`" + table + "`";
}

void Command::free_request()
{
    if (m_pRequest)
    {
        gwbuf_free(m_pRequest);
        m_pRequest = nullptr;
    }
}

void Command::send_downstream(const string& sql)
{
    MXS_NOTICE("SQL: %s", sql.c_str());

    GWBUF* pRequest = modutil_create_query(sql.c_str());

    m_database.context().downstream().routeQuery(pRequest);
}

GWBUF* Command::create_response(const bsoncxx::document::value& doc)
{
    GWBUF* pResponse = nullptr;

    switch (m_req.opcode())
    {
    case Packet::QUERY:
        pResponse = create_reply_response(doc);
        break;

    case Packet::MSG:
        pResponse = create_msg_response(doc);
        break;

    default:
        mxb_assert(!true);
    }

    return pResponse;
}

GWBUF* Command::translate_resultset(vector<string>& extractions, GWBUF* pMariadb_response)
{
    bool is_msg_response = (m_req.opcode() == Packet::MSG);

    // msg response
    bsoncxx::builder::basic::array firstBatch_builder;

    // reply response
    vector<bsoncxx::document::value> documents;
    uint32_t size_of_documents = 0;

    if (pMariadb_response)
    {
        uint8_t* pBuffer = GWBUF_DATA(pMariadb_response);

        ComQueryResponse cqr(&pBuffer);

        auto nFields = cqr.nFields();

        // If there are no extractions, then we SELECTed the entire document and there should
        // be just one field (the JSON document). Otherwise there should be as many fields
        // (JSON_EXTRACT(doc, '$...')) as there are extractions.
        mxb_assert((extractions.empty() && nFields == 1) || (extractions.size() == nFields));

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

        // Then there will be an arbitrary number of rows. After all rows
        // (of which there obviously may be 0), there will be an EOF packet.
        int nRow = 0;
        while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
        {
            ++nRow;

            CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer

            auto it = row.begin();

            string json;

            if (extractions.empty())
            {
                const auto& value = *it++;
                mxb_assert(it == row.end());
                // The value is now a JSON object.
                json = value.as_string().to_string();
            }
            else
            {
                auto jt = extractions.begin();

                bool first = true;
                json += "{";
                for (; it != row.end(); ++it, ++jt)
                {
                    if (first)
                    {
                        first = false;
                    }
                    else
                    {
                        json += ", ";
                    }

                    const auto& value = *it;
                    auto extraction = *jt;

                    json += create_entry(extraction, value.as_string().to_string());
                }
                json += "}";
            }

            try
            {
                auto doc = bsoncxx::from_json(json);

                if (is_msg_response)
                {
                    firstBatch_builder.append(doc);
                }
                else
                {
                    size_of_documents += doc.view().length();
                    documents.push_back(doc);
                }
            }
            catch (const std::exception& x)
            {
                MXS_ERROR("Could not convert object to JSON: %s", x.what());
                MXS_NOTICE("String: '%s'", json.c_str());
            }
        }
    }

    GWBUF* pResponse = nullptr;

    if (is_msg_response)
    {
        bsoncxx::builder::basic::document cursor_builder;
        cursor_builder.append(bsoncxx::builder::basic::kvp("firstBatch", firstBatch_builder.extract()));
        cursor_builder.append(bsoncxx::builder::basic::kvp("partialResultsReturned", false));
        cursor_builder.append(bsoncxx::builder::basic::kvp("id", int64_t(0)));
        cursor_builder.append(bsoncxx::builder::basic::kvp("ns", get_table(key::FIND)));

        bsoncxx::builder::basic::document msg_builder;
        msg_builder.append(bsoncxx::builder::basic::kvp("cursor", cursor_builder.extract()));
        msg_builder.append(bsoncxx::builder::basic::kvp("ok", int32_t(1)));

        pResponse = create_msg_response(msg_builder.extract());
    }
    else
    {
        pResponse = create_reply_response(size_of_documents, documents);
    }

    return pResponse;
}

void Command::add_error(bsoncxx::builder::basic::document& builder, const ComERR& err)
{
    MXS_WARNING("Mongo request to backend failed: (%d), %s", err.code(), err.message().c_str());

    bsoncxx::builder::basic::document mariadb_builder;

    mariadb_builder.append(bsoncxx::builder::basic::kvp("code", err.code()));
    mariadb_builder.append(bsoncxx::builder::basic::kvp("state", err.state()));
    mariadb_builder.append(bsoncxx::builder::basic::kvp("message", err.message()));

    builder.append(bsoncxx::builder::basic::kvp("mariadb", mariadb_builder.extract()));

    // TODO: Map MariaDB errors to something sensible from
    // TODO: https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

    bsoncxx::builder::basic::array array_builder;

    for (int64_t i = 0; i < 1; ++i) // TODO: With multiple updates/deletes object this must change.
    {
        bsoncxx::builder::basic::document error_builder;

        error_builder.append(bsoncxx::builder::basic::kvp("index", i));
        int32_t code = error::from_mariadb_code(err.code());
        error_builder.append(bsoncxx::builder::basic::kvp("code", code));
        error_builder.append(bsoncxx::builder::basic::kvp("errmsg", err.message()));

        array_builder.append(error_builder.extract());
    }

    builder.append(bsoncxx::builder::basic::kvp("writeErrors", array_builder.extract()));
}

pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(size_t size_of_documents, size_t nDocuments)
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a Mongo packet.

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
    int64_t cursor_id = 0;
    int32_t starting_from = 0;
    int32_t number_returned = nDocuments;

    size_t response_size = MXSMONGO_HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

    pData += set_byte4(pData, response_flags);
    pData += set_byte8(pData, cursor_id);
    pData += set_byte4(pData, starting_from);
    pData += set_byte4(pData, number_returned);

    return make_pair(pResponse, pData);
}

GWBUF* Command::create_reply_response(size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents)
{
    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(size_of_documents, documents.size());

    for (const auto& doc : documents)
    {
        auto view = doc.view();
        size_t size = view.length();

        memcpy(pData, view.data(), view.length());
        pData += view.length();
    }

    return pResponse;
}

GWBUF* Command::create_reply_response(const bsoncxx::document::value& doc)
{
    MXS_NOTICE("Response(REPLY): %s", bsoncxx::to_json(doc).c_str());

    auto doc_view = doc.view();
    size_t doc_len = doc_view.length();

    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(doc_len, 1);

    memcpy(pData, doc_view.data(), doc_view.length());

    return pResponse;
}

GWBUF* Command::create_msg_response(const bsoncxx::document::value& doc)
{
    MXS_NOTICE("Response(MSG): %s", bsoncxx::to_json(doc).c_str());

    uint32_t flag_bits = 0;
    uint8_t kind = 0;
    uint32_t doc_length = doc.view().length();
    uint32_t checksum = 0;

    size_t response_size = MXSMONGO_HEADER_LEN
        + sizeof(flag_bits) + sizeof(kind) + doc_length; // + sizeof(checksum);

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_MSG;

    uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

    pData += set_byte4(pData, flag_bits);

    pData += set_byte1(pData, kind);
    memcpy(pData, doc.view().data(), doc_length);
    pData += doc_length;
    //pData += set_byte4(pData, checksum);

    return pResponse;
}

string Command::create_leaf_entry(const string& extraction, const std::string& value)
{
    mxb_assert(extraction.find('.') == string::npos);

    return "\"" + extraction + "\": " + value;
}

string Command::create_nested_entry(const string& extraction, const std::string& value)
{
    string entry;
    auto i = extraction.find('.');

    if (i == string::npos)
    {
        entry = "{ "  + create_leaf_entry(extraction, value) + " }";
    }
    else
    {
        auto head = extraction.substr(0, i);
        auto tail = extraction.substr(i + 1);

        entry = "{ \"" + head + "\": " + create_nested_entry(tail, value) + "}";
    }

    return entry;
}

string Command::create_entry(const string& extraction, const std::string& value)
{
    string entry;
    auto i = extraction.find('.');

    if (i == string::npos)
    {
        entry = create_leaf_entry(extraction, value);
    }
    else
    {
        auto head = extraction.substr(0, i);
        auto tail = extraction.substr(i + 1);

        entry = "\"" + head + "\": " + create_nested_entry(tail, value);;
    }

    return entry;
}

}

