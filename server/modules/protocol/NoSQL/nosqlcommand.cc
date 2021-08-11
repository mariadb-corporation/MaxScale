/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlcommand.hh"
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxbase/string.hh>
#include <maxbase/worker.hh>
#include <maxscale/modutil.hh>
#include "nosqldatabase.hh"
#include "crc32.h"

using mxb::Worker;

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
unique_ptr<OpMsgCommand> create_command(const string& name,
                                        Database* pDatabase,
                                        GWBUF* pRequest,
                                        const Query* pQuery,
                                        const Msg* pMsg,
                                        const bsoncxx::document::view& doc,
                                        const OpMsgCommand::DocumentArguments& arguments)
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

using CreatorFunction = unique_ptr<OpMsgCommand> (*)(const string& name,
                                                     Database* pDatabase,
                                                     GWBUF* pRequest,
                                                     const Query* pQuery,
                                                     const Msg* pMsg,
                                                     const bsoncxx::document::view& doc,
                                                     const OpMsgCommand::DocumentArguments& arguments);

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
        { tolower(command::Validate::KEY),                create_info<command::Validate>() },
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

//
// Command
//
Command::~Command()
{
    free_request();
}

bool Command::is_admin() const
{
    return false;
}

string Command::to_json() const
{
    return "";
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

namespace
{

string unexpected_message(const std::string& who, const std::string& statement)
{
    ostringstream ss;
    ss << "Unexpected response received by " << who << " from backend for: " << statement;

    return ss.str();
}

}

void Command::log_unexpected_packet()
{
    MXS_ERROR("%s", unexpected_message(description(), m_last_statement).c_str());
}

void Command::throw_unexpected_packet()
{
    throw HardError(unexpected_message(description(), m_last_statement), error::INTERNAL_ERROR);
}

GWBUF* Command::create_response(const bsoncxx::document::value& doc) const
{
    GWBUF* pResponse = nullptr;

    if (m_response_kind == ResponseKind::REPLY)
    {
        pResponse = create_reply_response(doc);
    }
    else
    {
        pResponse = create_msg_response(doc);
    }

    return pResponse;
}

pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(size_t size_of_documents, size_t nDocuments) const
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a MongoDB packet.

    int32_t response_flags = MONGOC_QUERY_AWAIT_DATA;
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
    pRes_hdr->response_to = m_request_id;
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + protocol::HEADER_LEN;

    pData += protocol::set_byte4(pData, response_flags);
    pData += protocol::set_byte8(pData, cursor_id);
    pData += protocol::set_byte4(pData, starting_from);
    pData += protocol::set_byte4(pData, number_returned);

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

    bool append_checksum = (m_response_kind == ResponseKind::MSG_WITH_CHECKSUM);

    if (append_checksum)
    {
        flag_bits |= Msg::CHECKSUM_PRESENT;
        response_size += sizeof(uint32_t); // sizeof checksum
    }

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = m_database.context().next_request_id();
    pRes_hdr->response_to = m_request_id;
    pRes_hdr->opcode = MONGOC_OPCODE_MSG;

    uint8_t* pData = GWBUF_DATA(pResponse) + protocol::HEADER_LEN;

    pData += protocol::set_byte4(pData, flag_bits);

    pData += protocol::set_byte1(pData, kind);
    memcpy(pData, doc.view().data(), doc_length);
    pData += doc_length;

    if (append_checksum)
    {
        uint32_t checksum = crc32_func(gwbuf_link_data(pResponse), response_size - sizeof(uint32_t));
        pData += protocol::set_byte4(pData, checksum);
    }

    return pResponse;
}

//
// OpDeleteCommand
//
std::string OpDeleteCommand::description() const
{
    return "OP_DELETE";
}

GWBUF* OpDeleteCommand::execute()
{
    ostringstream ss;
    ss << "DELETE FROM " << table() << query_to_where_clause(m_req.selector());

    if ((m_req.flags() & 0x01) == 1)
    {
        ss << " LIMIT 1";
    }

    auto statement = ss.str();

    check_maximum_sql_length(statement);

    send_downstream(statement);

    return nullptr;
}

Command::State OpDeleteCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        break;

    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);

            if (err.code() != ER_NO_SUCH_TABLE)
            {
                m_database.context().set_last_error(MariaDBError(err).create_last_error());
            }
        }
        break;

    default:
        // We do not throw, as that would generate a response.
        log_unexpected_packet();
    }

    *ppNoSQL_response = nullptr;
    return READY;
};

//
// OpInsertCommand
//
std::string OpInsertCommand::description() const
{
    return "OP_INSERT";
}

GWBUF* OpInsertCommand::execute()
{
    auto doc = m_req.documents()[0];

    ostringstream ss;
    ss << "INSERT INTO " << table() << " (doc) VALUES " << convert_document_data(doc) << ";";

    m_statement = ss.str();

    check_maximum_sql_length(m_statement);

    send_downstream(m_statement);

    return nullptr;
}

Command::State OpInsertCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    State state = READY;
    GWBUF* pResponse = nullptr;

    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        if (m_action == CREATING_TABLE || m_action == CREATING_DATABASE)
        {
            Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                    if (action == Worker::Call::EXECUTE)
                    {
                        m_action = INSERTING_DATA;
                        send_downstream(m_statement);
                    }

                    return false;
                });
            state = BUSY;
        }
        else
        {
            state = READY;
        }
        break;

    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);
            auto s = err.message();
            MXS_INFO("%s", s.c_str());

            switch (err.code())
            {
            case ER_NO_SUCH_TABLE:
                {
                    Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                            if (action == Worker::Call::EXECUTE)
                            {
                                auto id_length = m_database.config().id_length;
                                auto sql = nosql::table_create_statement(table(), id_length);

                                m_action = CREATING_TABLE;
                                send_downstream(sql);
                            }

                            return false;
                        });
                    state = BUSY;
                }
                break;

            case ER_BAD_DB_ERROR:
                {
                    if (err.message().find("Unknown database") == 0)
                    {
                        Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                                if (action == Worker::Call::EXECUTE)
                                {
                                    ostringstream ss;
                                    ss << "CREATE DATABASE `" << m_database.name() << "`";

                                    m_action = CREATING_DATABASE;
                                    send_downstream(ss.str());
                                }

                                return false;
                            });
                        state = BUSY;
                    }
                    else
                    {
                        MXS_ERROR("Inserting '%s' failed with: (%d) %s",
                                  m_statement.c_str(), err.code(), err.message().c_str());
                        state = READY;
                    }
                }
                break;

            case ER_DB_CREATE_EXISTS:
            case ER_TABLE_EXISTS_ERROR:
                // Ok, someone else got there first.
                Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                        if (action == Worker::Call::EXECUTE)
                        {
                            m_action = INSERTING_DATA;
                            send_downstream(m_statement);
                        }

                        return false;
                    });
                state = BUSY;
                break;

            default:
                MXS_ERROR("Inserting '%s' failed with: (%d) %s",
                          m_statement.c_str(), err.code(), err.message().c_str());
                state = READY;
            }
        }
        break;

    default:
        mxb_assert(!true);
        throw_unexpected_packet();
    }

    *ppNoSQL_response = pResponse;
    return state;
};

string OpInsertCommand::convert_document_data(const bsoncxx::document::view& doc)
{
    ostringstream sql;

    string json;

    auto element = doc["_id"];

    if (element)
    {
        json = bsoncxx::to_json(doc);
    }
    else
    {
        // Ok, as the document does not have an id, one must be generated. However,
        // as an existing document is immutable, a new one must be created.

        bsoncxx::oid oid;

        DocumentBuilder builder;
        builder.append(kvp(key::_ID, oid));

        for (const auto& e : doc)
        {
            append(builder, e.key(), e);
        }

        // We need to keep the created document around, so that 'element'
        // down below stays alive.
        m_stashed_documents.emplace_back(builder.extract());

        const auto& doc_with_id = m_stashed_documents.back();

        json = bsoncxx::to_json(doc_with_id);
    }

    json = escape_essential_chars(std::move(json));

    sql << "('" << json << "')";

    return sql.str();
}

//
// OpUpdateCommand
//
string OpUpdateCommand::description() const
{
    return "OP_UPDATE";
}

GWBUF* OpUpdateCommand::execute()
{
    ostringstream sql;
    sql << "UPDATE " << table() << " SET DOC = ";

    // TODO: In principle the document may be something else than a
    // TODO: simple replacement document.
    auto json = escape_essential_chars(bsoncxx::to_json(m_req.update()));

    sql << "JSON_SET('" << json << "', '$._id', JSON_EXTRACT(id, '$')) ";
    sql << query_to_where_clause(m_req.selector());

    if (m_req.is_upsert())
    {
        MXS_WARNING("OP_UPDATE(%s): upsert not supported, "
                    "selector: '%s', document: '%s'.",
                    m_req.zCollection(), bsoncxx::to_json(m_req.selector()).c_str(), json.c_str());
    }

    if (!m_req.is_multi())
    {
        sql << " LIMIT 1";
    }

    const auto& statement = sql.str();

    check_maximum_sql_length(statement);

    send_downstream(statement);

    return nullptr;
}

Command::State OpUpdateCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        break;

    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);

            if (err.code() != ER_NO_SUCH_TABLE)
            {
                m_database.context().set_last_error(MariaDBError(err).create_last_error());
            }
        }
        break;

    default:
        // We do not throw, as that would generate a response.
        log_unexpected_packet();
    }

    *ppNoSQL_response = nullptr;
    return READY;
}

//
// OpMsgCommand
//
OpMsgCommand::~OpMsgCommand()
{
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
unique_ptr<OpMsgCommand> OpMsgCommand::get(nosql::Database* pDatabase,
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
unique_ptr<OpMsgCommand> OpMsgCommand::get(nosql::Database* pDatabase,
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

GWBUF* OpMsgCommand::create_empty_response() const
{
    auto builder = bsoncxx::builder::stream::document{};
    bsoncxx::document::value doc_value = builder << bsoncxx::builder::stream::finalize;

    return create_response(doc_value);
}

//static
void OpMsgCommand::check_write_batch_size(int size)
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
void OpMsgCommand::list_commands(DocumentBuilder& commands)
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
        command.append(kvp(key::HELP, zHelp));
        command.append(kvp(key::ADMIN_ONLY, info.is_admin));

        // Yes, passing a literal string to kvp as first argument works, but
        // passing a 'const char*' does not.
        commands.append(kvp(string(info.zKey), command.extract()));
    }
}

void OpMsgCommand::require_admin_db()
{
    if (m_database.name() != "admin")
    {
        throw SoftError(m_name + " may only be run against the admin database.",
                        error::UNAUTHORIZED);
    }
}

string OpMsgCommand::convert_skip_and_limit() const
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

const string& OpMsgCommand::table(Quoted quoted) const
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

void OpMsgCommand::add_error(bsoncxx::builder::basic::array& array, const ComERR& err, int index)
{
    bsoncxx::builder::basic::document mariadb;

    mariadb.append(bsoncxx::builder::basic::kvp(key::INDEX, index));
    mariadb.append(bsoncxx::builder::basic::kvp(key::CODE, err.code()));
    mariadb.append(bsoncxx::builder::basic::kvp(key::STATE, err.state()));
    mariadb.append(bsoncxx::builder::basic::kvp(key::MESSAGE, err.message()));

    // TODO: Map MariaDB errors to something sensible from
    // TODO: https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

    bsoncxx::builder::basic::document error;

    interpret_error(error, err, index);
    error.append(bsoncxx::builder::basic::kvp(key::MARIADB, mariadb.extract()));

    array.append(error.extract());
}

void OpMsgCommand::add_error(bsoncxx::builder::basic::document& response, const ComERR& err)
{
    bsoncxx::builder::basic::array array;

    add_error(array, err, 0);

    response.append(bsoncxx::builder::basic::kvp(key::WRITE_ERRORS, array.extract()));
}

void OpMsgCommand::interpret_error(bsoncxx::builder::basic::document& error, const ComERR& err, int index)
{
    error.append(bsoncxx::builder::basic::kvp(key::INDEX, index));
    error.append(bsoncxx::builder::basic::kvp(key::CODE, error::from_mariadb_code(err.code())));
    error.append(bsoncxx::builder::basic::kvp(key::ERRMSG, err.message()));
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
    doc.append(kvp(key::KIND, value::IMMEDIATE));

    DocumentBuilder response;
    populate_response(response);

    doc.append(kvp(key::RESPONSE, response.extract()));
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
    doc.append(kvp(key::KIND, value::SINGLE));
    doc.append(kvp(key::SQL, generate_sql()));
}

void MultiCommand::diagnose(DocumentBuilder& doc)
{
    doc.append(kvp(key::KIND, value::MULTI));
    const auto& query = generate_sql();

    ArrayBuilder sql;
    for (const auto& statement : query.statements())
    {
        sql.append(statement);
    }

    doc.append(kvp(key::SQL, sql.extract()));
}

}
