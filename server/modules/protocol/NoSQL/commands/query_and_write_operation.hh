/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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

public:
    GWBUF* execute() override final
    {
        auto query = generate_sql();

        for (const auto& statement : query.statements())
        {
            check_maximum_sql_length(statement);
        }

        m_query = std::move(query);

        m_it = m_query.statements().begin();

        execute_one_statement();

        return nullptr;
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
        GWBUF* pResponse = nullptr;

        bool abort = false;

        uint8_t* pBuffer = mariadb_response.data();
        uint8_t* pEnd = pBuffer + mariadb_response.length();

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
            if (!interpret_single(pBuffer))
            {
                abort = true;
            }

            pBuffer += ComPacket::packet_len(pBuffer);
        }

        if (pBuffer != pEnd)
        {
            MXS_WARNING("Received %ld excess bytes, ignoring.", pEnd - pBuffer);
        }

        ++m_it;

        State rv = BUSY;

        if (m_it == m_query.statements().end() || abort)
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
            rv = READY;
        }
        else
        {
            execute_one_statement();
        }

        *ppResponse = pResponse;
        return rv;
    }

protected:
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
            const auto& documents = it->second;
            check_write_batch_size(documents.size());

            query = generate_sql(documents);
        }
        else
        {
            auto documents = required<bsoncxx::array::view>(m_key.c_str());
            auto nDocuments = std::distance(documents.begin(), documents.end());
            check_write_batch_size(nDocuments);

            vector<bsoncxx::document::view> documents2;

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

                documents2.push_back(element.get_document());
            }

            query = generate_sql(documents2);
        }

        return query;
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

    virtual void interpret(const ComOK& response) = 0;

    virtual bool interpret_single(uint8_t* pBuffer)
    {
        bool rv = true;

        ComResponse response(pBuffer);

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            m_ok = 1;
            interpret(ComOK(response));
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
                        rv = false;
                    }

                    add_error(m_write_errors, err, m_it - m_query.statements().begin());
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

    string                         m_key;
    bool                           m_ordered { true };
    Query                          m_query;
    vector<string>::const_iterator m_it;
    int32_t                        m_n { 0 };
    int32_t                        m_ok { 0 };
    bsoncxx::builder::basic::array m_write_errors;
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

    void interpret(const ComOK& response) override
    {
        m_n += response.affected_rows();
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
        return READY;
    }

private:
    int32_t        m_batch_size { 101 }; // Documented to be that.
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

    using Worker = mxb::Worker;

    ~Insert()
    {
        if (m_dcid)
        {
            Worker::get_current()->cancel_delayed_call(m_dcid);
        }
    }

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override final
    {
        State state = BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (m_action)
        {
        case Action::INSERTING_DATA:
            state = translate_inserting_data(std::move(mariadb_response), &pResponse);
            break;

        case Action::CREATING_TABLE:
            state = translate_creating_table(std::move(mariadb_response), &pResponse);
            break;

        case Action::CREATING_DATABASE:
            state = translate_creating_database(std::move(mariadb_response), &pResponse);
            break;
        }

        mxb_assert((state == BUSY && pResponse == nullptr) || (state == READY && pResponse != nullptr));
        *ppResponse = pResponse;
        return state;
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
    State translate_inserting_data(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::INSERTING_DATA);

        State state = BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        if (!response.is_err() || ComERR(response).code() != ER_NO_SUCH_TABLE)
        {
            state = OrderedCommand::translate(std::move(mariadb_response), &pResponse);
        }
        else
        {
            if (m_database.config().auto_create_tables)
            {
                create_table();
            }
            else
            {
                ostringstream ss;
                ss << "Table " << table() << " does not exist, and 'auto_create_tables' "
                   << "is false.";

                throw HardError(ss.str(), error::COMMAND_FAILED);
            }
        }

        *ppResponse = pResponse;
        return state;
    }

    State translate_creating_table(mxs::Buffer&& mariadb_response, GWBUF** ppResponse)
    {
        mxb_assert(m_action == Action::CREATING_TABLE);

        State state = BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(mariadb_response.data());

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            MXS_INFO("Table created, now executing statment.");
            m_action = Action::INSERTING_DATA;
            execute_one_statement();
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_TABLE_EXISTS_ERROR)
                {
                    MXS_INFO("Table created by someone else, now executing statment.");
                    m_action = Action::INSERTING_DATA;
                    execute_one_statement();
                }
                else if (code == ER_BAD_DB_ERROR && err.message().find("Unknown database") == 0)
                {
                    if (m_database.config().auto_create_databases)
                    {
                        create_database();
                    }
                    else
                    {
                        ostringstream ss;
                        ss << "Database " << m_database.name() << " does not exist, and "
                           << "'auto_create_databases' is false.";

                        throw HardError(ss.str(), error::COMMAND_FAILED);
                    }
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
        mxb_assert(m_action == Action::CREATING_DATABASE);

        State state = BUSY;
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

    void create_table()
    {
        m_action = Action::CREATING_TABLE;

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
    }

    void create_database()
    {
        m_action = Action::CREATING_DATABASE;

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
    }

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

    void interpret(const ComOK& response) override
    {
        m_n += response.affected_rows();
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

    enum class Action
    {
        INSERTING_DATA,
        CREATING_TABLE,
        CREATING_DATABASE
    };

    Action                              m_action { Action::INSERTING_DATA };
    uint32_t                            m_dcid { 0 };
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

private:
    bool is_acceptable_error(const ComERR& err) const override
    {
        // Updating documents in non-existent table should appear to succeed.
        return err.code() == ER_NO_SUCH_TABLE;
    }

    string convert_document(const bsoncxx::document::view& update) override
    {
        ostringstream sql;
        sql << "UPDATE " << table() << " SET DOC = ";

        auto upsert = false;
        optional(update, key::UPSERT, &upsert);

        if (upsert)
        {
            throw SoftError("'upsert' is not supported.", error::COMMAND_FAILED);
        }

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

        switch (get_update_kind(u))
        {
        case AGGREGATION_PIPELINE:
            {
                string message("Aggregation pipeline not supported: '");
                message += bsoncxx::to_json(update);
                message += "'.";

                MXB_ERROR("%s", message.c_str());
                throw HardError(message, error::COMMAND_FAILED);
            }
            break;

        case REPLACEMENT_DOCUMENT:
            {
                auto json = bsoncxx::to_json(static_cast<bsoncxx::document::view>(u.get_document()));
                json = escape_essential_chars(std::move(json));

                sql << "JSON_SET('" << json << "', '$._id', JSON_EXTRACT(id, '$'))";
            }
            break;

        case UPDATE_OPERATORS:
            {
                auto doc = static_cast<bsoncxx::document::view>(u.get_document());
                sql << translate_update_operations(doc);
            }
            break;

        case INVALID:
            {
                string message("Invalid combination of updates: '");
                message += bsoncxx::to_json(update);
                message += "'.";

                throw HardError(message, error::COMMAND_FAILED);
            }
        }

        sql << " ";

        sql << query_to_where_clause(q.get_document());

        auto multi = update[key::MULTI];

        if (!multi || !multi.get_bool())
        {
            sql << " LIMIT 1";
        }

        return sql.str();
    }

    enum Kind
    {
        AGGREGATION_PIPELINE,
        REPLACEMENT_DOCUMENT,
        UPDATE_OPERATORS,
        INVALID
    };

    Kind get_update_kind(const bsoncxx::document::element& element)
    {
        Kind kind = INVALID;

        switch (element.type())
        {
        case bsoncxx::type::k_array:
            kind = AGGREGATION_PIPELINE;
            break;

        case bsoncxx::type::k_document:
            {
                auto doc = static_cast<bsoncxx::document::view>(element.get_document());

                if (doc.empty())
                {
                    kind = REPLACEMENT_DOCUMENT;
                }
                else
                {
                    for (auto field : doc)
                    {
                        const char* pData = field.key().data(); // Not necessarily null-terminated.

                        if (*pData == '$')
                        {
                            string name(pData, field.key().length());

                            if (name != "$set" && name != "$unset")
                            {
                                ostringstream ss;
                                ss << "Currently the only supported update operators are $set and $unset.";
                                throw SoftError(ss.str(), error::COMMAND_FAILED);
                            }
                            else
                            {
                                if (kind == INVALID)
                                {
                                    kind = UPDATE_OPERATORS;
                                }
                                else if (kind != UPDATE_OPERATORS)
                                {
                                    MXS_ERROR("'%s' contains both fields and update operators.",
                                              bsoncxx::to_json(doc).c_str());
                                    kind = INVALID;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            if (kind == INVALID)
                            {
                                kind = REPLACEMENT_DOCUMENT;
                            }
                            else if (kind != REPLACEMENT_DOCUMENT)
                            {
                                MXS_ERROR("'%s' contains both fields and update operators.",
                                          bsoncxx::to_json(doc).c_str());
                                kind = INVALID;
                                break;
                            }
                        }
                    }
                }
            }
            break;

        default:
            throw SoftError("Update argument must be either an object or an array", error::FAILED_TO_PARSE);
        }

        return kind;
    }

    string translate_update_operations(const bsoncxx::document::view& doc)
    {
        string rv;

        for (auto element : doc)
        {
            if (!rv.empty())
            {
                rv += ", ";
            }

            bool add_value = true;

            if (element.key().compare("$set") == 0)
            {
                rv += "JSON_SET(doc, ";
            }
            else if (element.key().compare("$unset") == 0)
            {
                rv += "JSON_REMOVE(doc, ";
                add_value = false;
            }
            else
            {
                // In get_update_kind() it is established that it is either $set or $unset.
                // This is to catch a changed there without a change here.
                mxb_assert(!true);
            }

            auto fields = static_cast<bsoncxx::document::view>(element.get_document());

            string s;
            for (auto field : fields)
            {
                if (!s.empty())
                {
                    s += ", ";
                }

                string key = field.key().data();
                key = escape_essential_chars(std::move(key));

                s += "'$.";
                s += key;
                s += "'";

                if (add_value)
                {
                    s += ", ";
                    s += nosql::to_value(field);
                }
            }

            rv += s;

            rv += ")";
        }

        rv += " ";

        return rv;
    }

    void interpret(const ComOK& response) override
    {
        m_nModified += response.affected_rows();

        string s = response.info().to_string();

        if (s.find("Rows matched: ") == 0)
        {
            m_n += atol(s.c_str() + 14);
        }
    }

    void amend_response(DocumentBuilder& doc) override
    {
        doc.append(kvp(key::N_MODIFIED, m_nModified));

        m_database.context().reset_error(m_n);
    }


private:
    int32_t m_nModified { 0 };
};


}

}
