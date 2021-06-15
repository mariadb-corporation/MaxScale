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

#include "nosqlcommand.hh"
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxbase/string.hh>
#include <maxscale/modutil.hh>
#include "nosqldatabase.hh"
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

class Unknown : public nosql::ImmediateCommand
{
public:
    using nosql::ImmediateCommand::ImmediateCommand;

    void populate_response(nosql::DocumentBuilder& doc) override
    {
        string command;
        if (!m_doc.empty())
        {
            auto element = *m_doc.begin();
            auto key = element.key();
            command = string(key.data(), key.length());
        }

        ostringstream ss;
        ss << "no such command: '" << command << "'";
        auto s = ss.str();

        switch (m_database.config().on_unknown_command)
        {
        case GlobalConfig::RETURN_ERROR:
            {
                MXS_INFO("%s", s.c_str());
                throw nosql::SoftError(s, nosql::error::COMMAND_NOT_FOUND);
            }
            break;

        case GlobalConfig::RETURN_EMPTY:
            MXS_INFO("%s", s.c_str());
            break;
        }
    }
};

using namespace nosql;

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

struct CommandInfo
{
    CommandInfo()
        : zKey(nullptr)
        , zHelp(nullptr)
        , create(nullptr)
        , is_admin(false)
    {
    }

    CommandInfo(const char* zKey, const char* zHelp, CreatorFunction create, bool is_admin)
        : zKey(zKey)
        , zHelp(zHelp)
        , create(create)
        , is_admin(is_admin)
    {
    }

    const char*     zKey;
    const char*     zHelp;
    CreatorFunction create;
    bool            is_admin;
};

template<class ConcreteCommand>
CommandInfo create_info()
{
    return CommandInfo(ConcreteCommand::KEY,
                       ConcreteCommand::HELP,
                       &create_command<ConcreteCommand>,
                       command::IsAdmin<ConcreteCommand>::is_admin);
}

using InfosByName = const map<string, CommandInfo>;

struct ThisUnit
{
    static std::string tolower(const char* zString)
    {
        return mxb::tolower(zString);
    }

    InfosByName infos_by_name =
    {
        { tolower(command::BuildInfo::KEY),               create_info<command::BuildInfo>() },
        { tolower(command::Count::KEY),                   create_info<command::Count>() },
        { tolower(command::Create::KEY),                  create_info<command::Create>() },
        { tolower(command::Delete::KEY),                  create_info<command::Delete>() },
        { tolower(command::Distinct::KEY),                create_info<command::Distinct>() },
        { tolower(command::Drop::KEY),                    create_info<command::Drop>() },
        { tolower(command::DropDatabase::KEY),            create_info<command::DropDatabase>() },
        { tolower(command::EndSessions::KEY),             create_info<command::EndSessions>() },
        { tolower(command::Find::KEY),                    create_info<command::Find>() },
        { tolower(command::GetCmdLineOpts::KEY),          create_info<command::GetCmdLineOpts>() },
        { tolower(command::GetFreeMonitoringStatus::KEY), create_info<command::GetFreeMonitoringStatus>() },
        { tolower(command::GetLastError::KEY),            create_info<command::GetLastError>() },
        { tolower(command::GetLog::KEY),                  create_info<command::GetLog>() },
        { tolower(command::GetMore::KEY),                 create_info<command::GetMore>() },
        { tolower(command::Insert::KEY),                  create_info<command::Insert>() },
        { tolower(command::IsMaster::KEY),                create_info<command::IsMaster>() },
        { tolower(command::KillCursors::KEY),             create_info<command::KillCursors>() },
        { tolower(command::ListCommands::KEY),            create_info<command::ListCommands>() },
        { tolower(command::ListCollections::KEY),         create_info<command::ListCollections>() },
        { tolower(command::ListDatabases::KEY),           create_info<command::ListDatabases>() },
        { tolower(command::Ping::KEY),                    create_info<command::Ping>() },
        { tolower(command::ReplSetGetStatus::KEY),        create_info<command::ReplSetGetStatus>() },
        { tolower(command::RenameCollection::KEY),        create_info<command::RenameCollection>() },
        { tolower(command::ResetError::KEY),              create_info<command::ResetError>() },
        { tolower(command::Update::KEY),                  create_info<command::Update>() },
        { tolower(command::WhatsMyUri::KEY),              create_info<command::WhatsMyUri>() },

        { tolower(command::MxsDiagnose::KEY),             create_info<command::MxsDiagnose>() },
        { tolower(command::MxsCreateDatabase::KEY),       create_info<command::MxsCreateDatabase>() },
        { tolower(command::MxsGetConfig::KEY),            create_info<command::MxsGetConfig>() },
        { tolower(command::MxsSetConfig::KEY),            create_info<command::MxsSetConfig>() },
    };
} this_unit;

}

namespace nosql
{

Command::~Command()
{
    free_request();
}

namespace
{

pair<string, CommandInfo> get_info(const bsoncxx::document::view& doc)
{
    string name;
    CommandInfo info;

    if (!doc.empty())
    {
        // The command *must* be the first element,
        auto element = *doc.begin();
        name.append(element.key().data(), element.key().length());

        auto it = this_unit.infos_by_name.find(mxb::tolower(name));

        if (it != this_unit.infos_by_name.end())
        {
            info = it->second;
        }
    }

    if (!info.create)
    {
        name = "unknown";
        info.create = &create_command<Unknown>;
        info.is_admin = false;
    }

    return make_pair(name, info);
}

}

//static
unique_ptr<Command> Command::get(nosql::Database* pDatabase,
                                 GWBUF* pRequest,
                                 const nosql::Query& query,
                                 const bsoncxx::document::view& doc,
                                 const DocumentArguments& arguments)
{
    auto p = get_info(doc);

    const string& name = p.first;
    CreatorFunction create = p.second.create;

    return create(name, pDatabase, pRequest, &query, nullptr, doc, arguments);
}

//static
unique_ptr<Command> Command::get(nosql::Database* pDatabase,
                                 GWBUF* pRequest,
                                 const nosql::Msg& msg,
                                 const bsoncxx::document::view& doc,
                                 const DocumentArguments& arguments)
{
    auto p = get_info(doc);

    const string& name = p.first;
    CreatorFunction create = p.second.create;

    return create(name, pDatabase, pRequest, nullptr, &msg, doc, arguments);
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
    if (size < 1 || size > protocol::MAX_WRITE_BATCH_SIZE)
    {
        ostringstream ss;
        ss << "Write batch sizes must be between 1 and " << protocol::MAX_WRITE_BATCH_SIZE
           << ". Got " << size << " operations.";
        throw nosql::SoftError(ss.str(), nosql::error::INVALID_LENGTH);
    }
}

//static
void Command::check_maximum_sql_length(int length)
{
    if (length > MAX_QUERY_LEN)
    {
        ostringstream ss;
        ss << "Generated SQL of " << length
           << " bytes, exceeds the maximum of " << MAX_QUERY_LEN
           << " bytes.";

        throw HardError(ss.str(), error::COMMAND_FAILED);
    }
}

//static
void Command::list_commands(DocumentBuilder& commands)
{
    for (const auto& kv : this_unit.infos_by_name)
    {
        const string& name = kv.first;
        const CommandInfo& info = kv.second;

        const char* zHelp = info.zHelp;
        if (!*zHelp)
        {
            zHelp = "no help defined";
        }

        DocumentBuilder command;
        command.append(kvp("help", zHelp));
        command.append(kvp("adminOnly", info.is_admin));

        // Yes, passing a literal string to kvp as first argument works, but
        // passing a 'const char*' does not.
        commands.append(kvp(string(info.zKey), command.extract()));
    }
}

void Command::throw_unexpected_packet()
{
    ostringstream ss;
    ss << m_name << " received unexpected packet from backend.";

    throw HardError(ss.str(), error::INTERNAL_ERROR);
}

void Command::require_admin_db()
{
    if (m_database.name() != "admin")
    {
        throw SoftError(m_name + " may only be run against the admin database.",
                        error::UNAUTHORIZED);
    }
}

string Command::convert_skip_and_limit() const
{
    string rv;

    auto skip = m_doc[nosql::key::SKIP];
    auto limit = m_doc[nosql::key::LIMIT];

    if (skip || limit)
    {
        int64_t nSkip = 0;
        if (skip && (!get_number_as_integer(skip, &nSkip) || nSkip < 0))
        {
            ostringstream ss;
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
            ostringstream ss;
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

        ostringstream ss;
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
            ostringstream ss;
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
    MXB_INFO("SQL: %s", sql.c_str());

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

void Command::add_error(bsoncxx::builder::basic::array& array, const ComERR& err, int index)
{
    MXB_WARNING("Mongo request to backend failed: (%d), %s", err.code(), err.message().c_str());

    bsoncxx::builder::basic::document mariadb;

    mariadb.append(bsoncxx::builder::basic::kvp("index", index));
    mariadb.append(bsoncxx::builder::basic::kvp("code", err.code()));
    mariadb.append(bsoncxx::builder::basic::kvp("state", err.state()));
    mariadb.append(bsoncxx::builder::basic::kvp("message", err.message()));

    // TODO: Map MariaDB errors to something sensible from
    // TODO: https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

    bsoncxx::builder::basic::document error;

    interpret_error(error, err, index);
    error.append(bsoncxx::builder::basic::kvp("mariadb", mariadb.extract()));

    array.append(error.extract());
}

void Command::add_error(bsoncxx::builder::basic::document& response, const ComERR& err)
{
    bsoncxx::builder::basic::array array;

    add_error(array, err, 0);

    response.append(bsoncxx::builder::basic::kvp("writeErrors", array.extract()));
}

void Command::interpret_error(bsoncxx::builder::basic::document& error, const ComERR& err, int index)
{
    error.append(bsoncxx::builder::basic::kvp("index", index));
    error.append(bsoncxx::builder::basic::kvp("code", error::from_mariadb_code(err.code())));
    error.append(bsoncxx::builder::basic::kvp("errmsg", err.message()));
}

pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(size_t size_of_documents, size_t nDocuments) const
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a Mongo packet.

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
    int64_t cursor_id = 0;
    int32_t starting_from = 0;
    int32_t number_returned = nDocuments;

    size_t response_size = protocol::HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + protocol::HEADER_LEN;

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
    MXB_INFO("Response(REPLY): %s", bsoncxx::to_json(doc).c_str());

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
    MXB_INFO("Response(MSG): %s", bsoncxx::to_json(doc).c_str());

    uint32_t flag_bits = 0;
    uint8_t kind = 0;
    uint32_t doc_length = doc.view().length();

    size_t response_size = protocol::HEADER_LEN + sizeof(flag_bits) + sizeof(kind) + doc_length;

    if (m_append_checksum)
    {
        flag_bits |= Msg::CHECKSUM_PRESENT;
        response_size += sizeof(uint32_t); // sizeof checksum
    }

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_req.request_id();
    pRes_hdr->opcode = MONGOC_OPCODE_MSG;

    uint8_t* pData = GWBUF_DATA(pResponse) + protocol::HEADER_LEN;

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

GWBUF* ImmediateCommand::execute()
{
    DocumentBuilder doc;
    populate_response(doc);
    return create_response(doc.extract());
}

Command::State ImmediateCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppProtocol_response)
{
    // This will never be called.
    mxb_assert(!true);
    throw std::runtime_error("ImmediateCommand::translate(...) should not be called.");
    return READY;
}

void ImmediateCommand::diagnose(DocumentBuilder& doc)
{
    doc.append(kvp("kind", "immediate"));

    DocumentBuilder response;
    populate_response(response);

    doc.append(kvp("response", response.extract()));
}

GWBUF* SingleCommand::execute()
{
    prepare();

    string statement = generate_sql();

    check_maximum_sql_length(statement);

    m_statement = std::move(statement);

    send_downstream(m_statement);
    return nullptr;
}

void SingleCommand::prepare()
{
}

void SingleCommand::diagnose(DocumentBuilder& doc)
{
    doc.append(kvp("kind", "single"));
    doc.append(kvp("sql", generate_sql()));
}

void MultiCommand::diagnose(DocumentBuilder& doc)
{
    doc.append(kvp("kind", "multi"));
    const auto& query = generate_sql();

    ArrayBuilder sql;
    for (const auto& statement : query.statements())
    {
        sql.append(statement);
    }

    doc.append(kvp("sql", sql.extract()));
}

}
