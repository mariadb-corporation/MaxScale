/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include "nosqlcommand.hh"
#include <string>
#include <sstream>
#include <utility>
#include <vector>
#include <maxscale/buffer.hh>
#include "../../filter/masking/mysql.hh"
#include "nosqldatabase.hh"
#include "nosql.hh"

namespace nosql
{

//
// PacketCommand
//
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
// TableCreating (command)
//
template<class T>
class TableCreating : public T
{
public:
    using T::T;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override final
    {
        State state;

        if (m_creating_table)
        {
            state = translate_create_table(std::move(mariadb_response), ppResponse);
        }
        else
        {
            state = translate2(std::move(mariadb_response), ppResponse);
        }

        return state;
    }

protected:
    virtual State translate2(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) = 0;

    virtual State table_created(GWBUF** ppResponse) = 0;

    void create_table()
    {
        const auto& config = T::m_database.config();

        if (!config.auto_create_tables)
        {
            std::ostringstream ss;
            ss << "Table " << T::table() << " does not exist, and 'auto_create_tables' "
               << "is false.";

            throw HardError(ss.str(), error::COMMAND_FAILED);
        }

        mxb_assert(!m_creating_table);
        m_creating_table = true;

        bool if_not_exists = true;

        std::ostringstream sql;

        if (config.auto_create_databases)
        {
            sql << "CREATE DATABASE IF NOT EXISTS `" << T::m_database.name() << "`; ";
        }

        sql << table_create_statement(T::table(), config.id_length, if_not_exists);

        T::send_downstream_via_loop(sql.str());
    }

private:
    State translate_create_table(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_creating_table);
        m_creating_table = false;

        State state = State::BUSY;

        uint8_t* pBuffer = mariadb_response.data();
        uint8_t* pEnd = pBuffer + mariadb_response.length();

        if (T::m_database.config().auto_create_databases)
        {
            ComResponse create_database_response(&pBuffer);

            switch (create_database_response.type())
            {
            case ComResponse::OK_PACKET:
                {
                    ComResponse create_table_response(&pBuffer);

                    state = translate_create_table(create_table_response, ppResponse);
                }
                break;

            case ComResponse::ERR_PACKET:
                throw MariaDBError(ComERR(create_database_response));
                break;

            default:
                T::throw_unexpected_packet();
            }
        }
        else
        {
            ComResponse create_table_response(&pBuffer);

            state = translate_create_table(create_table_response, ppResponse);
        }

        return state;
    }

    State translate_create_table(const ComResponse& create_table_response, GWBUF** ppResponse)
    {
        State state = State::BUSY;

        switch (create_table_response.type())
        {
        case ComResponse::OK_PACKET:
            state = table_created(ppResponse);
            break;

        case ComResponse::ERR_PACKET:
            throw MariaDBError(ComERR(create_table_response));
            break;

        default:
            T::throw_unexpected_packet();
        }

        return state;
    }

private:
    bool m_creating_table { false };
};

//
// OpDeleteCommand
//
class OpDeleteCommand final : public PacketCommand<packet::Delete>
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
class OpInsertCommand final : public TableCreating<PacketCommand<packet::Insert>>
{
public:
    OpInsertCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    packet::Insert&& req)
        : TableCreating<PacketCommand<packet::Insert>>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
    {
        mxb_assert(m_req.documents().size() == 1);
    }

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate2(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

    State table_created(GWBUF** ppResponse) override final;

private:
    std::string convert_document_data(const bsoncxx::document::view& doc);

private:
    std::string                           m_statement;
    std::vector<bsoncxx::document::value> m_stashed_documents;
};

//
// OpUpdateCommand
//
class OpUpdateCommand final : public TableCreating<PacketCommand<packet::Update>>
{
public:
    OpUpdateCommand(Database* pDatabase,
                    GWBUF* pRequest,
                    packet::Update&& req)
        : TableCreating<PacketCommand<packet::Update>>(pDatabase, pRequest, std::move(req), ResponseKind::NONE)
    {
    }

    ~OpUpdateCommand();

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate2(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

    State table_created(GWBUF** ppResponse) override final;

private:
    enum class Action
    {
        UPDATING_DOCUMENT,
        INSERTING_DOCUMENT,
    };

    State translate_updating_document(ComResponse& response);
    State translate_inserting_document(ComResponse& response);

    enum class Send
    {
        DIRECTLY,
        VIA_LOOP
    };

    void update_document(const std::string& sql, Send send);
    State insert_document();

    Action                       m_action { Action::UPDATING_DOCUMENT };
    std::string                  m_update;
    std::string                  m_insert;
    std::unique_ptr<NoError::Id> m_sId;
};

//
// OpQueryCommand
//
class OpQueryCommand final : public PacketCommand<packet::Query>
{
public:
    OpQueryCommand(Database* pDatabase,
                   GWBUF* pRequest,
                   packet::Query&& req);

    bool session_must_be_ready() const override;

    std::string description() const override;

    State execute(GWBUF** ppNoSQL_response) override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override final;

private:
    void send_query(const bsoncxx::document::view& query,
                    const bsoncxx::document::element& orderby = bsoncxx::document::element());

private:
    enum class Kind
    {
        EMPTY,
        IS_MASTER,
        QUERY,
        IMPLICIT_QUERY,
    };

    int32_t                  m_nReturn      { DEFAULT_CURSOR_RETURN };
    bool                     m_single_batch { false };
    std::vector<std::string> m_extractions;
    Kind                     m_kind         { Kind::EMPTY };
};

//
// OpGetMoreCommand
//
class OpGetMoreCommand final : public PacketCommand<packet::GetMore>
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
class OpKillCursorsCommand final : public PacketCommand<packet::KillCursors>
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

    void authenticate() override final;

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
        return nosql::optional(m_name, doc, zKey, pElement, error_code, conversion);
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const char* zKey,
                  Type* pElement,
                  Conversion conversion = Conversion::STRICT) const
    {
        return nosql::optional(m_name, doc, zKey, pElement, conversion);
    }

    template<class Type>
    bool optional(const bsoncxx::document::view& doc,
                  const std::string& key,
                  Type* pElement,
                  Conversion conversion = Conversion::STRICT) const
    {
        return optional(doc, key.c_str(), pElement, conversion);
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
        return optional(m_doc, zKey, pElement, conversion);
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

    bool session_must_be_ready() const override
    {
        return false;
    }

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

    State execute(GWBUF** ppNoSQL_response) override;
    virtual State translate(mxs::Buffer&& mariadb_response, GWBUF** ppNoSQL_response) override = 0;

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

//
// Authorize
//
template<class BaseCommand, uint32_t ROLE_MASK>
class Authorize : public BaseCommand
{
public:
    using BaseCommand::BaseCommand;

    void authorize(uint32_t role_mask) override
    {
        if ((role_mask & ROLE_MASK) != ROLE_MASK)
        {
            std::ostringstream ss;
            ss << "command " << this->m_name << " requires authentication";

            throw SoftError(ss.str(), error::UNAUTHORIZED);
        }
    }
};

//
// UserAdminAuthorize
//
// If a user has the USER_ADMIN role in the "admin" database, then
// it may create users in any database.
//
template<class BaseCommand>
class UserAdminAuthorize : public Authorize<BaseCommand, role::USER_ADMIN>
{
public:
    using Base = Authorize<BaseCommand, role::USER_ADMIN>;

    using Base::Base;

    void authorize(uint32_t role_mask) override final
    {
        role_mask |= this->m_database.context().role_mask_of("admin");

        Base::authorize(role_mask);
    }
};

}
