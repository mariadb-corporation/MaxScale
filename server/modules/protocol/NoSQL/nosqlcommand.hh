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
    // For the time being, the requirement is that the SQL a document corresponds to
    // must fit into a single COM_QUERY packet.
    // The first -1 is the command byte and the second to ensure that the length stays
    // below 0xffffff, as a length of exactly that would require an additional empty
    // packet to be sent.
    static const int32_t MAX_QUERY_LEN = (GW_MYSQL_MAX_PACKET_LEN - MYSQL_HEADER_LEN - 1 - 1);

    virtual ~Command();

    virtual bool is_admin() const;

    virtual std::string description() const = 0;

    virtual std::string to_json() const;

    const std::string& last_statement() const
    {
        return m_last_statement;
    }

    virtual GWBUF* execute() = 0;

    enum State
    {
        BUSY,
        READY
    };

    virtual State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) = 0;

    GWBUF* create_response(const bsoncxx::document::value& doc) const;

    static void check_maximum_sql_length(int length);
    static void check_maximum_sql_length(const std::string& s)
    {
        check_maximum_sql_length(s.length());
    }

protected:
    enum class ResponseKind
    {
        REPLY,
        MSG,
        MSG_WITH_CHECKSUM
    };

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

    Database&     m_database;
    GWBUF*        m_pRequest;
    const int32_t m_request_id;
    std::string   m_last_statement;

private:
    std::pair<GWBUF*, uint8_t*> create_reply_response_buffer(size_t size_of_documents,
                                                             size_t nDocuments) const;

    GWBUF* create_reply_response(size_t size_of_documents,
                                 const std::vector<bsoncxx::document::value>& documents) const;
    GWBUF* create_reply_response(const bsoncxx::document::value& doc) const;

    GWBUF* create_msg_response(const bsoncxx::document::value& doc) const;

    ResponseKind m_response_kind;
};

template<class Packet>
class PacketCommand : public Command
{
protected:
    PacketCommand(Database* pDatabase,
                  GWBUF* pRequest,
                  Packet&& req)
        : Command(pDatabase, pRequest, req.request_id(), ResponseKind::REPLY)
        ,  m_req(std::move(req))
    {
    }

protected:
    Packet m_req;
};

//
// OpDeleteCommand
//
class OpDeleteCommand : public PacketCommand<nosql::Delete>
{
public:
    OpDeleteCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    nosql::Delete&& req)
        : PacketCommand<nosql::Delete>(pDatabase, pRequest, std::move(req))
    {
    }

    std::string description() const override;

    GWBUF* execute() override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    std::string table() const;
};

//
// OpInsertCommand
//
class OpInsertCommand : public PacketCommand<nosql::Insert>
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
                    nosql::Insert&& req)
        : PacketCommand<nosql::Insert>(pDatabase, pRequest, std::move(req))
        , m_action(INSERTING_DATA)
    {
        mxb_assert(m_req.documents().size() == 1);
    }

    std::string description() const override;

    GWBUF* execute() override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    std::string convert_document_data(const bsoncxx::document::view& doc);

    std::string table() const;

private:
    Action                                m_action;
    std::string                           m_statement;
    std::vector<bsoncxx::document::value> m_stashed_documents;
};

//
// OpMsgCommand
//
class OpMsgCommand : public Command
{
public:
    using DocumentVector = std::vector<bsoncxx::document::view>;
    using DocumentArguments = std::unordered_map<std::string, DocumentVector>;

    template<class ConcretePacket>
    OpMsgCommand(const std::string& name,
                 Database* pDatabase,
                 GWBUF* pRequest,
                 const ConcretePacket& req,
                 const bsoncxx::document::view& doc,
                 const DocumentArguments& arguments)
        : Command(pDatabase, pRequest, req.request_id(), response_kind(req))
        , m_name(name)
        , m_req(req)
        , m_doc(doc)
        , m_arguments(arguments)
    {
    }

    static std::unique_ptr<OpMsgCommand> get(nosql::Database* pDatabase,
                                             GWBUF* pRequest,
                                             const nosql::Query& req,
                                             const bsoncxx::document::view& doc,
                                             const DocumentArguments& arguments);

    static std::unique_ptr<OpMsgCommand> get(nosql::Database* pDatabase,
                                             GWBUF* pRequest,
                                             const nosql::Msg& req,
                                             const bsoncxx::document::view& doc,
                                             const DocumentArguments& arguments);

    ~OpMsgCommand() override;

    const std::string& name() const
    {
        return m_name;
    }

    std::string description() const override
    {
        return m_req.opcode() == MONGOC_OPCODE_QUERY ? "OP_QUERY" : ("OP_MSG(" + m_name + ")");
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
                  const char* zKey, Type* pElement, Conversion conversion = Conversion::STRICT) const
    {
        bool rv = false;

        auto element = doc[zKey];

        if (element)
        {
            *pElement = element_as<Type>(m_name, zKey, element, conversion);
            rv = true;
        }

        return rv;
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const std::string& key, Type* pElement, Conversion conversion = Conversion::STRICT) const
    {
        return optional(doc, key.c_str(), pElement, conversion);
    }

    template<class Type>
    bool optional(const char* zKey, Type* pElement, Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, zKey, pElement, conversion);
    }

    template<class Type>
    bool optional(const std::string& key, Type* pElement, Conversion conversion = Conversion::STRICT) const
    {
        return optional(m_doc, key.c_str(), pElement, conversion);
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
    std::string convert_skip_and_limit() const;

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

    const std::string       m_name;
    Packet                  m_req;
    bsoncxx::document::view m_doc;
    DocumentArguments       m_arguments;

private:
    ResponseKind response_kind(const Msg& req)
    {
        return req.checksum_present() ? ResponseKind::MSG_WITH_CHECKSUM : ResponseKind::MSG;
    }

    ResponseKind response_kind(const Query&)
    {
        return ResponseKind::REPLY;
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

    GWBUF* execute() override final;

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

    GWBUF* execute() override final;

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
