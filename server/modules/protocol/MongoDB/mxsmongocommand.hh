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

#pragma once

#include "mongodbclient.hh"
#include <string>
#include <utility>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <maxscale/buffer.hh>
#include "../../filter/masking/mysql.hh"
#include "mxsmongo.hh"

namespace mxsmongo
{

class Database;

class Command
{
public:
    using DocumentVector = std::vector<bsoncxx::document::view>;
    using DocumentArguments = std::unordered_map<std::string, DocumentVector>;

    template<class ConcretePacket>
    Command(const std::string& name,
            Database* pDatabase,
            GWBUF* pRequest,
            const ConcretePacket& req,
            const bsoncxx::document::view& doc,
            const DocumentArguments& arguments)
        : m_name(name)
        , m_database(*pDatabase)
        , m_pRequest(gwbuf_clone(pRequest))
        , m_req(req)
        , m_doc(doc)
        , m_arguments(arguments)
        , m_append_checksum(checksum_used(req))
    {
    }

    static std::unique_ptr<Command> get(mxsmongo::Database* pDatabase,
                                        GWBUF* pRequest,
                                        const mxsmongo::Query& req,
                                        const bsoncxx::document::view& doc,
                                        const DocumentArguments& arguments);

    static std::unique_ptr<Command> get(mxsmongo::Database* pDatabase,
                                        GWBUF* pRequest,
                                        const mxsmongo::Msg& req,
                                        const bsoncxx::document::view& doc,
                                        const DocumentArguments& arguments);

    enum State
    {
        BUSY,
        READY
    };

    virtual ~Command();

    const bsoncxx::document::view& doc() const
    {
        return m_doc;
    }

    const std::string& last_statement() const
    {
        return m_last_statement;
    }

    virtual GWBUF* execute() = 0;

    virtual State translate(mxs::Buffer&& mariadb_response, GWBUF** ppMongo_response) = 0;

    GWBUF* create_empty_response() const;

    GWBUF* create_response(const bsoncxx::document::value& doc) const;

    static void check_write_batch_size(int size);

    virtual void diagnose(DocumentBuilder& doc)
    {
        // TODO: To be made pure virtual.
        mxb_assert(!true);
    }

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

protected:
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
            std::stringstream ss;
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

    void free_request();

    void send_downstream(const std::string& sql);

    void add_error(bsoncxx::builder::basic::array& builder, const ComERR& err, int index);
    void add_error(bsoncxx::builder::basic::document& builder, const ComERR& err);

    /**
     * Add at least 'index', 'code' and 'errmsg'.
    */
    virtual void interpret_error(bsoncxx::builder::basic::document& error, const ComERR& err, int index);

    const std::string       m_name;
    Database&               m_database;
    GWBUF*                  m_pRequest;
    Packet                  m_req;
    bsoncxx::document::view m_doc;
    DocumentArguments       m_arguments;
    std::string             m_last_statement;

private:
    bool checksum_used(const Msg& req)
    {
        return req.checksum_present();
    }

    bool checksum_used(const Query&)
    {
        return false;
    }

    std::pair<GWBUF*, uint8_t*> create_reply_response_buffer(size_t size_of_documents,
                                                             size_t nDocuments) const;

    GWBUF* create_reply_response(size_t size_of_documents, const std::
                                 vector<bsoncxx::document::value>& documents) const;
    GWBUF* create_reply_response(const bsoncxx::document::value& doc) const;

    GWBUF* create_msg_response(const bsoncxx::document::value& doc) const;

    bool                m_append_checksum { false };
    mutable std::string m_quoted_table;
    mutable std::string m_unquoted_table;
};

/**
 * @class ImmediateCommand
 *
 * A command that generates the response immediately, without any backend activity.
 */
class ImmediateCommand : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override final;

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppMongo_response);

    void diagnose(DocumentBuilder& doc);

protected:
    virtual void populate_response(DocumentBuilder& doc) = 0;
};

/**
 * @class SingleCommand
 *
 * A command that executes a single SQL statement against the backend, in order
 * to produce the response.
 */
class SingleCommand : public Command
{
public:
    using Command::Command;

    GWBUF* execute() override final;

    void diagnose(DocumentBuilder& doc);

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
class MultiCommand : public Command
{
public:
    using Command::Command;

    void diagnose(DocumentBuilder& doc);

protected:
    virtual std::vector<std::string> generate_sql() = 0;
};


}
