/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
#include "commands/authentication.hh"
#include "commands/user_management.hh"
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
        if (m_database.config().log_unknown_command)
        {
            MXS_WARNING("Unknown command: %s", bsoncxx::to_json(m_doc).c_str());
        }

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
            throw nosql::SoftError(s, nosql::error::COMMAND_NOT_FOUND);
            break;

        case GlobalConfig::RETURN_EMPTY:
            break;
        }
    }
};

using namespace nosql;

template<class ConcreteCommand>
unique_ptr<OpMsgCommand> create_default_command(const string& name,
                                                Database* pDatabase,
                                                GWBUF* pRequest,
                                                packet::Msg&& msg)
{
    unique_ptr<ConcreteCommand> sCommand;

    sCommand.reset(new ConcreteCommand(name, pDatabase, pRequest, std::move(msg)));

    return sCommand;
}

template<class ConcreteCommand>
unique_ptr<OpMsgCommand> create_diagnose_command(const string& name,
                                                 Database* pDatabase,
                                                 GWBUF* pRequest,
                                                 packet::Msg&& msg,
                                                 const bsoncxx::document::view& doc,
                                                 const OpMsgCommand::DocumentArguments& arguments)
{
    unique_ptr<ConcreteCommand> sCommand;

    sCommand.reset(new ConcreteCommand(name, pDatabase, pRequest, std::move(msg), doc, arguments));

    return sCommand;
}

using CreateDefaultFunction = unique_ptr<OpMsgCommand> (*)(const string& name,
                                                           Database* pDatabase,
                                                           GWBUF* pRequest,
                                                           packet::Msg&& msg);

using CreateDiagnoseFunction = unique_ptr<OpMsgCommand> (*)(const string& name,
                                                            Database* pDatabase,
                                                            GWBUF* pRequest,
                                                            packet::Msg&& msg,
                                                            const bsoncxx::document::view& doc,
                                                            const OpMsgCommand::DocumentArguments& arguments);

struct CommandInfo
{
    CommandInfo()
        : zKey(nullptr)
        , zHelp(nullptr)
        , create_default(nullptr)
        , create_diagnose(nullptr)
        , is_admin(false)
    {
    }

    CommandInfo(const char* zKey, const char* zHelp,
                CreateDefaultFunction create_default,
                CreateDiagnoseFunction create_diagnose,
                bool is_admin)
        : zKey(zKey)
        , zHelp(zHelp)
        , create_default(create_default)
        , create_diagnose(create_diagnose)
        , is_admin(is_admin)
    {
    }

    const char*           zKey;
    const char*           zHelp;
    CreateDefaultFunction create_default;
    CreateDiagnoseFunction  create_diagnose;
    bool                  is_admin;
};

template<class ConcreteCommand>
CommandInfo create_info()
{
    return CommandInfo(ConcreteCommand::KEY,
                       ConcreteCommand::HELP,
                       &create_default_command<ConcreteCommand>,
                       &create_diagnose_command<ConcreteCommand>,
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
        // NOTE: This *MUST* be kept in alphabetical order.
        { tolower(command::BuildInfo::KEY),                create_info<command::BuildInfo>() },
        { tolower(command::Count::KEY),                    create_info<command::Count>() },
        { tolower(command::Create::KEY),                   create_info<command::Create>() },
        { tolower(command::CreateIndexes::KEY),            create_info<command::CreateIndexes>() },
        //Cannot be included as a mockup, causes hangs.
        //{ tolower(command::CurrentOp::KEY),                create_info<command::CurrentOp>() },
        { tolower(command::Delete::KEY),                   create_info<command::Delete>() },
        { tolower(command::Distinct::KEY),                 create_info<command::Distinct>() },
        { tolower(command::Drop::KEY),                     create_info<command::Drop>() },
        { tolower(command::DropAllUsersFromDatabase::KEY), create_info<command::DropAllUsersFromDatabase>() },
        { tolower(command::DropDatabase::KEY),             create_info<command::DropDatabase>() },
        { tolower(command::DropIndexes::KEY),              create_info<command::DropIndexes>() },
        { tolower(command::EndSessions::KEY),              create_info<command::EndSessions>() },
        { tolower(command::Explain::KEY),                  create_info<command::Explain>() },
        { tolower(command::FSync::KEY),                    create_info<command::FSync>() },
        { tolower(command::Find::KEY),                     create_info<command::Find>() },
        { tolower(command::FindAndModify::KEY),            create_info<command::FindAndModify>() },
        { tolower(command::GetCmdLineOpts::KEY),           create_info<command::GetCmdLineOpts>() },
        { tolower(command::GetFreeMonitoringStatus::KEY),  create_info<command::GetFreeMonitoringStatus>() },
        { tolower(command::GetLastError::KEY),             create_info<command::GetLastError>() },
        { tolower(command::GetLog::KEY),                   create_info<command::GetLog>() },
        { tolower(command::GetMore::KEY),                  create_info<command::GetMore>() },
        { tolower(command::HostInfo::KEY),                 create_info<command::HostInfo>() },
        { tolower(command::Insert::KEY),                   create_info<command::Insert>() },
        { tolower(command::IsMaster::KEY),                 create_info<command::IsMaster>() },
        { tolower(command::KillCursors::KEY),              create_info<command::KillCursors>() },
        { tolower(command::ListCollections::KEY),          create_info<command::ListCollections>() },
        { tolower(command::ListCommands::KEY),             create_info<command::ListCommands>() },
        { tolower(command::ListDatabases::KEY),            create_info<command::ListDatabases>() },
        { tolower(command::ListIndexes::KEY),              create_info<command::ListIndexes>() },
        { tolower(command::Logout::KEY),                   create_info<command::Logout>() },
        { tolower(command::MxsCreateDatabase::KEY),        create_info<command::MxsCreateDatabase>() },
        { tolower(command::MxsDiagnose::KEY),              create_info<command::MxsDiagnose>() },
        { tolower(command::MxsGetConfig::KEY),             create_info<command::MxsGetConfig>() },
        { tolower(command::MxsSetConfig::KEY),             create_info<command::MxsSetConfig>() },
        { tolower(command::Ping::KEY),                     create_info<command::Ping>() },
        { tolower(command::RenameCollection::KEY),         create_info<command::RenameCollection>() },
        { tolower(command::ReplSetGetStatus::KEY),         create_info<command::ReplSetGetStatus>() },
        { tolower(command::ResetError::KEY),               create_info<command::ResetError>() },
        { tolower(command::ServerStatus::KEY),             create_info<command::ServerStatus>() },
        { tolower(command::SetParameter::KEY),             create_info<command::SetParameter>() },
        { tolower(command::Update::KEY),                   create_info<command::Update>() },
        { tolower(command::Validate::KEY),                 create_info<command::Validate>() },
        { tolower(command::WhatsMyUri::KEY),               create_info<command::WhatsMyUri>() },
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

    if (m_dcid != 0)
    {
        m_database.context().worker().cancel_delayed_call(m_dcid);
        m_dcid = 0;
    }
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

namespace
{

GWBUF* create_packet(const char* zSql, size_t sql_len, uint8_t seq_no)
{
    bool is_first = (seq_no == 0);

    size_t payload_len = sql_len + (is_first ? 1 : 0);
    mxb_assert(payload_len <= Command::MAX_PAYLOAD_LEN);

    GWBUF* pPacket = gwbuf_alloc(MYSQL_HEADER_LEN + payload_len);

    uint8_t* p = (uint8_t*)pPacket->start;
    *p++ = payload_len;
    *p++ = (payload_len >> 8);
    *p++ = (payload_len >> 16);
    *p++ = seq_no;

    if (is_first)
    {
        *p++ = 0x03;
    }

    memcpy(p, zSql, sql_len);

    return pPacket;
}

}

void Command::send_downstream(const string& sql)
{
    if (m_database.config().should_log_out())
    {
        MXB_NOTICE("SQL: %s", sql.c_str());
    }

    uint8_t seq_no = 0;

    const char* zSql = sql.data();
    const char* end = zSql + sql.length();

    size_t nRemaining = end - zSql;
    size_t payload_len = (nRemaining + 1 > MAX_PAYLOAD_LEN ? MAX_PAYLOAD_LEN : nRemaining + 1);
    size_t sql_len = payload_len - 1; // First packet, 1 byte for the command byte.

    GWBUF* pPacket = create_packet(zSql, sql_len, seq_no++);

    m_database.context().downstream().routeQuery(pPacket);

    zSql += sql_len;
    nRemaining -= sql_len;

    while (nRemaining != 0 || payload_len == MAX_PAYLOAD_LEN)
    {
        payload_len = (nRemaining > MAX_PAYLOAD_LEN ? MAX_PAYLOAD_LEN : nRemaining);
        sql_len = payload_len; // NOT first packet, no command byte is present.

        pPacket = create_packet(zSql, sql_len, seq_no++);

        m_database.context().downstream().routeQuery(pPacket);

        zSql += sql_len;
        nRemaining -= sql_len;
    }

    m_last_statement = sql;
}

void Command::send_downstream_via_loop(const string& sql)
{
    mxb_assert(m_dcid == 0);

    m_dcid = m_database.context().worker().delayed_call(0, [this, sql](Worker::Call::action_t action) {
            m_dcid = 0;

            if (action == Worker::Call::EXECUTE)
            {
                send_downstream(sql);
            }

            return false;
        });
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

mxs::RoutingWorker& Command::worker() const
{
    return m_database.context().worker();
}

GWBUF* Command::create_response(const bsoncxx::document::value& doc, IsError is_error) const
{
    GWBUF* pResponse = nullptr;

    if (!is_silent())
    {
        switch (m_response_kind)
        {
        case ResponseKind::REPLY:
            pResponse = create_reply_response(doc, is_error);
            break;

        case ResponseKind::MSG:
        case ResponseKind::MSG_WITH_CHECKSUM:
            pResponse = create_msg_response(doc);
            break;

        case ResponseKind::NONE:
            mxb_assert(!true);
        }
    }

    return pResponse;
}

void Command::log_back(const char* zContext, const bsoncxx::document::value& doc) const
{
    if (m_database.config().should_log_back())
    {
        MXS_NOTICE("%s: %s", zContext, bsoncxx::to_json(doc).c_str());
    }
}

//static
pair<GWBUF*, uint8_t*> Command::create_reply_response_buffer(int32_t request_id,
                                                             int32_t response_to,
                                                             int64_t cursor_id,
                                                             int32_t starting_from,
                                                             size_t size_of_documents,
                                                             size_t nDocuments,
                                                             IsError is_error)
{
    // TODO: In the following is assumed that whatever is returned will
    // TODO: fit into a MongoDB packet.

    int32_t response_flags = 0;
    if (is_error == IsError::YES)
    {
        response_flags |= MONGOC_REPLY_QUERY_FAILURE;
    }
    int32_t number_returned = nDocuments;

    size_t response_size = protocol::HEADER_LEN
        + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
        + size_of_documents;

    GWBUF* pResponse = gwbuf_alloc(response_size);

    auto* pRes_hdr = reinterpret_cast<protocol::HEADER*>(GWBUF_DATA(pResponse));
    pRes_hdr->msg_len = response_size;
    pRes_hdr->request_id = request_id;
    pRes_hdr->response_to = response_to;
    pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

    uint8_t* pData = GWBUF_DATA(pResponse) + protocol::HEADER_LEN;

    pData += protocol::set_byte4(pData, response_flags);
    pData += protocol::set_byte8(pData, cursor_id);
    pData += protocol::set_byte4(pData, starting_from);
    pData += protocol::set_byte4(pData, number_returned);

    return make_pair(pResponse, pData);
}

//static
GWBUF* Command::create_reply_response(int32_t request_id,
                                      int32_t response_to,
                                      int64_t cursor_id,
                                      int32_t position,
                                      size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents)
{
    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(request_id,
                                                         response_to,
                                                         cursor_id,
                                                         position,
                                                         size_of_documents,
                                                         documents.size(),
                                                         IsError::NO);

    for (const auto& doc : documents)
    {
        auto view = doc.view();
        size_t size = view.length();

        memcpy(pData, view.data(), view.length());
        pData += view.length();
    }

    return pResponse;
}

GWBUF* Command::create_reply_response(int64_t cursor_id,
                                      int32_t position,
                                      size_t size_of_documents,
                                      const vector<bsoncxx::document::value>& documents) const
{
    return create_reply_response(m_database.context().next_request_id(),
                                 m_request_id,
                                 cursor_id,
                                 position,
                                 size_of_documents,
                                 documents);
}

GWBUF* Command::create_reply_response(const bsoncxx::document::value& doc, IsError is_error) const
{
    log_back("Response(Reply)", doc);

    auto doc_view = doc.view();
    size_t doc_len = doc_view.length();

    GWBUF* pResponse;
    uint8_t* pData;

    tie(pResponse, pData) = create_reply_response_buffer(m_database.context().next_request_id(), m_request_id,
                                                         0, 0, doc_len, 1, is_error);

    memcpy(pData, doc_view.data(), doc_view.length());

    return pResponse;
}

GWBUF* Command::create_msg_response(const bsoncxx::document::value& doc) const
{
    log_back("Response(Msg)", doc);

    uint32_t flag_bits = 0;
    uint8_t kind = 0;
    uint32_t doc_length = doc.view().length();

    size_t response_size = protocol::HEADER_LEN + sizeof(flag_bits) + sizeof(kind) + doc_length;

    bool append_checksum = (m_response_kind == ResponseKind::MSG_WITH_CHECKSUM);

    if (append_checksum)
    {
        flag_bits |= packet::Msg::CHECKSUM_PRESENT;
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

State OpDeleteCommand::execute(GWBUF** ppNoSQL_response)
{
    ostringstream ss;
    ss << "DELETE FROM " << table() << where_clause_from_query(m_req.selector()) << " ";

    if (m_req.is_single_remove())
    {
        ss << "LIMIT 1";
    }

    auto statement = ss.str();

    send_downstream(statement);

    *ppNoSQL_response = nullptr;
    return State::BUSY;
}

State OpDeleteCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        {
            ComOK ok(response);

            m_database.context().set_last_error(std::make_unique<NoError>(ok.affected_rows(), true));
        }
        break;

    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);

            if (err.code() != ER_NO_SUCH_TABLE)
            {
                m_database.context().set_last_error(MariaDBError(err).create_last_error());
            }
            else
            {
                m_database.context().set_last_error(std::make_unique<NoError>(0));
            }
        }
        break;

    default:
        // We do not throw, as that would generate a response.
        log_unexpected_packet();
    }

    *ppNoSQL_response = nullptr;
    return State::READY;
};

//
// OpInsertCommand
//
std::string OpInsertCommand::description() const
{
    return "OP_INSERT";
}

State OpInsertCommand::execute(GWBUF** ppNoSQL_response)
{
    mxb_assert(m_req.documents().size() == 1);

    if (m_req.documents().size() != 1)
    {
        const char* zMessage = "Currently only a single document can be insterted at a time with OP_INSERT.";
        MXS_ERROR("%s", zMessage);

        throw HardError(zMessage, error::INTERNAL_ERROR);
    }

    auto doc = m_req.documents()[0];

    ostringstream ss;
    ss << "INSERT INTO " << table() << " (doc) VALUES " << convert_document_data(doc) << ";";

    m_statement = ss.str();

    send_downstream(m_statement);

    *ppNoSQL_response = nullptr;
    return State::BUSY;
}

State OpInsertCommand::translate2(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    State state = State::BUSY;
    GWBUF* pResponse = nullptr;

    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::OK_PACKET:
        m_database.context().set_last_error(std::make_unique<NoError>(1));
        state = State::READY;
        break;

    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);
            auto s = err.message();

            switch (err.code())
            {
            case ER_NO_SUCH_TABLE:
                create_table();
                break;

            default:
                throw MariaDBError(err);
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

State OpInsertCommand::table_created(GWBUF** ppResponse)
{
    send_downstream_via_loop(m_statement);

    *ppResponse = nullptr;
    return State::BUSY;
}

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
OpUpdateCommand::~OpUpdateCommand()
{
}

string OpUpdateCommand::description() const
{
    return "OP_UPDATE";
}

State OpUpdateCommand::execute(GWBUF** ppNoSQL_response)
{
    *ppNoSQL_response = nullptr;

    ostringstream ss;
    ss << "UPDATE " << table() << " SET DOC = "
       << set_value_from_update_specification(m_req.update()) << " "
       << where_clause_from_query(m_req.selector()) << " ";

    if (!m_req.is_multi())
    {
        ss << "LIMIT 1";
    }

    auto sql = ss.str();

    return update_document(sql);
}

State OpUpdateCommand::translate2(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    State state = State::READY;

    ComResponse response(mariadb_response.data());

    auto type = response.type();
    if (type == ComResponse::OK_PACKET || type == ComResponse::ERR_PACKET)
    {
        switch (m_action)
        {
        case Action::UPDATING_DOCUMENT:
            state = translate_updating_document(response);
            break;

        case Action::INSERTING_DOCUMENT:
            state = translate_inserting_document(response);
            break;
        }
    }
    else
    {
        // We do not throw, as that would generate a response.
        log_unexpected_packet();
    }

    *ppNoSQL_response = nullptr;

    return state;
}

State OpUpdateCommand::translate_updating_document(ComResponse& response)
{
    State state = State::READY;

    if (response.type() == ComResponse::OK_PACKET)
    {
        ComOK ok(response);

        if (ok.matched_rows() == 0)
        {
            if (m_req.is_upsert())
            {
                if (m_insert.empty())
                {
                    // We have not attempted an insert, so let's do that.
                    state = insert_document();
                }
                else
                {
                    // An insert has been made, but now the update did not match?!

                    SoftError error("The query did not match a document, and a document "
                                    "was thus inserted, but yet there was no match.",
                                    error::COMMAND_FAILED);

                    m_database.context().set_last_error(error.create_last_error());
                }
            }
            else
            {
                m_database.context().set_last_error(std::make_unique<NoError>(0, false));
            }
        }
        else
        {
            auto n = ok.affected_rows();

            if (n == 0)
            {
                m_database.context().set_last_error(std::make_unique<NoError>(0, false));
            }
            else
            {
                if (m_insert.empty())
                {
                    // We did not try inserting anything, which means something existing was updated.
                    m_database.context().set_last_error(std::make_unique<NoError>(n, true));
                }
                else
                {
                    // Ok, so we updated an inserted document.
                    m_database.context().set_last_error(std::make_unique<NoError>(std::move(m_sId)));
                }
            }
        }
    }
    else
    {
        mxb_assert(response.type() == ComResponse::ERR_PACKET);

        ComERR err(response);

        if (err.code() == ER_NO_SUCH_TABLE)
        {
            if (m_database.config().auto_create_tables)
            {
                create_table();
                state = State::BUSY;
            }
            else
            {
                ostringstream ss;
                ss << "Table " << table() << " does not exist, and 'auto_create_tables' "
                   << "is false.";

                MXB_WARNING("%s", ss.str().c_str());
            }
        }
        else
        {
            MariaDBError mariadb_err(err);
            MXB_ERROR("%s", mariadb_err.message().c_str());
            m_database.context().set_last_error(mariadb_err.create_last_error());
        }
    }

    return state;
}

State OpUpdateCommand::translate_inserting_document(ComResponse& response)
{
    State state = State::BUSY;

    if (response.type() == ComResponse::ERR_PACKET)
    {
        ComERR err(response);
        m_database.context().set_last_error(MariaDBError(err).create_last_error());

        state = State::READY;
    }
    else
    {
        ostringstream ss;
        ss << "UPDATE " << table() << " SET DOC = "
           << set_value_from_update_specification(m_req.update())
           << " "
           << "WHERE id = '" << m_sId->to_string() << "'";

        auto sql = ss.str();

        mxb_assert(m_dcid == 0);
        m_dcid = worker().delayed_call(0, [this, sql](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    update_document(sql);
                }

            return false;
        });
    }

    return state;
}

State OpUpdateCommand::table_created(GWBUF** ppResponse)
{
    insert_document();

    *ppResponse = nullptr;
    return State::BUSY;
}

State OpUpdateCommand::update_document(const string& sql)
{
    m_action = Action::UPDATING_DOCUMENT;

    m_update = sql;

    send_downstream(m_update);

    return State::BUSY;
}

State OpUpdateCommand::insert_document()
{
    m_action = Action::INSERTING_DOCUMENT;

    ostringstream ss;
    ss << "INSERT INTO " << table() << " (doc) VALUES ('";

    auto q = m_req.selector();

    DocumentBuilder builder;

    auto qid = q[key::_ID];

    if (qid)
    {
        class ElementId : public NoError::Id
        {
        public:
            ElementId(const bsoncxx::document::element& id)
                : m_id(id)
            {
            }

            string to_string() const override
            {
                return nosql::to_string(m_id);
            }

            void append(DocumentBuilder& doc, const string& key) const override
            {
                nosql::append(doc, key, m_id);
            }

        private:
            bsoncxx::document::element m_id;
        };

        m_sId = make_unique<ElementId>(qid);
    }
    else
    {
        auto id = bsoncxx::oid();

        class ObjectId : public NoError::Id
        {
        public:
            ObjectId(const bsoncxx::oid& id)
                : m_id(id)
            {
            }

            string to_string() const override
            {
                return "{\"$oid\":\"" + m_id.to_string() + "\"}'";
            }

            void append(DocumentBuilder& doc, const string& key) const override
            {
                doc.append(kvp(key, m_id));
            }

        private:
            bsoncxx::oid m_id;
        };

        m_sId = make_unique<ObjectId>(id);

        builder.append(kvp(key::_ID, id));
    }

    for (const auto& e : q)
    {
        append(builder, e.key(), e);
    }

    ss << bsoncxx::to_json(builder.extract());

    ss << "')";

    m_insert = ss.str();

    mxb_assert(m_dcid == 0);
    m_dcid = worker().delayed_call(0, [this](Worker::Call::action_t action) {
            m_dcid = 0;

            if (action == Worker::Call::EXECUTE)
            {
                send_downstream(m_insert);
            }

            return false;
        });

    return State::BUSY;
}

//
// OpQueryCommand
//
std::string OpQueryCommand::description() const
{
    return "OP_QUERY";
}

State OpQueryCommand::execute(GWBUF** ppNoSQL_response)
{
    State state = State::BUSY;
    GWBUF* pResponse = nullptr;

    auto it = m_req.query().begin();
    auto end = m_req.query().end();

    if (it == end)
    {
        bsoncxx::document::view query;
        send_query(query);
    }
    else
    {
        for (; it != end; ++it)
        {
            auto element = *it;
            auto key = element.key();

            if (key.compare(command::IsMaster::KEY) == 0 || key.compare(key::ISMASTER) == 0)
            {
                DocumentBuilder doc;
                command::IsMaster::populate_response(m_database, m_req.query(), doc);

                pResponse = create_response(doc.extract());
                state = State::READY;
                break;
            }
            else if (key.compare(key::QUERY) == 0)
            {
                send_query(element.get_document(), m_req.query()[key::ORDERBY]);
                break;
            }
            else
            {
                ++it;
            }
        }

        if (it == end)
        {
            // Ok, we assume the whole document is a query.
            send_query(m_req.query());
        }
    }

    *ppNoSQL_response = pResponse;
    return state;
}

State OpQueryCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    GWBUF* pResponse = nullptr;

    ComResponse response(mariadb_response.data());

    switch (response.type())
    {
    case ComResponse::ERR_PACKET:
        {
            ComERR err(response);

            auto code = err.code();

            if (code == ER_NO_SUCH_TABLE)
            {
                size_t size_of_documents = 0;
                vector<bsoncxx::document::value> documents;

                pResponse = create_reply_response(0, 0, size_of_documents, documents);
            }
            else
            {
                throw MariaDBError(err);
            }
        }
        break;

    case ComResponse::OK_PACKET:
    case ComResponse::LOCAL_INFILE_PACKET:
        mxb_assert(!true);
        throw_unexpected_packet();

    default:
        {
            unique_ptr<NoSQLCursor> sCursor = NoSQLCursor::create(table(Quoted::NO),
                                                                  m_extractions,
                                                                  std::move(mariadb_response));

            int32_t position = sCursor->position();
            size_t size_of_documents = 0;
            vector<bsoncxx::document::value> documents;

            sCursor->create_batch(worker(), m_nReturn, m_single_batch, &size_of_documents, &documents);

            int64_t cursor_id = sCursor->exhausted() ? 0 : sCursor->id();

            int32_t response_to = m_request_id;
            int32_t request_id = m_database.context().next_request_id();

            pResponse = create_reply_response(request_id, response_to,
                                              cursor_id, position, size_of_documents, documents);

            // TODO: Somewhat unclear how exhaust should interact with single_batch.
            if (m_req.is_exhaust())
            {
                // Return everything in as many reply packets as needed.
                size_t nReturn = std::numeric_limits<int32_t>::max();

                while (!sCursor->exhausted())
                {
                    int32_t position = sCursor->position();

                    documents.clear();
                    sCursor->create_batch(worker(), nReturn, false, &size_of_documents, &documents);

                    cursor_id = sCursor->exhausted() ? 0 : sCursor->id();

                    response_to = request_id;
                    request_id = m_database.context().next_request_id();

                    auto* pMore = create_reply_response(request_id, response_to,
                                                        cursor_id, position, size_of_documents, documents);

                    gwbuf_append(pResponse, pMore);
                }
            }

            if (!sCursor->exhausted())
            {
                NoSQLCursor::put(std::move(sCursor));
            }
        }
    }

    *ppNoSQL_response = pResponse;
    return State::READY;
}

void OpQueryCommand::send_query(const bsoncxx::document::view& query,
                                const bsoncxx::document::element& orderby)
{
    ostringstream sql;
    sql << "SELECT ";

    m_extractions = extractions_from_projection(m_req.fields());

    if (!m_extractions.empty())
    {
        string s;
        for (auto extraction : m_extractions)
        {
            if (!s.empty())
            {
                s += ", ";
            }

            s += "JSON_EXTRACT(doc, '$." + extraction + "')";
        }

        sql << s;
    }
    else
    {
        sql << "doc";
    }

    sql << " FROM " << table();

    if (!query.empty())
    {
        sql << where_clause_from_query(query) << " ";
    }

    if (orderby)
    {
        string s = order_by_value_from_sort(orderby.get_document());

        if (!s.empty())
        {
            sql << "ORDER BY " << s << " ";
        }
    }

    sql << "LIMIT ";

    auto nSkip = m_req.nSkip();

    if (m_req.nSkip() != 0)
    {
        sql << nSkip << ", ";
    }

    int64_t nLimit = std::numeric_limits<int64_t>::max();

    if (m_req.nReturn() < 0)
    {
        m_nReturn = -m_req.nReturn();
        nLimit = m_nReturn;
        m_single_batch = true;
    }
    else if (m_req.nReturn() == 1)
    {
        m_nReturn = 1;
        nLimit = m_nReturn;
        m_single_batch = true;
    }
    else if (m_req.nReturn() == 0)
    {
        m_nReturn = DEFAULT_CURSOR_RETURN;
    }
    else
    {
        m_nReturn = m_req.nReturn();
    }

    sql << nLimit;

    send_downstream(sql.str());
}

//
// OpGetMoreCommand
//
string OpGetMoreCommand::description() const
{
    return "OP_GET_MORE";
}

State OpGetMoreCommand::execute(GWBUF** ppNoSQL_response)
{
    auto cursor_id = m_req.cursor_id();

    unique_ptr<NoSQLCursor> sCursor = NoSQLCursor::get(m_req.collection(), m_req.cursor_id());

    int32_t position = sCursor->position();
    size_t size_of_documents;
    vector<bsoncxx::document::value> documents;

    sCursor->create_batch(worker(), m_req.nReturn(), false, &size_of_documents, &documents);

    cursor_id = sCursor->exhausted() ? 0 : sCursor->id();

    GWBUF* pResponse = create_reply_response(cursor_id, position, size_of_documents, documents);

    if (!sCursor->exhausted())
    {
        NoSQLCursor::put(std::move(sCursor));
    }

    *ppNoSQL_response = pResponse;
    return State::READY;
}

State OpGetMoreCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    mxb_assert(!true);
    *ppNoSQL_response = nullptr;
    return State::READY;
}

//
// OpKillCursorsCommand
//
string OpKillCursorsCommand::description() const
{
    return "OP_KILL_CURSORS";
}

State OpKillCursorsCommand::execute(GWBUF** ppNoSQL_response)
{
    NoSQLCursor::kill(m_req.cursor_ids());

    *ppNoSQL_response = nullptr;
    return State::READY;
}

State OpKillCursorsCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response)
{
    mxb_assert(!true);
    *ppNoSQL_response = nullptr;
    return State::READY;
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

    if (!info.create_default)
    {
        name = "unknown";
        info.create_default = &create_default_command<Unknown>;
        info.create_diagnose = &create_diagnose_command<Unknown>;
        info.is_admin = false;
    }

    return make_pair(name, info);
}

}

//static
unique_ptr<OpMsgCommand> OpMsgCommand::get(nosql::Database* pDatabase,
                                           GWBUF* pRequest,
                                           packet::Msg&& msg)
{
    auto p = get_info(msg.document());

    const string& name = p.first;
    CreateDefaultFunction create = p.second.create_default;

    return create(name, pDatabase, pRequest, std::move(msg));
}

//static
unique_ptr<OpMsgCommand> OpMsgCommand::get(nosql::Database* pDatabase,
                                           GWBUF* pRequest,
                                           packet::Msg&& msg,
                                           const bsoncxx::document::view& doc,
                                           const DocumentArguments& arguments)
{
    auto p = get_info(doc);

    const string& name = p.first;
    CreateDiagnoseFunction create = p.second.create_diagnose;

    return create(name, pDatabase, pRequest, std::move(msg), doc, arguments);
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
        command.append(kvp(key::SLAVE_OK, bsoncxx::types::b_undefined()));
        command.append(kvp(key::ADMIN_ONLY, info.is_admin));
        command.append(kvp(key::REQUIRES_AUTH, (name == "ismaster" ? false : true)));

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

string OpMsgCommand::convert_skip_and_limit(AcceptAsLimit accept_as_limit) const
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
        if (limit)
        {
            if (!get_number_as_integer(limit, &nLimit))
            {
                ostringstream ss;
                ss << "Failed to parse: " << bsoncxx::to_json(m_doc) << ". 'limit' field must be numeric.";
                throw SoftError(ss.str(), error::FAILED_TO_PARSE);
            }

            if (nLimit < 0)
            {
                if (accept_as_limit == AcceptAsLimit::INTEGER)
                {
                    nLimit = -nLimit;
                }
                else
                {
                    ostringstream ss;
                    ss << "Limit value must be non-negative, but received: " << nLimit;
                    throw SoftError(ss.str(), error::BAD_VALUE);
                }
            }
        }

        ostringstream ss;
        ss << "LIMIT ";

        if (nSkip != 0)
        {
            ss << nSkip << ", ";
        }

        if (nLimit == 0)
        {
            // A limit of 0 should have no effect.
            nLimit = std::numeric_limits<int64_t>::max();
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

        string_view table = element.get_utf8();

        if (table.length() == 0)
        {
            ostringstream ss;
            ss << "Invalid namespace specified '" << m_database.name() << ".'";
            throw SoftError(ss.str(), error::INVALID_NAMESPACE);
        }

        ostringstream ss1;
        ss1 << "`" << m_database.name() << "`.`" <<  table << "`";

        ostringstream ss2;
        ss2 << m_database.name() << "." << table;

        m_quoted_table = ss1.str();
        m_unquoted_table = ss2.str();
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
    auto code = error::from_mariadb_code(err.code());
    auto errmsg = err.message();

    error.append(bsoncxx::builder::basic::kvp(key::INDEX, index));
    error.append(bsoncxx::builder::basic::kvp(key::CODE, code));
    error.append(bsoncxx::builder::basic::kvp(key::ERRMSG, errmsg));

    m_database.context().set_last_error(std::make_unique<ConcreteLastError>(errmsg, code));
}

State ImmediateCommand::execute(GWBUF** ppNoSQL_response)
{
    DocumentBuilder doc;
    populate_response(doc);

    *ppNoSQL_response = create_response(doc.extract());
    return State::READY;
}

State ImmediateCommand::translate(mxs::Buffer&& mariadb_response, GWBUF** ppProtocol_response)
{
    // This will never be called.
    mxb_assert(!true);
    throw std::runtime_error("ImmediateCommand::translate(...) should not be called.");
    return State::READY;
}

void ImmediateCommand::diagnose(DocumentBuilder& doc)
{
    doc.append(kvp(key::KIND, value::IMMEDIATE));

    DocumentBuilder response;
    populate_response(response);

    doc.append(kvp(key::RESPONSE, response.extract()));
}

State SingleCommand::execute(GWBUF** ppNoSQL_response)
{
    prepare();

    string statement = generate_sql();

    m_statement = std::move(statement);

    send_downstream(m_statement);

    *ppNoSQL_response = nullptr;
    return State::BUSY;
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
