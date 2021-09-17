/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-crud/
//

#include "defs.hh"
#include <maxbase/worker.hh>
#include "../nosqlcursor.hh"

using mxb::Worker;

namespace nosql
{

namespace command
{

class OrderedCommand : public MultiCommand
{
public:
    OrderedCommand(const std::string& name,
                   Database* pDatabase,
                   GWBUF* pRequest,
                   nosql::Msg&& req,
                   const std::string& array_key)
        : MultiCommand(name, pDatabase, pRequest, std::move(req))
        , m_key(array_key)
    {
    }

    OrderedCommand(const std::string& name,
                   Database* pDatabase,
                   GWBUF* pRequest,
                   nosql::Msg&& req,
                   const bsoncxx::document::view& doc,
                   const DocumentArguments& arguments,
                   const std::string& array_key)
        : MultiCommand(name, pDatabase, pRequest, std::move(req), doc, arguments)
        , m_key(array_key)
    {
    }

    ~OrderedCommand()
    {
        if (m_dcid)
        {
            Worker::get_current()->cancel_delayed_call(m_dcid);
        }
    }


public:
    enum class Mode
    {
        DEFAULT,
        CREATING_TABLE,
        CREATING_DATABASE
    };

    enum class Execution
    {
        CONTINUE,
        ABORT,
        BUSY
    };

    State execute(GWBUF** ppNoSQL_response) override final
    {
        auto query = generate_sql();

        for (const auto& statement : query.statements())
        {
            check_maximum_sql_length(statement);
        }

        m_query = std::move(query);

        m_it = m_query.statements().begin();

        execute_one_statement();

        *ppNoSQL_response = nullptr;
        return State::BUSY;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override final
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.

        State state = State::READY;

        switch (m_mode)
        {
        case Mode::DEFAULT:
            state = translate_default(std::move(mariadb_response), ppResponse);
            break;

        case Mode::CREATING_TABLE:
            state = translate_creating_table(std::move(mariadb_response), ppResponse);
            break;

        case Mode::CREATING_DATABASE:
            state = translate_creating_database(std::move(mariadb_response), ppResponse);
            break;
        }

        return state;
    }

    State translate_default(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        State rv = State::BUSY;
        GWBUF* pResponse = nullptr;

        uint8_t* pBuffer = mariadb_response.data();

        ComResponse response(pBuffer);

        if (response.is_err() && ComERR(response).code() == ER_NO_SUCH_TABLE && should_create_table())
        {
            rv = create_table();
        }
        else
        {
            uint8_t* pEnd = pBuffer + mariadb_response.length();

            Execution execution = Execution::CONTINUE;

            switch (m_query.kind())
            {
            case Query::MULTI:
                pBuffer = interpret_multi(pBuffer, pEnd, m_query.nStatements());
                m_ok = 1;
                break;

            case Query::COMPOUND:
                pBuffer = interpret_compound(pBuffer, pEnd, m_query.nStatements());
                m_ok = 1;
                break;

            case Query::SINGLE:
                {
                    execution = interpret_single(pBuffer);

                    pBuffer += ComPacket::packet_len(pBuffer);
                }
            }

            if (pBuffer != pEnd)
            {
                MXS_WARNING("Received %ld excess bytes, ignoring.", pEnd - pBuffer);
            }

            if (execution != Execution::BUSY)
            {
                ++m_it;

                if (m_it == m_query.statements().end() || execution == Execution::ABORT)
                {
                    DocumentBuilder doc;

                    auto write_errors = m_write_errors.extract();

                    doc.append(kvp(key::N, m_n));
                    doc.append(kvp(key::OK, m_ok));

                    amend_response(doc);

                    if (!write_errors.view().empty())
                    {
                        doc.append(kvp(key::WRITE_ERRORS, write_errors));
                    }

                    pResponse = create_response(doc.extract());
                    rv = State::READY;
                }
                else
                {
                    execute_one_statement();
                }
            }
        }

        *ppResponse = pResponse;
        return rv;
    }

    State translate_creating_table(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_mode == Mode::CREATING_TABLE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXS_INFO("Table created, now executing statment.");
            m_mode = Mode::DEFAULT;
            execute_one_statement();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_TABLE_EXISTS_ERROR)
                {
                    MXS_INFO("Table created by someone else, now executing statment.");
                    m_mode = Mode::DEFAULT;
                    execute_one_statement();
                }
                else if (code == ER_BAD_DB_ERROR && err.message().find("Unknown database") == 0)
                {
                    state = create_database();
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_database(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_mode == Mode::CREATING_DATABASE);

        State state = State::BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXS_INFO("Database created, now creating table.");
            create_table();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_DB_CREATE_EXISTS)
                {
                    MXS_INFO("Database created by someone else, now creating table.");
                    create_table();
                }
                else
                {
                    throw MariaDBError(err);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        *ppResponse = pResponse;
        return state;
    }

protected:
    virtual bool should_create_table() const
    {
        return false;
    }

    virtual bool is_acceptable_error(const ComERR&) const
    {
        return false;
    }

    Query generate_sql() override final
    {
        Query query;

        optional(key::ORDERED, &m_ordered);

        auto it = m_arguments.find(m_key);

        if (it != m_arguments.end())
        {
            m_documents = it->second;
            check_write_batch_size(m_documents.size());
        }
        else
        {
            auto documents = required<bsoncxx::array::view>(m_key.c_str());
            auto nDocuments = std::distance(documents.begin(), documents.end());
            check_write_batch_size(nDocuments);

            int i = 0;
            for (auto element : documents)
            {
                if (element.type() != bsoncxx::type::k_document)
                {
                    ostringstream ss;
                    ss << "BSON field '" << m_name << "." << m_key << "."
                       << i << "' is the wrong type '"
                       << bsoncxx::to_string(element.type())
                       << "', expected type 'object'";

                    throw SoftError(ss.str(), error::TYPE_MISMATCH);
                }

                m_documents.push_back(element.get_document());
            }
        }

        return generate_sql(m_documents);
    }

    virtual Query generate_sql(const vector<bsoncxx::document::view>& documents)
    {
        vector<string> statements;

        for (const auto& doc : documents)
        {
            statements.push_back(convert_document(doc));
        }

        return Query(std::move(statements));
    }

    virtual string convert_document(const bsoncxx::document::view& doc) = 0;

    virtual Execution interpret(const ComOK& response, int index) = 0;

    virtual Execution interpret_single(uint8_t* pBuffer)
    {
        Execution rv = Execution::CONTINUE;

        ComResponse response(pBuffer);

        auto index = m_it - m_query.statements().begin();

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                m_ok = 1;
                rv = interpret(ComOK(response), index);

                if (rv == Execution::ABORT && !m_ordered)
                {
                    rv = Execution::CONTINUE;
                }
            }
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                if (is_acceptable_error(err))
                {
                    m_ok = true;
                }
                else
                {
                    if (m_ordered)
                    {
                        rv = Execution::ABORT;
                    }

                    add_error(m_write_errors, err, index);
                }
            }
            break;

        default:
            mxb_assert(!true);
            throw_unexpected_packet();
        }

        return rv;
    }

    virtual uint8_t* interpret_multi(uint8_t* pBegin, uint8_t* pEnd, size_t nStatements)
    {
        // This is not going to happen outside development.
        mxb_assert(!true);
        throw std::runtime_error("Multi query, but no multi handler.");
    }

    virtual uint8_t* interpret_compound(uint8_t* pBegin, uint8_t* pEnd, size_t nStatements)
    {
        // This is not going to happen outside development.
        mxb_assert(!true);
        throw std::runtime_error("Compound query, but no compound handler.");
    }

    virtual void amend_response(DocumentBuilder& response)
    {
    }

    void execute_one_statement()
    {
        mxb_assert(m_it != m_query.statements().end());

        send_downstream(*m_it);
    }

    State create_table()
    {
        mxb_assert(m_mode != Mode::CREATING_TABLE);

        if (!m_database.config().auto_create_tables)
        {
            ostringstream ss;
            ss << "Table " << table() << " does not exist, and 'auto_create_tables' "
               << "is false.";

            throw HardError(ss.str(), error::COMMAND_FAILED);
        }

        m_mode = Mode::CREATING_TABLE;

        mxb_assert(m_dcid == 0);
        m_dcid = Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    auto sql = nosql::table_create_statement(table(), m_database.config().id_length);

                    send_downstream(sql);
                }

                return false;
            });

        return State::BUSY;
    }

    State create_database()
    {
        mxb_assert(m_mode != Mode::CREATING_DATABASE);

        if (!m_database.config().auto_create_databases)
        {
            ostringstream ss;
            ss << "Database " << m_database.name() << " does not exist, and "
               << "'auto_create_databases' is false.";

            throw HardError(ss.str(), error::COMMAND_FAILED);
        }

        m_mode = Mode::CREATING_DATABASE;

        mxb_assert(m_dcid == 0);
        m_dcid = Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    ostringstream ss;
                    ss << "CREATE DATABASE `" << m_database.name() << "`";

                    send_downstream(ss.str());
                }

                return false;
            });

        return State::BUSY;
    }

    Mode                            m_mode = { Mode::DEFAULT };
    uint32_t                        m_dcid { 0 };
    string                          m_key;
    bool                            m_ordered { true };
    Query                           m_query;
    vector<bsoncxx::document::view> m_documents;
    vector<string>::const_iterator  m_it;
    int32_t                         m_n { 0 };
    int32_t                         m_ok { 0 };
    bsoncxx::builder::basic::array  m_write_errors;
};

// https://docs.mongodb.com/v4.4/reference/command/delete/
class Delete final : public OrderedCommand
{
public:
    static constexpr const char* const KEY = "delete";
    static constexpr const char* const HELP = "";

    Delete(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), key::DELETES)
    {
    }

    Delete(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req,
           const bsoncxx::document::view& doc,
           const DocumentArguments& arguments)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), doc, arguments, key::DELETES)
    {
    }

private:
    bool is_acceptable_error(const ComERR& err) const override
    {
        // Deleting documents from a non-existent table should appear to succeed.
        return err.code() == ER_NO_SUCH_TABLE;
    }

    string convert_document(const bsoncxx::document::view& doc) override
    {
        ostringstream sql;

        sql << "DELETE FROM " << table() << " ";

        auto q = doc["q"];

        if (!q)
        {
            throw SoftError("BSON field 'delete.deletes.q' is missing but a required field",
                            error::LOCATION40414);
        }

        if (q.type() != bsoncxx::type::k_document)
        {
            ostringstream ss;
            ss << "BSON field 'delete.deletes.q' is the wrong type '"
               << bsoncxx::to_string(q.type()) << "' expected type 'object'";
            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        sql << query_to_where_clause(q.get_document());

        auto limit = doc["limit"];

        if (!limit)
        {
            throw SoftError("BSON field 'delete.deletes.limit' is missing but a required field",
                            error::LOCATION40414);
        }

        if (limit)
        {
            double nLimit = 0;

            if (get_number_as_double(limit, &nLimit))
            {
                if (nLimit != 0 && nLimit != 1)
                {
                    ostringstream ss;
                    ss << "The limit field in delete objects must be 0 or 1. Got " << nLimit;

                    throw SoftError(ss.str(), error::FAILED_TO_PARSE);
                }
            }

            // Yes, if the type of the value is something else, there is no limit.

            if (nLimit == 1)
            {
                sql << " LIMIT 1";
            }
        }

        return sql.str();
    }

    Execution interpret(const ComOK& response, int) override
    {
        m_n += response.affected_rows();
        return Execution::CONTINUE;
    }

    void amend_response(DocumentBuilder&) override final
    {
        m_database.context().reset_error(m_n);
    }

};


// https://docs.mongodb.com/v4.4/reference/command/find/
class Find final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "find";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    void prepare() override
    {
        optional(key::BATCH_SIZE, &m_batch_size, Conversion::RELAXED);

        if (m_batch_size < 0)
        {
            ostringstream ss;
            ss << "BatchSize value must be non-negative, but received: " << m_batch_size;
            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        optional(key::SINGLE_BATCH, &m_single_batch);
    }

    string generate_sql() override
    {
        ostringstream sql;
        sql << "SELECT ";

        bsoncxx::document::view projection;
        if (optional(key::PROJECTION, &projection))
        {
            m_extractions = projection_to_extractions(projection);

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
        }
        else
        {
            sql << "doc";
        }

        sql << " FROM " << table() << " ";

        bsoncxx::document::view filter;
        if (optional(key::FILTER, &filter))
        {
            sql << query_to_where_clause(filter);
        }

        bsoncxx::document::view sort;
        if (optional(key::SORT, &sort))
        {
            string order_by = sort_to_order_by(sort);

            if (!order_by.empty())
            {
                sql << "ORDER BY " << order_by << " ";
            }
        }

        sql << convert_skip_and_limit();

        return sql.str();
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
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
                    DocumentBuilder doc;
                    NoSQLCursor::create_first_batch(doc, table(Quoted::NO));

                    pResponse = create_response(doc.extract());
                }
                else
                {
                    pResponse = MariaDBError(err).create_response(*this);
                }
            }
            break;

        case ComResponse::OK_PACKET:
        case ComResponse::LOCAL_INFILE_PACKET:
            mxb_assert(!true);
            throw_unexpected_packet();

        default:
            {
                // Must be a result set.
                unique_ptr<NoSQLCursor> sCursor = NoSQLCursor::create(table(Quoted::NO),
                                                                      m_extractions,
                                                                      std::move(mariadb_response));

                DocumentBuilder doc;
                sCursor->create_first_batch(doc, m_batch_size, m_single_batch);

                pResponse = create_response(doc.extract());

                if (!sCursor->exhausted())
                {
                    NoSQLCursor::put(std::move(sCursor));
                }
            }
        }

        *ppResponse = pResponse;
        return State::READY;
    }

private:
    int32_t        m_batch_size { DEFAULT_CURSOR_RETURN };
    bool           m_single_batch { false };
    vector<string> m_extractions;
};


// https://docs.mongodb.com/v4.4/reference/command/findAndModify/

// https://docs.mongodb.com/v4.4/reference/command/getLastError/
class GetLastError final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "getLastError";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        m_database.context().get_last_error(doc);
    }
};

// https://docs.mongodb.com/v4.4/reference/command/getMore/
class GetMore final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "getMore";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        int64_t id = value_as<int64_t>();
        string collection = m_database.name() + "." + required<string>(key::COLLECTION);
        int32_t batch_size = std::numeric_limits<int32_t>::max();

        optional(key::BATCH_SIZE, &batch_size, Conversion::RELAXED);

        if (batch_size < 0)
        {
            ostringstream ss;
            ss << "BatchSize value must be non-negative, bit received: " << batch_size;
            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        unique_ptr<NoSQLCursor> sCursor = NoSQLCursor::get(collection, id);

        sCursor->create_next_batch(doc, batch_size);

        if (!sCursor->exhausted())
        {
            NoSQLCursor::put(std::move(sCursor));
        }
    }
};

// https://docs.mongodb.com/v4.4/reference/command/insert/
class Insert final : public OrderedCommand
{
public:
    static constexpr const char* const KEY = "insert";
    static constexpr const char* const HELP = "";

    Insert(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), key::DOCUMENTS)
    {
    }

    Insert(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req,
           const bsoncxx::document::view& doc,
           const DocumentArguments& arguments)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), doc, arguments, key::DOCUMENTS)
    {
    }

    bool should_create_table() const override
    {
        return true;
    }

    void interpret_error(bsoncxx::builder::basic::document& error, const ComERR& err, int index) override
    {
        if (err.code() == ER_DUP_ENTRY)
        {
            string duplicate;

            auto oib = m_database.config().ordered_insert_behavior;

            if (oib == GlobalConfig::OrderedInsertBehavior::ATOMIC && m_ordered == true)
            {
                // Ok, so the documents were not inserted one by one, but everything
                // in one go. As 'index' refers to the n:th statement being executed,
                // it will be 0 as there is just one.
                mxb_assert(index == 0);

                // The duplicate can be found in the error message.
                string message = err.message();

                static const char PATTERN[] = "Duplicate entry '";
                static const int PATTERN_LENGTH = sizeof(PATTERN) - 1;

                auto i = message.find(PATTERN);
                mxb_assert(i != string::npos);

                if (i != string::npos)
                {
                    string s = message.substr(i + PATTERN_LENGTH);

                    auto j = s.find("'");
                    mxb_assert(j != string::npos);

                    duplicate = s.substr(0, j);

                    // Let's try finding the correct index. We need to loop through the
                    // whole thing in case the duplicate is in the same insert statement.
                    index = 0;
                    vector<int> indexes;
                    for (const auto& element : m_ids)
                    {
                        if (nosql::to_string(element) == duplicate)
                        {
                            indexes.push_back(index);

                            if (indexes.size() > 1)
                            {
                                // We've seen enough. We can break out.
                                break;
                            }
                        }

                        ++index;
                    }

                    if (indexes.size() == 1)
                    {
                        // If there is just one entry, then the id existed already in the database.
                        index = indexes[0];
                    }
                    else if (indexes.size() > 1)
                    {
                        // If there is more than one, then there were duplicates in the server entries.
                        index = indexes[1];
                    }
                }
            }

            error.append(kvp(key::CODE, error::DUPLICATE_KEY));

            // If we did not find the entry, we don't add any details.
            if (index < (int)m_ids.size())
            {
                error.append(kvp(key::INDEX, index));
                DocumentBuilder keyPattern;
                keyPattern.append(kvp(key::_ID, 1));
                error.append(kvp(key::KEY_PATTERN, keyPattern.extract()));
                DocumentBuilder keyValue_builder;
                mxb_assert(index < (int)m_ids.size());
                append(keyValue_builder, key::_ID, m_ids[index]);
                auto keyValue = keyValue_builder.extract();
                error.append(kvp(key::KEY_VALUE, keyValue));

                duplicate = bsoncxx::to_json(keyValue);
            }

            ostringstream ss;
            ss << "E" << error::DUPLICATE_KEY << " duplicate key error collection: "
               << m_database.name() << "." << value_as<string>()
               << " index: _id_ dup key: " << duplicate;

            error.append(kvp(key::ERRMSG, ss.str()));
        }
        else
        {
            OrderedCommand::interpret_error(error, err, index);
        }
    }

protected:
    Query generate_sql(const vector<bsoncxx::document::view>& documents) override
    {
        Query query;

        auto oib = m_database.config().ordered_insert_behavior;

        if (oib == GlobalConfig::OrderedInsertBehavior::DEFAULT || m_ordered == false)
        {
            if (m_ordered)
            {
                ostringstream ss;
                size_t nStatements = 0;

                // ER_BAD_DB_ERROR  1049
                // ER_NO_SUCH_TABLE 1146

                // NOTE: Making any change that affects the statement size, will cause 3-big-misc.js to fail.
                ss << "BEGIN NOT ATOMIC "
                   <<   "DECLARE EXIT HANDLER FOR 1146, 1049 RESIGNAL;"
                   <<   "DECLARE EXIT HANDLER FOR SQLEXCEPTION COMMIT;"
                   <<   "START TRANSACTION;";

                for (const auto& doc : documents)
                {
                    ss << "INSERT INTO " << table() << " (doc) VALUES "
                       << convert_document_data(doc) << ";";
                    ++nStatements;
                }

                ss <<   "COMMIT;"
                   << "END";

                query = Query(Query::COMPOUND, nStatements, std::move(ss.str()));
            }
            else
            {
                size_t nStatements = 0;
                ostringstream ss;

                ss << "BEGIN;";
                ++nStatements;

                for (const auto& doc : documents)
                {
                    ss << "INSERT IGNORE INTO " << table() << " (doc) VALUES "
                       << convert_document_data(doc) << ";";
                    ++nStatements;
                }

                ss << "COMMIT;";
                ++nStatements;

                query = Query(Query::MULTI, nStatements, std::move(ss.str()));
            }
        }
        else
        {
            ostringstream sql;
            sql << "INSERT INTO " << table() << " (doc) VALUES ";

            bool first = true;
            for (const auto& doc : documents)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    sql << ", ";
                }

                sql << convert_document_data(doc);
            }

            query = Query(std::move(sql.str()));
        }

        return query;
    }

    string convert_document(const bsoncxx::document::view& doc) override
    {
        ostringstream sql;
        sql << "INSERT INTO " << table() << " (doc) VALUES " << convert_document_data(doc);

        return sql.str();
    }

    string convert_document_data(const bsoncxx::document::view& doc)
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

            element = doc_with_id.view()["_id"];
            json = bsoncxx::to_json(doc_with_id);
        }

        m_ids.push_back(element);

        json = escape_essential_chars(std::move(json));

        sql << "('" << json << "')";

        return sql.str();
    }

    Execution interpret(const ComOK& response, int) override
    {
        m_n += response.affected_rows();
        return Execution::CONTINUE;
    }

    uint8_t* interpret_multi(uint8_t* pBuffer, uint8_t* pEnd, size_t nStatements) override
    {
        mxb_assert(nStatements > 2);

        ComResponse begin(pBuffer);

        if (begin.is_ok())
        {
            pBuffer += ComPacket::packet_len(pBuffer);

            size_t nInserts = nStatements - 2; // The starting BEGIN and the ending COMMIT

            for (size_t i = 0; i < nInserts; ++i)
            {
                ComResponse response(pBuffer);

                switch (response.type())
                {
                case ComResponse::OK_PACKET:
                    {
                        ComOK ok(response);

                        auto n = ok.affected_rows();

                        if (n == 0)
                        {
                            ostringstream ss;
                            ss << "E" << (int)error::COMMAND_FAILED << " error collection "
                               << table(Quoted::NO)
                               << ", possibly duplicate id.";

                            DocumentBuilder error;
                            error.append(kvp(key::INDEX, (int)i));
                            error.append(kvp(key::CODE, error::COMMAND_FAILED));
                            error.append(kvp(key::ERRMSG, ss.str()));

                            m_write_errors.append(error.extract());
                        }
                        else
                        {
                            m_n += n;
                        }
                    }
                    break;

                case ComResponse::ERR_PACKET:
                    // An error packet in the middle of everything is a complete failure.
                    throw MariaDBError(ComERR(response));

                default:
                    mxb_assert(!true);
                    throw_unexpected_packet();
                }

                pBuffer += ComPacket::packet_len(pBuffer);

                if (pBuffer >= pEnd)
                {
                    mxb_assert(!true);
                    throw HardError("Too few packets in received data.", error::INTERNAL_ERROR);
                }
            }

            ComResponse commit(pBuffer);

            if (!commit.is_ok())
            {
                mxb_assert(commit.is_err());
                throw MariaDBError(ComERR(commit));
            }

            pBuffer += ComPacket::packet_len(pBuffer);
            mxb_assert(pBuffer == pEnd);
        }
        else
        {
            mxb_assert(begin.is_err());
            throw MariaDBError(ComERR(begin));
        }

        return pBuffer;
    }

    uint8_t* interpret_compound(uint8_t* pBuffer, uint8_t* pEnd, size_t nStatements) override
    {
        ComResponse response(pBuffer);

        if (response.is_ok())
        {
            ComOK ok(response);

            m_n = ok.affected_rows();

            if (m_n != (int64_t)nStatements)
            {
                ostringstream ss;
                ss << "E" << (int)error::COMMAND_FAILED << " error collection "
                   << table(Quoted::NO)
                   << ", possibly duplicate id.";

                DocumentBuilder error;
                error.append(kvp(key::INDEX, (int)m_n));
                error.append(kvp(key::CODE, error::COMMAND_FAILED));
                error.append(kvp(key::ERRMSG, ss.str()));

                m_write_errors.append(error.extract());
            }
        }
        else
        {
            // We always expect an OK.
            throw MariaDBError(ComERR(response));
        }

        pBuffer += ComPacket::packet_len(pBuffer);

        return pBuffer;
    }

    mutable int64_t                     m_nDocuments { 0 };
    vector<bsoncxx::document::element>  m_ids;
    vector<bsoncxx::document::value>    m_stashed_documents;
};


// https://docs.mongodb.com/v4.4/reference/command/resetError/
class ResetError final : public ImmediateCommand
{
public:
    static constexpr const char* const KEY = "resetError";
    static constexpr const char* const HELP = "";

    using ImmediateCommand::ImmediateCommand;

    void populate_response(DocumentBuilder& doc) override
    {
        // No action needed, the error is reset on each command but for getLastError.
        doc.append(kvp(key::OK, 1));
    }
};

// https://docs.mongodb.com/v4.4/reference/command/update/
class Update final : public OrderedCommand
{
public:
    static constexpr const char* const KEY = "update";
    static constexpr const char* const HELP = "";

    Update(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), key::UPDATES)
    {
    }

    Update(const std::string& name,
           Database* pDatabase,
           GWBUF* pRequest,
           nosql::Msg&& req,
           const bsoncxx::document::view& doc,
           const DocumentArguments& arguments)
        : OrderedCommand(name, pDatabase, pRequest, std::move(req), doc, arguments, key::UPDATES)
    {
    }

    bool should_create_table() const override
    {
        return m_upsert;
    }

private:
    enum class UpdateAction
    {
        UPDATING,
        INSERTING
    };

    bool is_acceptable_error(const ComERR& err) const override
    {
        // Updating documents in non-existent table should appear to succeed.
        return err.code() == ER_NO_SUCH_TABLE;
    }

    string convert_document(const bsoncxx::document::view& update) override
    {
        ostringstream sql;
        sql << "UPDATE " << table() << " SET DOC = ";

        optional(update, key::UPSERT, &m_upsert);

        auto q = update[key::Q];

        if (!q)
        {
            throw SoftError("BSON field 'update.updates.q' is missing but a required field",
                            error::LOCATION40414);
        }

        if (q.type() != bsoncxx::type::k_document)
        {
            ostringstream ss;
            ss << "BSON field 'update.updates.q' is the wrong type '" << bsoncxx::to_string(q.type())
               << "', expected type 'object'";
            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }

        auto u = update[key::U];

        if (!u)
        {
            throw SoftError("BSON field 'update.updates.u' is missing but a required field",
                            error::LOCATION40414);
        }

        sql << update_specification_to_set_value(update, u);
        sql << " ";
        sql << query_to_where_clause(q.get_document());

        auto multi = update[key::MULTI];

        if (!multi || !multi.get_bool())
        {
            sql << " LIMIT 1";
        }

        return sql.str();
    }

    Execution interpret(const ComOK& response, int index) override
    {
        Execution rv;

        switch (m_update_action)
        {
        case UpdateAction::UPDATING:
            rv = interpret_update(response, index);
            break;

        case UpdateAction::INSERTING:
            rv = interpret_insert(response, index);
            break;
        }

        return rv;
    }

    Execution interpret_update(const ComOK& response, int index)
    {
        Execution rv = Execution::CONTINUE;

        auto n = response.matched_rows();

        if (n == 0)
        {
            if (m_upsert)
            {
                if (m_insert.empty())
                {
                    // Ok, so the update did not match anything and we havn't attempted
                    // an insert.
                    rv = insert_document(index);
                }
                else
                {
                    // We attempted updating the document we just insterted, but it was
                    // not found.
                    bsoncxx::builder::basic::document error;
                    error.append(kvp(key::INDEX, index));
                    error.append(kvp(key::CODE, error::INTERNAL_ERROR));
                    error.append(kvp(key::ERRMSG, "Inserted document not found when attempting to update."));

                    rv = Execution::ABORT;
                }
            }
        }
        else
        {
            if (m_insert.empty())
            {
                // A regular update.
                m_nModified += response.affected_rows();
                m_n += n;
            }
            else
            {
                DocumentBuilder upsert;
                upsert.append(kvp(key::INDEX, index));
                upsert.append(kvp(key::_ID, m_id));

                m_upserted.append(upsert.extract());
            }

            m_insert.clear();
        }

        return rv;
    }

    Execution interpret_insert(const ComOK& response, int index)
    {
        auto update = m_documents[index];
        auto u = update[key::U];

        ostringstream ss;
        ss << "UPDATE " << table() << " SET DOC = "
           << update_specification_to_set_value(update, u)
           << "WHERE id = '{\"$oid\":\"" << m_id.to_string() << "\"}'";

        string sql = ss.str();

        check_maximum_sql_length(sql);

        mxb_assert(m_dcid == 0);
        m_dcid = Worker::get_current()->delayed_call(0, [this, sql](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    send_downstream(sql);
                }

            return false;
        });

        return Execution::CONTINUE;
    }

    Execution insert_document(int index)
    {
        mxb_assert(m_update_action == UpdateAction::UPDATING && m_insert.empty());

        m_update_action = UpdateAction::INSERTING;


        // TODO: If we were here to apply the update operations, then an
        // TODO: INSERT alone would suffice instead of the INSERT + UPDATE
        // TODO: that currently is done.

        ostringstream ss;
        ss << "INSERT INTO " << table() << " (doc) VALUES ('";

        auto update = m_documents[index];
        bsoncxx::document::view q = update[key::Q].get_document();

        m_id = bsoncxx::oid();

        DocumentBuilder builder;
        builder.append(kvp(key::_ID, m_id));

        for (const auto& e : q)
        {
            append(builder, e.key(), e);
        }

        ss << bsoncxx::to_json(builder.extract());

        ss << "')";

        m_insert = ss.str();

        check_maximum_sql_length(m_insert);

        mxb_assert(m_dcid == 0);
        m_dcid = Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                m_dcid = 0;

                if (action == Worker::Call::EXECUTE)
                {
                    send_downstream(m_insert);
                }

            return false;
        });

        return Execution::BUSY;
    }

    void amend_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::N_MODIFIED, m_nModified));

        if (!m_upserted.view().empty())
        {
            doc.append(kvp(key::UPSERTED, m_upserted.extract()));
        }

        m_database.context().reset_error(m_n);
    }


private:
    UpdateAction m_update_action { UpdateAction::UPDATING };
    bool         m_upsert;
    int32_t      m_nModified { 0 };
    string       m_insert;
    bsoncxx::oid m_id;
    ArrayBuilder m_upserted;
};


}

}
