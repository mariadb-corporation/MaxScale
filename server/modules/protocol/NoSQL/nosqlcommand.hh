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

#pragma once

#include "nosqlprotocol.hh"
#include <string>
#include <utility>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <maxscale/buffer.hh>
#include "../../filter/masking/mysql.hh"
#include "nosql.hh"

namespace nosql
{

class Database;

namespace command
{

template<class ConcreteCommand>
struct IsAdmin
{
    static const bool is_admin { false };
};

}

//
// Command
//
class Command
{
public:
    static const int32_t MAX_PAYLOAD_LEN = 0xffffff;
    static const int32_t MAX_PACKET_LEN = MYSQL_HEADER_LEN + MAX_PAYLOAD_LEN;

    virtual ~Command();

    virtual bool is_admin() const;

    virtual bool is_silent() const
    {
        return m_response_kind == ResponseKind::NONE;
    }

    virtual bool is_get_last_error() const
    {
        return false;
    }

    virtual std::string description() const = 0;

    virtual std::string to_json() const;

    const std::string& last_statement() const
    {
        return m_last_statement;
    }

    virtual State execute(GWBUF** ppNoSQL_response) = 0;

    virtual State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) = 0;

    enum class IsError
    {
        NO,
        YES
    };

    GWBUF* create_response(const bsoncxx::document::value& doc, IsError = IsError::NO) const;

    static GWBUF* create_reply_response(int32_t request_id,
                                        int32_t response_to,
                                        int64_t cursor_id,
                                        int32_t position,
                                        size_t size_of_documents,
                                        const std::vector<bsoncxx::document::value>& documents);

    GWBUF* create_reply_response(int64_t cursor_id,
                                 int32_t position,
                                 size_t size_of_documents,
                                 const std::vector<bsoncxx::document::value>& documents) const;

    enum class ResponseKind
    {
        NONE,
        REPLY,
        MSG,
        MSG_WITH_CHECKSUM
    };

    ResponseKind response_kind() const
    {
        return m_response_kind;
    }

protected:
    Command(Database* pDatabase,
            GWBUF* pRequest,
            int32_t request_id,
            ResponseKind response_kind)
        : m_database(*pDatabase)
        , m_pRequest(gwbuf_clone(pRequest))
        , m_request_id(request_id)
        , m_response_kind(response_kind)
    {
    }

    void free_request();

    void send_downstream(const std::string& sql);

    void log_unexpected_packet();
    void throw_unexpected_packet();

    mxs::RoutingWorker& worker() const;

    Database&     m_database;
    GWBUF*        m_pRequest;
    const int32_t m_request_id;
    std::string   m_last_statement;

private:
    static std::pair<GWBUF*, uint8_t*> create_reply_response_buffer(int32_t request_id,
                                                                    int32_t response_to,
                                                                    int64_t cursor_id,
                                                                    int32_t position,
                                                                    size_t size_of_documents,
                                                                    size_t nDocuments,
                                                                    IsError is_error);

    GWBUF* create_reply_response(const bsoncxx::document::value& doc, IsError is_error) const;

    GWBUF* create_msg_response(const bsoncxx::document::value& doc) const;

    ResponseKind m_response_kind;
};

template<class Packet>
class PacketCommand : public Command
{
protected:
    PacketCommand(Database* pDatabase,
                  GWBUF* pRequest,
                  Packet&& req,
                  ResponseKind response_kind)
        : Command(pDatabase, pRequest, req.request_id(), response_kind)
        ,  m_req(std::move(req))
    {
    }

protected:
    enum Quoted
    {
        NO,
        YES
    };

    std::string table(Quoted quoted = Quoted::YES) const
    {
        if (quoted == Quoted::YES)
        {
            const auto& collection = m_req.collection();

            auto n = collection.find('.');

            auto d = collection.substr(0, n);
            auto t = collection.substr(n + 1);

            return '`' + d + "`.`" + t + '`';
        }
        else
        {
            return m_req.collection();
        }
    }

    Packet m_req;
};

//
// OpDeleteCommand
//
class OpDeleteCommand : public PacketCommand<packet::Delete>
{
public:
    OpDeleteCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    packet::Delete&& req)
        : PacketCommand<packet::Delete>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
    {
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;
};

//
// OpInsertCommand
//
class OpInsertCommand : public PacketCommand<packet::Insert>
{
public:
    enum Action
    {
        INSERTING_DATA,
        CREATING_TABLE,
        CREATING_DATABASE
    };

    OpInsertCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    packet::Insert&& req)
        : PacketCommand<packet::Insert>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
        , m_action(INSERTING_DATA)
    {
        mxb_assert(m_req.documents().size() == 1);
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    std::string convert_document_data(const bsoncxx::document::view& doc);

private:
    Action                                m_action;
    std::string                           m_statement;
    std::vector<bsoncxx::document::value> m_stashed_documents;
};

//
// OpUpdateCommand
//
class OpUpdateCommand : public PacketCommand<packet::Update>
{
public:
    OpUpdateCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    packet::Update&& req)
        : PacketCommand<packet::Update>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
    {
    }

    ~OpUpdateCommand();

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    enum class Action
    {
        UPDATING_DOCUMENT,
        INSERTING_DOCUMENT,
        CREATING_TABLE
    };

    State translate_updating_document(ComResponse& response);
    State translate_inserting_document(ComResponse& response);
    State translate_creating_table(ComResponse& response);

    State update_document(const std::string& sql);
    State create_table();
    State insert_document();

    Action                       m_action { Action::UPDATING_DOCUMENT };
    uint32_t                     m_dcid { 0 };
    std::string                  m_update;
    std::string                  m_insert;
    std::unique_ptr<NoError::Id> m_sId;
};

//
// OpQueryCommand
//
class OpQueryCommand : public PacketCommand<packet::Query>
{
public:
    OpQueryCommand(Database* pDatabase,
                   GWBUF* pRequest,
                   packet::Query&& req)
        : PacketCommand<packet::Query>(pDatabase, pRequest, std::move(req), ResponseKind::REPLY)
    {
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    void send_query(const bsoncxx::document::view& query,
                    const bsoncxx::document::element& orderby = bsoncxx::document::element());

private:
    int32_t                  m_nReturn      { DEFAULT_CURSOR_RETURN };
    bool                     m_single_batch { false };
    std::vector<std::string> m_extractions;
};

//
// OpGetMoreCommand
//
class OpGetMoreCommand : public PacketCommand<packet::GetMore>
{
public:
    OpGetMoreCommand(Database* pDatabase,
                     GWBUF* pRequest,
                     packet::GetMore&& req)
        : PacketCommand<packet::GetMore>(pDatabase, pRequest, std::move(req), ResponseKind::REPLY)
    {
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;
};

//
// OpKillCursorsCommand
//
class OpKillCursorsCommand : public PacketCommand<packet::KillCursors>
{
public:
    OpKillCursorsCommand(Database* pDatabase,
                         GWBUF* pRequest,
                         packet::KillCursors&& req)
        : PacketCommand<packet::KillCursors>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
    {
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;
};

//
// OpMsgCommand
//
class OpMsgCommand : public Command
{
public:
    using DocumentVector = std::vector<bsoncxx::document::view>;
    using DocumentArguments = std::unordered_map<std::string, DocumentVector>;

    OpMsgCommand(const std::string& name,
                 Database* pDatabase,
                 GWBUF* pRequest,
                 packet::Msg&& req)
        : Command(pDatabase, pRequest, req.request_id(), response_kind(req))
        , m_name(name)
        , m_req(std::move(req))
        , m_doc(m_req.document())
        , m_arguments(m_req.arguments())
    {
    }

    OpMsgCommand(const std::string& name,
                 Database* pDatabase,
                 GWBUF* pRequest,
                 packet::Msg&& req,
                 const bsoncxx::document::view& doc,
                 const DocumentArguments& arguments)
        : Command(pDatabase, pRequest, req.request_id(), response_kind(req))
        , m_name(name)
        , m_req(std::move(req))
        , m_doc(doc)
        , m_arguments(arguments)
    {
    }

    static std::unique_ptr<OpMsgCommand> get(Database* pDatabase,
                                             GWBUF* pRequest,
                                             packet::Msg&& req);

    static std::unique_ptr<OpMsgCommand> get(Database* pDatabase,
                                             GWBUF* pRequest,
                                             packet::Msg&& req,
                                             const bsoncxx::document::view& doc,
                                             const DocumentArguments& arguments);

    ~OpMsgCommand() override;

    const std::string& name() const
    {
        return m_name;
    }

    bool is_silent() const override
    {
        return m_req.more_to_come();
    }

    std::string description() const override
    {
        return "OP_MSG(" + m_name + ")";
    }

    virtual void diagnose(DocumentBuilder& doc) = 0;

    std::string to_json() const override
    {
        return bsoncxx::to_json(m_doc);
    }

    const bsoncxx::document::view& doc() const
    {
        return m_doc;
    }

    GWBUF* create_empty_response() const;

    static void check_write_batch_size(int size);

    enum Quoted
    {
        NO,
        YES
    };

    /**
     * Returns the table name of the command. Meaningful only if the value of
     * the command key, is the targeted collection/table.
     *
     * @returns The table name, with or without quotes.
     * @throws SoftError, if the value of the command key is not a string.
     */
    const std::string& table(Quoted quoted = Quoted::YES) const;

    static void list_commands(DocumentBuilder& commands);

protected:
    void require_admin_db();

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const char* zKey,
                  Type* pElement,
                  int error_code,
                  Conversion conversion = Conversion::STRICT) const
    {
        bool rv = false;

        auto element = doc[zKey];

        if (element)
        {
            *pElement = element_as<Type>(m_name, zKey, element, error_code, conversion);
            rv = true;
        }

        return rv;
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const char* zKey,
                  Type* pElement,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(doc, zKey, pElement, error::TYPE_MISMATCH, conversion);
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const std::string& key,
                  Type* pElement,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(doc, key.c_str(), pElement, error::TYPE_MISMATCH, conversion);
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const std::string& key,
                  Type* pElement,
                  int error_code,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(doc, key.c_str(), pElement, error_code, conversion);
    }

    template<class Type>
    bool optional(const char* zKey,
                  Type* pElement,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, zKey, pElement, error::TYPE_MISMATCH, conversion);
    }

    template<class Type>
    bool optional(const char* zKey,
                  Type* pElement,
                  int error_code,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, zKey, pElement, error_code, conversion);
    }

    template<class Type>
    bool optional(const std::string& key, Type* pElement, Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, key.c_str(), pElement, conversion);
    }

    template<class Type>
    bool optional(const std::string& key,
                  Type* pElement,
                  int error_code,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, key.c_str(), pElement, error_code, conversion);
    }

    template<class Type>
    Type required(const char* zKey, Conversion conversion = Conversion::STRICT) const
    {
        auto element = m_doc[zKey];

        if (!element)
        {
            std::ostringstream ss;
            ss << "BSON field '" << m_name << "." << zKey << "' is missing but a required field";
            throw SoftError(ss.str(), error::LOCATION40414);
        }

        return element_as<Type>(m_name, zKey, element, conversion);
    }

    /**
     * Converts the values of a 'skip' and 'limit' to the corresponding
     * LIMIT clause.
     *
     * @return A LIMIT clause, if 'skip' and/or 'limit' are present in the
     *         command object, otherwise an empty string.
     */
    enum class AcceptAsLimit
    {
        POSITIVE_INTEGER,
        INTEGER           // The absolute value is used.
    };
    std::string convert_skip_and_limit(AcceptAsLimit limit = AcceptAsLimit::POSITIVE_INTEGER) const;

    template<class T>
    T value_as(Conversion conversion = Conversion::STRICT) const
    {
        return required<T>(m_name.c_str(), conversion);
    }

    void add_error(bsoncxx::builder::basic::array& builder, const ComERR& err, int index);
    void add_error(bsoncxx::builder::basic::document& builder, const ComERR& err);

    /**
     * Add at least 'index', 'code' and 'errmsg'.
     */
    virtual void interpret_error(bsoncxx::builder::basic::document& error, const ComERR& err, int index);

    const std::string              m_name;
    const packet::Msg              m_req;
    const bsoncxx::document::view& m_doc;
    const DocumentArguments&       m_arguments;

private:
    ResponseKind response_kind(const packet::Msg& req)
    {
        return req.checksum_present() ? ResponseKind::MSG_WITH_CHECKSUM : ResponseKind::MSG;
    }

    mutable std::string m_quoted_table;
    mutable std::string m_unquoted_table;
};

/**
 * @class ImmediateCommand
 *
 * A command that generates the response immediately, without any backend activity.
 */
class ImmediateCommand : public OpMsgCommand
{
public:
    using OpMsgCommand::OpMsgCommand;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

    void diagnose(DocumentBuilder& doc) override;

protected:
    virtual void populate_response(DocumentBuilder& doc) = 0;
};

/**
 * @class SingleCommand
 *
 * A command that executes a single SQL statement against the backend, in order
 * to produce the response.
 */
class SingleCommand : public OpMsgCommand
{
public:
    using OpMsgCommand::OpMsgCommand;

    State execute(GWBUF** ppNoSQL_response) override final;

    void diagnose(DocumentBuilder& doc) override;

protected:
    virtual void prepare();

    virtual std::string generate_sql() = 0;

    std::string m_statement;
};

/**
 * @class MultiCommand
 *
 * A command that may execute multiple SQL statements against the backend, in order
 * to produce the response.
 */
class MultiCommand : public OpMsgCommand
{
public:
    using OpMsgCommand::OpMsgCommand;

    void diagnose(DocumentBuilder& doc) override;

protected:
    class Query
    {
    public:
        enum Kind
        {
            SINGLE,    // Each statement in the vector must be executed individually.
            MULTI,     // There is just one multi-statement, but there will be many replies.
            COMPOUND   // There is just one compound statement, and there is just one reply.
        };

        Query()
            : m_kind(SINGLE)
            , m_nStatements(0)
        {
        }

        Query(std::vector<std::string>&& statements)
            : m_kind(SINGLE)
            , m_nStatements(statements.size())
            , m_statements(std::move(statements))
        {
        }

        Query(std::string&& statement)
            : m_kind(SINGLE)
            , m_nStatements(1)
        {
            m_statements.emplace_back(std::move(statement));
        }

        Query(Kind kind, size_t nStatements, std::string&& multi_statement)
            : m_kind(kind)
            , m_nStatements(nStatements)
        {
            m_statements.emplace_back(std::move(multi_statement));
        }

        Query(Query&& rhs) = default;
        Query& operator = (Query&&) = default;

        Kind kind() const
        {
            return m_kind;
        }

        size_t nStatements() const
        {
            return m_nStatements;
        }

        const std::vector<std::string>& statements() const
        {
            return m_statements;
        }


    private:
        Kind                     m_kind;
        size_t                   m_nStatements;
        std::vector<std::string> m_statements;
    };

    virtual Query generate_sql() = 0;
};


}
