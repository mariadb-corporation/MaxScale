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
#include "crc32.h"

//
// The include order, which has no impact on the functionality, is the one
// used here: https://docs.mongodb.com/manual/reference/command/
//
// Files that contain no implemented commands are commented out.
//
#include "commands/aggregation.hh"
//#include "commands/geospatial.hh"
#include "commands/query_and_write_operation.hh"
//#include "commands/query_plan_cache.hh"
//#include "commands/authentication.hh"
//#include "commands/user_management.hh"
//#include "commands/role_management.hh"
#include "commands/replication.hh"
//#include "commands/sharding.hh"
#include "commands/sessions.hh"
#include "commands/administration.hh"
#include "commands/diagnostic.hh"
#include "commands/free_monitoring.hh"
//#include "commands/system_events_auditing.hh"

#include "commands/maxscale.hh"

using namespace std;

namespace
{

uint32_t (*crc32_func)(const void *, size_t) = wiredtiger_crc32c_func();

class Unknown : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        GWBUF* pResponse = nullptr;

        string command;
        if (!m_doc.empty())
        {
            auto element = *m_doc.begin();
            auto key = element.key();
            command = string(key.data(), key.length());
        }

        stringstream ss;
        ss << "no such command: '" << command << "'";
        auto s = ss.str();

        switch (m_database.config().on_unknown_command)
        {
        case Config::RETURN_ERROR:
            {
                MXS_ERROR("%s", s.c_str());
                pResponse = mxsmongo::SoftError(s, mxsmongo::error::COMMAND_NOT_FOUND).create_response(*this);
            }
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
unique_ptr<Command> create_command(const string& name,
                                   Database* pDatabase,
                                   GWBUF* pRequest,
                                   const Query* pQuery,
                                   const Msg* pMsg,
                                   const bsoncxx::document::view& doc,
                                   const Command::DocumentArguments& arguments)
{
    unique_ptr<ConcreteCommand> sCommand;

    if (pQuery)
    {
        mxb_assert(!pMsg);
        sCommand.reset(new ConcreteCommand(name, pDatabase, pRequest, *pQuery, doc, arguments));
    }
    else
    {
        mxb_assert(pMsg);
        sCommand.reset(new ConcreteCommand(name, pDatabase, pRequest, *pMsg, doc, arguments));
    }

    return sCommand;
}

using CreatorFunction = unique_ptr<Command> (*)(const string& name,
                                                Database* pDatabase,
                                                GWBUF* pRequest,
                                                const Query* pQuery,
                                                const Msg* pMsg,
                                                const bsoncxx::document::view& doc,
                                                const Command::DocumentArguments& arguments);
using CreatorsByName = const map<string, CreatorFunction>;

struct ThisUnit
{
    CreatorsByName creators_by_name =
    {
        { mxb::tolower(key::BUILDINFO),               &create_command<command::BuildInfo> },
        { mxb::tolower(key::COUNT),                   &create_command<command::Count> },
        { mxb::tolower(key::CREATE),                  &create_command<command::Create> },
        { mxb::tolower(key::DELETE),                  &create_command<command::Delete> },
        { mxb::tolower(key::DISTINCT),                &create_command<command::Distinct> },
        { mxb::tolower(key::DROP),                    &create_command<command::Drop> },
        { mxb::tolower(key::DROPDATABASE),            &create_command<command::DropDatabase> },
        { mxb::tolower(key::ENDSESSIONS),             &create_command<command::EndSessions> },
        { mxb::tolower(key::FIND),                    &create_command<command::Find> },
        { mxb::tolower(key::GETLOG),                  &create_command<command::GetLog> },
        { mxb::tolower(key::GETCMDLINEOPTS),          &create_command<command::GetCmdLineOpts> },
        { mxb::tolower(key::GETFREEMONITORINGSTATUS), &create_command<command::GetFreeMonitoringStatus> },
        { mxb::tolower(key::INSERT),                  &create_command<command::Insert> },
        { mxb::tolower(key::ISMASTER),                &create_command<command::IsMaster> },
        { mxb::tolower(key::LISTCOLLECTIONS),         &create_command<command::ListCollections> },
        { mxb::tolower(key::LISTDATABASES),           &create_command<command::ListDatabases> },
        { mxb::tolower(key::PING),                    &create_command<command::Ping> },
        { mxb::tolower(key::UPDATE),                  &create_command<command::Update> },
        { mxb::tolower(key::REPLSETGETSTATUS),        &create_command<command::ReplSetGetStatus> },
        { mxb::tolower(key::WHATSMYURI),              &create_command<command::WhatsMyUri> },

        { mxb::tolower(key::MXSDIAGNOSE),             &create_command<command::MxsDiagnose> },
    };
} this_unit;

}

namespace mxsmongo
{

Command::~Command()
{
    free_request();
}

namespace
{

pair<string, CreatorFunction> get_creator(const bsoncxx::document::view& doc)
{
    CreatorFunction create = nullptr;
    string name;

    if (!doc.empty())
    {
        // The command *must* be the first element,
        auto element = *doc.begin();
        name.append(element.key().data(), element.key().length());

        auto it = this_unit.creators_by_name.find(mxb::tolower(name));

        if (it != this_unit.creators_by_name.end())
        {
            create = it->second;
        }
    }

    if (!create)
    {
        name = "unknown";
        create = &create_command<Unknown>;
    }

    return make_pair(name, create);
}

}

//static
unique_ptr<Command> Command::get(mxsmongo::Database* pDatabase,
                                 GWBUF* pRequest,
                                 const mxsmongo::Query& query,
                                 const bsoncxx::document::view& doc,
                                 const DocumentArguments& arguments)
{
    auto creator = get_creator(doc);

    const string& name = creator.first;
    CreatorFunction create = creator.second;

    return create(name, pDatabase, pRequest, &query, nullptr, doc, arguments);
}

//static
unique_ptr<Command> Command::get(mxsmongo::Database* pDatabase,
                                 GWBUF* pRequest,
                                 const mxsmongo::Msg& msg,
                                 const bsoncxx::document::view& doc,
                                 const DocumentArguments& arguments)
{
    auto creator = get_creator(doc);

    const string& name = creator.first;
    CreatorFunction create = creator.second;

    return create(name, pDatabase, pRequest, nullptr, &msg, doc, arguments);
}

Command::State Command::translate(GWBUF& mariadb_response, GWBUF** ppMongo_response)
{
    mxb_assert(!true);
    *ppMongo_response = nullptr;
    return READY;
}

GWBUF* Command::create_empty_response() const
{
    auto builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value doc_value = builder << bsoncxx::builder::stream::finalize;

    return create_response(doc_value);
}

//static
void Command::check_write_batch_size(int size)
{
    if (size < 1 || size > mongo::MAX_WRITE_BATCH_SIZE)
    {
        stringstream ss;
        ss << "Write batch sizes must be between 1 and " << mongo::MAX_WRITE_BATCH_SIZE
           << ". Got " << size << " operations.";
        throw mxsmongo::SoftError(ss.str(), mxsmongo::error::INVALID_LENGTH);
    }
}

string Command::convert_skip_and_limit() const
{
    string rv;

    auto skip = m_doc[mxsmongo::key::SKIP];
    auto limit = m_doc[mxsmongo::key::LIMIT];

    if (skip || limit)
    {
        int64_t nSkip = 0;
        if (skip && (!get_number_as_integer(skip, &nSkip) || nSkip < 0))
        {
            stringstream ss;
            int code;
            if (nSkip < 0)
            {
                ss << "Skip value must be non-negative, but received: " << nSkip;
                code = error::BAD_VALUE;
            }
            else
            {
                ss << "Failed to parse: " << bsoncxx::to_json(m_doc) << ". 'skip' field must be numeric.";
                code = error::FAILED_TO_PARSE;
            }

            throw SoftError(ss.str(), code);
        }

        int64_t nLimit = std::numeric_limits<int64_t>::max();
        if (limit && (!get_number_as_integer(limit, &nLimit) || nLimit < 0))
        {
            stringstream ss;
            int code;

            if (nLimit < 0)
            {
                ss << "Limit value must be non-negative, but received: " << nLimit;
                code = error::BAD_VALUE;
            }
            else
            {
                ss << "Failed to parse: " << bsoncxx::to_json(m_doc) << ". 'limit' field must be numeric.";
                code = error::FAILED_TO_PARSE;
            }

            throw SoftError(ss.str(), code);
        }

        stringstream ss;
        ss << "LIMIT ";

        if (nSkip != 0)
        {
            ss << nSkip << ", ";
        }

        ss << nLimit;

        rv = ss.str();
    }

    return rv;
}

const string& Command::table(Quoted quoted) const
{
    if (m_quoted_table.empty())
    {
        auto element = m_doc[m_name];
        mxb_assert(element);

        if (element.type() != bsoncxx::type::k_utf8)
        {
            stringstream ss;
            ss << "collection name has invalid type " << bsoncxx::to_string(element.type());
            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto utf8 = element.get_utf8();
        string table(utf8.value.data(), utf8.value.size());

        m_quoted_table = "`" + m_database.name() + "`.`" + table + "`";
        m_unquoted_table = m_database.name() + "." + table;
    }

    return quoted == Quoted::YES ? m_quoted_table : m_unquoted_table;
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

    m_last_statement = sql;

    GWBUF* pRequest = modutil_create_query(sql.c_str());

    m_database.context().downstream().routeQuery(pRequest);
}

GWBUF* Command::create_response(const bsoncxx::document::value& doc) const
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
        cursor_builder.append(bsoncxx::builder::basic::kvp("ns", table(Quoted::NO)));

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

void Command::add_error(bsoncxx::builder::basic::array& array, const ComERR& err, int index)
{
    MXS_WARNING("Mongo request to backend failed: (%d), %s", err.code(), err.message().c_str());

    bsoncxx::builder::basic::document mariadb;

    mariadb.append(bsoncxx::builder::basic::kvp("code", err.code()));
    mariadb.append(bsoncxx::builder::basic::kvp("state", err.state()));
    mariadb.append(bsoncxx::builder::basic::kvp("message", err.message()));

    // TODO: Map MariaDB errors to something sensible from
    // TODO: https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

    bsoncxx::builder::basic::array array_builder;

    bsoncxx::builder::basic::document error;

    error.append(bsoncxx::builder::basic::kvp("index", index));
    int32_t code = error::from_mariadb_code(err.code());
    error.append(bsoncxx::builder::basic::kvp("code", code));
    error.append(bsoncxx::builder::basic::kvp("errmsg", err.message()));
    error.append(bsoncxx::builder::basic::kvp("mariadb", mariadb.extract()));

    array.append(error.extract());
}

void Command::add_error(bsoncxx::builder::basic::document& response, const ComERR& err)
{
    bsoncxx::builder::basic::array array;

    add_error(array, err, 0);

    response.append(bsoncxx::builder::basic::kvp("writeErrors", array.extract()));
}

pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(size_t size_of_documents, size_t nDocuments) const
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a Mongo packet.

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
    int64_t cursor_id = 0;
    int32_t starting_from = 0;
    int32_t number_returned = nDocuments;

    size_t response_size = mongo::HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongo::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + mongo::HEADER_LEN;

    pData += set_byte4(pData, response_flags);
    pData += set_byte8(pData, cursor_id);
    pData += set_byte4(pData, starting_from);
    pData += set_byte4(pData, number_returned);

    return make_pair(pResponse, pData);
}

GWBUF* Command::create_reply_response(size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents) const
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

GWBUF* Command::create_reply_response(const bsoncxx::document::value& doc) const
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

GWBUF* Command::create_msg_response(const bsoncxx::document::value& doc) const
{
    MXS_NOTICE("Response(MSG): %s", bsoncxx::to_json(doc).c_str());

    uint32_t flag_bits = 0;
    uint8_t kind = 0;
    uint32_t doc_length = doc.view().length();

    size_t response_size = mongo::HEADER_LEN + sizeof(flag_bits) + sizeof(kind) + doc_length;

    if (m_append_checksum)
    {
        flag_bits |= Msg::CHECKSUM_PRESENT;
        response_size += sizeof(uint32_t); // sizeof checksum
    }

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<mongo::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_MSG;

    uint8_t* pData = GWBUF_DATA(pResponse) + mongo::HEADER_LEN;

    pData += set_byte4(pData, flag_bits);

    pData += set_byte1(pData, kind);
    memcpy(pData, doc.view().data(), doc_length);
    pData += doc_length;

    if (m_append_checksum)
    {
        uint32_t checksum = crc32_func(gwbuf_link_data(pResponse), response_size - sizeof(uint32_t));
        pData += set_byte4(pData, checksum);
    }

    return pResponse;
}

string Command::create_leaf_entry(const string& extraction, const std::string& value) const
{
    mxb_assert(extraction.find('.') == string::npos);

    return "\"" + extraction + "\": " + value;
}

string Command::create_nested_entry(const string& extraction, const std::string& value) const
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

string Command::create_entry(const string& extraction, const std::string& value) const
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

