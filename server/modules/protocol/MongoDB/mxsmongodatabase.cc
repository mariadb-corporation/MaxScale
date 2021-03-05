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

#include "mxsmongodatabase.hh"
#include <mysqld_error.h>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../filter/masking/mysql.hh"
#include "config.hh"

using namespace std;
using mxb::Worker;

class mxsmongo::Database::Command
{
public:
    Command(mxsmongo::Database* pDatabase,
            GWBUF* pRequest,
            const mxsmongo::Packet& req,
            const bsoncxx::document::view& doc)
        : m_database(*pDatabase)
        , m_pRequest(gwbuf_clone(pRequest))
        , m_req(req)
        , m_doc(doc)
    {
    }

    enum State
    {
        BUSY,
        READY
    };

    virtual ~Command()
    {
        free_request();
    }

    virtual GWBUF* execute() = 0;

    virtual State translate(GWBUF& mariadb_response, GWBUF** ppMongo_response)
    {
        mxb_assert(!true);
        *ppMongo_response = nullptr;
        return READY;
    }

    GWBUF* create_empty_response()
    {
        auto builder = bsoncxx::builder::stream::document{};
        bsoncxx::document::value doc_value = builder << bsoncxx::builder::stream::finalize;

        return create_response(doc_value);
    }

    GWBUF* create_error_response(const std::string& message, mxsmongo::error::Code code)
    {
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("$err", message.c_str()));
        builder.append(bsoncxx::builder::basic::kvp("code", static_cast<int32_t>(code)));

        return create_response(builder.extract());
    }

protected:
    string get_table(const char* zCommand) const
    {
        auto utf8 = m_doc[zCommand].get_utf8();
        string table(utf8.value.data(), utf8.value.size());

        return "`" + m_database.name() + "`.`" + table + "`";
    }

    void free_request()
    {
        if (m_pRequest)
        {
            gwbuf_free(m_pRequest);
            m_pRequest = nullptr;
        }
    }

    void send_downstream(const string& sql)
    {
        MXS_NOTICE("SQL: %s", sql.c_str());

        GWBUF* pRequest = modutil_create_query(sql.c_str());

        m_database.context().downstream().routeQuery(pRequest);
    }

    pair<GWBUF*, uint8_t*> create_response(size_t size_of_documents, size_t nDocuments)
    {
        // TODO: In the following is assumed that whatever is returned will
        // TODO: fit into a Mongo packet.

        int32_t response_flags = MONGOC_QUERY_AWAIT_DATA; // Dunno if this should be on.
        int64_t cursor_id = 0;
        int32_t starting_from = 0;
        int32_t number_returned = nDocuments;

        size_t response_size = MXSMONGO_HEADER_LEN
            + sizeof(response_flags) + sizeof(cursor_id) + sizeof(starting_from) + sizeof(number_returned)
            + size_of_documents;

        GWBUF* pResponse = gwbuf_alloc(response_size);

        auto* pRes_hdr = reinterpret_cast<mongoc_rpc_header_t*>(GWBUF_DATA(pResponse));
        pRes_hdr->msg_len = response_size;
        pRes_hdr->request_id = m_database.context().next_request_id();
        pRes_hdr->response_to = m_req.request_id();
        pRes_hdr->opcode = MONGOC_OPCODE_REPLY;

        uint8_t* pData = GWBUF_DATA(pResponse) + MXSMONGO_HEADER_LEN;

        pData += mxsmongo::set_byte4(pData, response_flags);
        pData += mxsmongo::set_byte8(pData, cursor_id);
        pData += mxsmongo::set_byte4(pData, starting_from);
        pData += mxsmongo::set_byte4(pData, number_returned);

        return make_pair(pResponse, pData);
    }

    GWBUF* create_response(size_t size_of_documents, const vector<bsoncxx::document::value>& documents)
    {
        GWBUF* pResponse;
        uint8_t* pData;

        tie(pResponse, pData) = create_response(size_of_documents, documents.size());

        for (const auto& doc : documents)
        {
            auto view = doc.view();
            size_t size = view.length();

            memcpy(pData, view.data(), view.length());
            pData += view.length();
        }

        return pResponse;
    }

    GWBUF* create_response(const bsoncxx::document::value& doc)
    {
        MXS_NOTICE("RESPONSE: %s", bsoncxx::to_json(doc).c_str());

        auto doc_view = doc.view();
        size_t doc_len = doc_view.length();

        GWBUF* pResponse;
        uint8_t* pData;

        tie(pResponse, pData) = create_response(doc_len, 1);

        memcpy(pData, doc_view.data(), doc_view.length());

        return pResponse;
    }

    GWBUF* translate_resultset(vector<string>& extractions, GWBUF& mariadb_response)
    {
        bsoncxx::builder::basic::document builder;

        uint8_t* pBuffer = GWBUF_DATA(&mariadb_response);

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

        vector<bsoncxx::document::value> documents;
        uint32_t size_of_documents = 0;

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

                size_of_documents += doc.view().length();

                documents.push_back(doc);
            }
            catch (const std::exception& x)
            {
                MXS_ERROR("Could not convert object to JSON: %s", x.what());
                MXS_NOTICE("String: '%s'", json.c_str());
            }
        }

        return create_response(size_of_documents, documents);
    }

    string create_leaf_entry(const string& extraction, const std::string& value)
    {
        mxb_assert(extraction.find('.') == string::npos);

        return "\"" + extraction + "\": " + value;
    }

    string create_nested_entry(const string& extraction, const std::string& value)
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

    string create_entry(const string& extraction, const std::string& value)
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

    void add_error(bsoncxx::builder::basic::document& builder, const ComERR& err)
    {
        MXS_WARNING("Mongo request to backend failed: (%d), %s", err.code(), err.message().c_str());

        bsoncxx::builder::basic::document mariadb_builder;

        mariadb_builder.append(bsoncxx::builder::basic::kvp("code", err.code()));
        mariadb_builder.append(bsoncxx::builder::basic::kvp("state", err.state()));
        mariadb_builder.append(bsoncxx::builder::basic::kvp("message", err.message()));

        builder.append(bsoncxx::builder::basic::kvp("mariadb", mariadb_builder.extract()));

        // TODO: Map MariaDB errors to something sensible from
        // TODO: https://github.com/mongodb/mongo/blob/master/src/mongo/base/error_codes.yml

        bsoncxx::builder::basic::array array_builder;

        for (int64_t i = 0; i < 1; ++i) // TODO: With multiple updates/deletes object this must change.
        {
            bsoncxx::builder::basic::document error_builder;

            error_builder.append(bsoncxx::builder::basic::kvp("index", i));
            int32_t code = mxsmongo::error::from_mariadb_code(err.code());
            error_builder.append(bsoncxx::builder::basic::kvp("code", code));
            error_builder.append(bsoncxx::builder::basic::kvp("errmsg", err.message()));

            array_builder.append(error_builder.extract());
        }

        builder.append(bsoncxx::builder::basic::kvp("writeErrors", array_builder.extract()));
    }

    mxsmongo::Database&     m_database;
    GWBUF*                  m_pRequest;
    mxsmongo::Packet        m_req;
    bsoncxx::document::view m_doc;
};

namespace
{


namespace command
{

class TableCreatingCommand : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    ~TableCreatingCommand()
    {
        if (m_dcid)
        {
            Worker::get_current()->cancel_delayed_call(m_dcid);
        }
    }

    GWBUF* execute() override final
    {
        if (m_statement.empty())
        {
            m_statement = create_statement();
        }

        send_downstream(m_statement);

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override final
    {
        State state = BUSY;
        GWBUF* pResponse = nullptr;

        ComResponse response(GWBUF_DATA(&mariadb_response));

        if (m_mode == NORMAL)
        {
            if (!response.is_err() || ComERR(response).code() != ER_NO_SUCH_TABLE)
            {
                state = translate(response, &pResponse);
            }
            else
            {
                if (m_database.config().auto_create_tables)
                {
                    // The table did not exist, so it must be created.
                    mxb_assert(m_dcid == 0);

                    m_dcid = Worker::get_current()->delayed_call(0, [this](Worker::Call::action_t action) {
                            m_dcid = 0;

                            if (action == Worker::Call::EXECUTE)
                            {
                                m_mode = TABLE_CREATING;

                                stringstream ss;
                                ss << "CREATE TABLE " << table_name() << " (id TEXT, doc JSON)";

                                send_downstream(ss.str());
                            }

                            return false;
                        });
                }
                else
                {
                    stringstream ss;
                    ss << "Table " << table_name() << " does not exist, and 'auto_create_tables' "
                       << "is false.";

                    pResponse = create_error_response(ss.str(), mxsmongo::error::COMMAND_FAILED);
                    state = READY;
                }
            }
        }
        else
        {
            mxb_assert(m_mode == TABLE_CREATING);
            mxb_assert(!m_statement.empty());

            switch (response.type())
            {
            case ComResponse::OK_PACKET:
                MXS_NOTICE("TABLE created, now executing statment.");
                m_mode = NORMAL;
                send_downstream(m_statement);
                break;

            case ComResponse::ERR_PACKET:
                {
                    ComERR err(response);

                    auto code = err.code();

                    if (code == ER_TABLE_EXISTS_ERROR)
                    {
                        MXS_NOTICE("TABLE created by someone else, now executing statment.");
                        m_mode = NORMAL;
                        send_downstream(m_statement);
                    }
                    else
                    {
                        MXS_ERROR("Could not create table: (%d), %s", err.code(), err.message().c_str());
                        pResponse = create_error_response(err.message(),
                                                          mxsmongo::error::from_mariadb_code(code));
                        state = READY;
                    }
                }
                break;

            default:
                mxb_assert(!true);
                MXS_ERROR("Expected OK or ERR packet, received something else.");
                pResponse = create_error_response("Unexpected response received from backend.",
                                                  mxsmongo::error::COMMAND_FAILED);
                state = READY;
            }
        }

        mxb_assert((state == BUSY && pResponse == nullptr) || (state == READY && pResponse != nullptr));
        *ppResponse = pResponse;
        return state;
    }

protected:
    virtual string create_statement() const = 0;
    virtual string table_name() const = 0;
    virtual State translate(ComResponse& response, GWBUF** ppResponse) = 0;

private:
    enum Mode
    {
        NORMAL,
        TABLE_CREATING,
    };

    Mode     m_mode { NORMAL };
    string   m_statement;
    uint32_t m_dcid { 0 };
};

// https://docs.mongodb.com/manual/reference/command/delete/
class Delete : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;
        sql << "DELETE FROM " << get_table(mxsmongo::keys::DELETE);

        auto docs = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::keys::DELETES].get_array());

        size_t nDocs = 0;
        for (auto element : docs)
        {
            ++nDocs;

            if (nDocs > 1)
            {
                break;
            }

            auto doc = static_cast<bsoncxx::document::view>(element.get_document());

            auto q = static_cast<bsoncxx::document::view>(doc["q"].get_document());

            // TODO: Convert q.
            mxb_assert(q.empty());

            auto limit = doc["limit"];

            if (limit)
            {
                bool deleteOne = false;

                switch (limit.type())
                {
                case bsoncxx::type::k_int32:
                    deleteOne = (static_cast<int32_t>(limit.get_int32()) != 0);
                    break;

                case bsoncxx::type::k_int64:
                    deleteOne = (static_cast<int64_t>(limit.get_int64()) != 0);
                    break;

                default:
                    mxb_assert(!true);
                }

                if (deleteOne)
                {
                    sql << " LIMIT 1";
                }
            }
        }

        if (nDocs > 1)
        {
            // TODO: Since the limit is part of the query object, a Mongo DELETE command
            // TODO: having more delete documents than one, must be handled as individual
            // TODO: DELETE statements.
            mxb_assert(!true);
        }

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
        GWBUF* pResponse = nullptr;

        bsoncxx::builder::basic::document builder;

        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t ok = response.is_ok() ? 1 : 0;
        int64_t n = 0;

        builder.append(bsoncxx::builder::basic::kvp("ok", ok));

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            n = ComOK(response).affected_rows();
            break;

        case ComResponse::ERR_PACKET:
            add_error(builder, ComERR(response));
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
        default:
            mxb_assert(!true);
        }

        builder.append(bsoncxx::builder::basic::kvp("n", n));

        auto doc = builder.extract();

        pResponse = create_response(doc);

        *ppResponse = pResponse;
        return READY;
    }
};

// TODO: This will be generalized so that there will be e.g. a base-class ResultSet for
// TODO: commands that expects, well, a resultset. But for now there is no hierarchy.

// https://docs.mongodb.com/manual/reference/command/find
class Find : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;
        sql << "SELECT ";

        auto projection = m_doc[mxsmongo::keys::PROJECTION];

        if (projection)
        {
            m_extractions = mxsmongo::projection_to_extractions(projection.get_document());

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

        sql << " FROM " << get_table(mxsmongo::keys::FIND);

        auto filter = m_doc[mxsmongo::keys::FILTER];

        if (filter)
        {
            const auto& doc = filter.get_document();
            string where = mxsmongo::filter_to_where_clause(doc);

            MXS_NOTICE("Filter '%s' converted to where clause '%s'.",
                       bsoncxx::to_json(doc).c_str(),
                       where.c_str());

            if (!where.empty())
            {
                sql << " WHERE " << where;
            }
        }

        auto sort = m_doc[mxsmongo::keys::SORT];

        if (sort)
        {
            const auto& doc = sort.get_document();
            string order_by = mxsmongo::sort_to_order_by(doc);

            MXS_NOTICE("Sort '%s' converted to 'ORDER BY %s'.",
                       bsoncxx::to_json(doc).c_str(),
                       order_by.c_str());

            if (!order_by.empty())
            {
                sql << " ORDER BY " << order_by;
            }
        }

        auto skip = m_doc[mxsmongo::keys::SKIP];
        auto limit = m_doc[mxsmongo::keys::LIMIT];

        if (skip || limit)
        {
            string s = mxsmongo::skip_and_limit_to_limit(skip, limit);

            if (!s.empty())
            {
                sql << s;
            }
        }

        send_downstream(sql.str());

        return nullptr;
    }

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
        GWBUF* pResponse = nullptr;

        ComResponse response(GWBUF_DATA(&mariadb_response));

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            break;

        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_NO_SUCH_TABLE)
                {
                    vector<bsoncxx::document::value> documents;
                    uint32_t size_of_documents = 0;

                    pResponse = create_response(size_of_documents, documents);
                }
                else
                {
                    MXS_WARNING("Mongo request to backend failed: (%d), %s", code, err.message().c_str());

                    pResponse = create_error_response(err.message(), mxsmongo::error::from_mariadb_code(code));
                }
            }
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
            // This should not happen as the respon
            mxb_assert(!true);
            break;

        default:
            // Must be a result set.
            pResponse = translate_resultset(m_extractions, mariadb_response);
        }

        *ppResponse = pResponse;
        return READY;
    }

private:
    vector<string> m_extractions;
};

// https://docs.mongodb.com/manual/reference/command/insert/
class Insert : public TableCreatingCommand
{
public:
    using TableCreatingCommand::TableCreatingCommand;

    string create_statement() const override final
    {
        stringstream sql;
        sql << "INSERT INTO " << table_name() << " (id, doc) VALUES ";

        auto docs = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::keys::DOCUMENTS].get_array());

        bool first = true;
        for (auto element : docs)
        {
            ++m_nDocuments;

            if (first)
            {
                first = false;
            }
            else
            {
                sql << ", ";
            }

            sql << "(";

            auto doc = static_cast<bsoncxx::document::view>(element.get_document());
            auto id = get_id(doc["_id"]);

            sql << "'" << id << "'";
            sql << ", '";
            sql << bsoncxx::to_json(doc);
            sql << "'";

            sql << ")";
        }

        return sql.str();
    }

    string table_name() const override final
    {
        return get_table(mxsmongo::keys::INSERT);
    }

    State translate(ComResponse& response, GWBUF** ppResponse) override final
    {
        bsoncxx::builder::basic::document builder;

        int32_t ok = response.is_ok() ? 1 : 0;
        int64_t n = response.is_ok() ? m_nDocuments : 0;

        builder.append(bsoncxx::builder::basic::kvp("ok", ok));
        builder.append(bsoncxx::builder::basic::kvp("n", n));

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            break;

        case ComResponse::ERR_PACKET:
            add_error(builder, ComERR(response));
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
        default:
            mxb_assert(!true);
        }

        auto doc = builder.extract();

        GWBUF* pResponse = create_response(doc);

        *ppResponse = pResponse;
        return READY;
    }

private:
    string get_id(const bsoncxx::document::element& element) const
    {
        string id;

        if (element)
        {
            auto oid = element.get_oid().value;

            id = oid.to_string();
        }

        return id;
    }

    mutable int64_t m_nDocuments { 0 };
};

// https://docs.mongodb.com/manual/reference/command/update
class Update : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        GWBUF* pResponse = nullptr;
        stringstream sql;
        sql << "UPDATE " << get_table(mxsmongo::keys::UPDATE) << " ";

        auto updates = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::keys::UPDATES].get_array());

        // TODO: Deal with multiple updates.
        mxb_assert(!updates[1]);

        sql << "SET doc = ";

        auto update = static_cast<bsoncxx::document::view>(updates[0].get_document());
        auto u = update[mxsmongo::keys::U];

        switch (get_update_kind(u))
        {
        case AGGREGATION_PIPELINE:
            {
                string message("Aggregation pipeline not supported: '");
                message += bsoncxx::to_json(update);
                message += "'.";

                MXS_ERROR("%s", message.c_str());
                pResponse = create_error_response(message, mxsmongo::error::COMMAND_FAILED);
            }
            break;

        case REPLACEMENT_DOCUMENT:
            sql << "'"
                << bsoncxx::to_json(static_cast<bsoncxx::document::view>(u.get_document()))
                << "'";
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

                MXS_ERROR("%s", message.c_str());
                pResponse = create_error_response(message, mxsmongo::error::COMMAND_FAILED);
            }
        }

        if (!pResponse)
        {
            auto q = static_cast<bsoncxx::document::view>(update[mxsmongo::keys::Q].get_document());
            string where = mxsmongo::filter_to_where_clause(q);

            if (!where.empty())
            {
                sql << "WHERE " << where;
            }

            auto multi = update[mxsmongo::keys::MULTI];

            if (!multi || !multi.get_bool())
            {
                sql << " LIMIT 1";
            }

            send_downstream(sql.str());
        }

        return pResponse;
    };

    State translate(GWBUF& mariadb_response, GWBUF** ppResponse) override
    {
        // TODO: Update will be needed when DEPRECATE_EOF it turned on.
        bsoncxx::builder::basic::document builder;

        ComResponse response(GWBUF_DATA(&mariadb_response));

        int32_t is_ok = response.is_ok() ? 1 : 0;
        int64_t n = 0;
        int64_t nModified = 0;

        switch (response.type())
        {
        case ComResponse::OK_PACKET:
            {
                ComOK ok(response);
                nModified = ok.affected_rows();

                string s = ok.info().to_string();

                if (s.find("Rows matched: ") == 0)
                {
                    n = atol(s.c_str() + 14);
                }

                MXS_NOTICE("INFO: %s", s.c_str());
            }
            break;

        case ComResponse::ERR_PACKET:
            add_error(builder, ComERR(response));
            break;

        case ComResponse::LOCAL_INFILE_PACKET:
        default:
            mxb_assert(!true);
        }

        GWBUF* pResponse = create_response(builder, is_ok, n, nModified);

        *ppResponse = pResponse;
        return READY;
    };

private:
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

        if (element.type() == bsoncxx::type::k_array)
        {
            kind = AGGREGATION_PIPELINE;
        }
        else
        {
            auto doc = static_cast<bsoncxx::document::view>(element.get_document());

            for (auto field : doc)
            {
                const char* zData = field.key().data();

                if (*zData == '$')
                {
                    if (strcmp(zData, "$set") != 0 && strcmp(zData, "$unset"))
                    {
                        MXS_ERROR("'%s' contains other than the supported '$set' and '$unset' "
                                  "operations.", bsoncxx::to_json(doc).c_str());
                        kind = INVALID;
                        break;
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

            if (element.key().compare("$set") == 0)
            {
                rv += "JSON_SET(doc, ";
            }
            else if (element.key().compare("$unset") == 0)
            {
                rv += "JSON_REMOVE(doc, ";
            }
            else
            {
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

                s += "'$.";
                s += field.key().data();
                s += "', ";
                s += mxsmongo::to_string(field);
            }

            rv += s;

            rv += ")";
        }

        rv += " ";

        return rv;
    }

    GWBUF* create_response(bsoncxx::builder::basic::document& builder,
                           int32_t ok,
                           int64_t n,
                           int64_t nModified)
    {
        builder.append(bsoncxx::builder::basic::kvp("ok", ok));
        builder.append(bsoncxx::builder::basic::kvp("n", n));
        builder.append(bsoncxx::builder::basic::kvp("nModified", nModified));

        return Command::create_response(builder.extract());
    }

    GWBUF* create_response(int32_t ok, int64_t n, int64_t nModified)
    {
        bsoncxx::builder::basic::document builder;

        return create_response(builder, ok, n, nModified);
    }
};


class IsMaster : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        // TODO: Do not simply return a hardwired response.

        auto builder = bsoncxx::builder::stream::document{};
        bsoncxx::document::value doc_value = builder
            << "ismaster" << true
            << "topologyVersion" << mxsmongo::topology_version()
            << "maxBsonObjectSize" << (int32_t)16777216
            << "maxMessageSizeBytes" << (int32_t)48000000
            << "maxWriteBatchSize" << (int32_t)100000
            << "localTime" << bsoncxx::types::b_date(std::chrono::system_clock::now())
            << "logicalSessionTimeoutMinutes" << (int32_t)30
            << "connectionId" << (int32_t)4
            << "minWireVersion" << (int32_t)0
            << "maxWireVersion" << (int32_t)9
            << "readOnly" << false
            << "ok" << (double)1
            << bsoncxx::builder::stream::finalize;

        return create_response(doc_value);
    }
};

class Unknown : public mxsmongo::Database::Command
{
public:
    using mxsmongo::Database::Command::Command;

    GWBUF* execute() override
    {
        MXS_ERROR("Command not recognized: %s", m_req.to_string().c_str());

        // Inconvenient during development if every single unknown command leads
        // to an abort. Now optionally an empty document may be returned instead.
        mxb_assert(m_database.config().continue_on_unknown);

        return create_empty_response();
    }
};


template<class ConcreteCommand>
unique_ptr<mxsmongo::Database::Command> create(mxsmongo::Database* pDatabase,
                                               GWBUF* pRequest,
                                               const mxsmongo::Packet& req,
                                               const bsoncxx::document::view& doc)
{
    return unique_ptr<ConcreteCommand>(new ConcreteCommand(pDatabase, pRequest, req, doc));
}

}

struct ThisUnit
{
    const map<mxsmongo::Command,
              unique_ptr<mxsmongo::Database::Command> (*)(mxsmongo::Database* pDatabase,
                                                          GWBUF* pRequest,
                                                          const mxsmongo::Packet& req,
                                                          const bsoncxx::document::view& doc)>
    creators_by_command =
    {
        { mxsmongo::Command::DELETE,   &command::create<command::Delete> },
        { mxsmongo::Command::FIND,     &command::create<command::Find> },
        { mxsmongo::Command::INSERT,   &command::create<command::Insert> },
        { mxsmongo::Command::ISMASTER, &command::create<command::IsMaster> },
        { mxsmongo::Command::UNKNOWN,  &command::create<command::Unknown> },
        { mxsmongo::Command::UPDATE,   &command::create<command::Update> }
    };
} this_unit;

}

mxsmongo::Database::Database(const std::string& name,
                             Mongo::Context* pContext,
                             const Config* pConfig)
    : m_name(name)
    , m_context(*pContext)
    , m_config(*pConfig)
{
}

mxsmongo::Database::~Database()
{
    mxb_assert(m_state == READY);
}

//static
unique_ptr<mxsmongo::Database> mxsmongo::Database::create(const std::string& name,
                                                          Mongo::Context* pContext,
                                                          const Config* pConfig)
{
    return unique_ptr<Database>(new Database(name, pContext, pConfig));
}

GWBUF* mxsmongo::Database::handle_query(GWBUF* pRequest, const mxsmongo::Query& req)
{
    mxb_assert(is_ready());

    return execute(mxsmongo::get_command(req.query()), pRequest, req, req.query());
}

GWBUF* mxsmongo::Database::handle_command(GWBUF* pRequest,
                                          const mxsmongo::Msg& req,
                                          const bsoncxx::document::view& doc)
{
    mxb_assert(is_ready());

    return execute(mxsmongo::get_command(doc), pRequest, req, doc);
}

GWBUF* mxsmongo::Database::translate(GWBUF& mariadb_response)
{
    mxb_assert(is_pending());
    mxb_assert(m_sCommand.get());

    GWBUF* pResponse;
    Command::State state = m_sCommand->translate(mariadb_response, &pResponse);

    if (state == Command::READY)
    {
        mxb_assert(state == Command::READY);

        m_sCommand.reset();

        set_ready();
    }

    return pResponse;
}

GWBUF* mxsmongo::Database::execute(mxsmongo::Command cid,
                                   GWBUF* pRequest,
                                   const mxsmongo::Packet& req,
                                   const bsoncxx::document::view& doc)
{
    GWBUF* pResponse = nullptr;

    auto it = this_unit.creators_by_command.find(cid);
    mxb_assert(it != this_unit.creators_by_command.end());

    auto sCommand = it->second(this, pRequest, req, doc);

    try
    {
        pResponse = sCommand->execute();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Exeception occurred when parsing MongoDB command: %s", x.what());
        mxb_assert(!true);

        pResponse = sCommand->create_error_response(x.what(), mxsmongo::error::FAILED_TO_PARSE);
    }

    if (!pResponse)
    {
        m_sCommand = std::move(sCommand);
        set_pending();
    }

    return pResponse;
}
