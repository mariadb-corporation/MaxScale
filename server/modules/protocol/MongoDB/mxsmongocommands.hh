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

#include "mxsmongocommand.hh"
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <mysqld_error.h>
#include <maxbase/worker.hh>
#include "mxsmongodatabase.hh"
#include "config.hh"

using namespace std;

using mxb::Worker;

namespace mxsmongo
{

namespace command
{

class TableCreatingCommand : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

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
                                ss << "CREATE TABLE "
                                   << table_name()
                                   << " (id TEXT NOT NULL UNIQUE, doc JSON)";

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

// https://docs.mongodb.com/manual/reference/command/buildInfo/
class BuildInfo : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        // TODO: Do not simply return a hardwired response.
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("version", "4.4.1"));

        bsoncxx::builder::basic::array version_builder;

        version_builder.append(4);
        version_builder.append(4);
        version_builder.append(1);

        builder.append(bsoncxx::builder::basic::kvp("versionArray", version_builder.extract()));

        return create_response(builder.extract());
    }
};

//https://docs.mongodb.com/manual/reference/command/endSessions/
class EndSessions : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        return create_response(builder.extract());
    }
};

//https://docs.mongodb.com/manual/reference/command/whatsmyuri/
class WhatsMyUri : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        bsoncxx::builder::basic::document builder;

        builder.append(bsoncxx::builder::basic::kvp("you", "127.0.0.1:49388"));
        builder.append(bsoncxx::builder::basic::kvp("ok", 1));

        return create_response(builder.extract());
    }
};

// https://docs.mongodb.com/manual/reference/command/delete/
class Delete : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;
        sql << "DELETE FROM " << get_table(mxsmongo::key::DELETE);

        auto docs = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::key::DELETES].get_array());

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
class Find : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        stringstream sql;
        sql << "SELECT ";

        auto projection = m_doc[mxsmongo::key::PROJECTION];

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

        sql << " FROM " << get_table(mxsmongo::key::FIND);

        auto filter = m_doc[mxsmongo::key::FILTER];

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

        auto sort = m_doc[mxsmongo::key::SORT];

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

        auto skip = m_doc[mxsmongo::key::SKIP];
        auto limit = m_doc[mxsmongo::key::LIMIT];

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
                    pResponse = translate_resultset(m_extractions, nullptr);
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
            pResponse = translate_resultset(m_extractions, &mariadb_response);
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

        auto docs = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::key::DOCUMENTS].get_array());

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

            sql << id;
            sql << ", '";
            sql << bsoncxx::to_json(doc);
            sql << "'";

            sql << ")";
        }

        return sql.str();
    }

    string table_name() const override final
    {
        return get_table(mxsmongo::key::INSERT);
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
        stringstream ss;

        switch (element.type())
        {
        case bsoncxx::type::k_utf8:
            {
                const auto& utf8 = element.get_utf8();
                ss << "'" << string(utf8.value.data(), utf8.value.size()) << "'";
            }
            break;

        case bsoncxx::type::k_oid:
            ss << "'" << element.get_oid().value.to_string() << "'";
            break;

        case bsoncxx::type::k_int32:
            ss << element.get_int32();
            break;

        case bsoncxx::type::k_int64:
            ss << element.get_int64();
            break;

            // By design not using 'default' so that if a new type is introduced,
            // an explicit decision regarding what to do with it, will be needed.
        case bsoncxx::type::k_array:
        case bsoncxx::type::k_binary:
        case bsoncxx::type::k_bool:
        case bsoncxx::type::k_code:
        case bsoncxx::type::k_decimal128:
        case bsoncxx::type::k_double:
        case bsoncxx::type::k_codewscope:
        case bsoncxx::type::k_date:
        case bsoncxx::type::k_dbpointer:
        case bsoncxx::type::k_document:
        case bsoncxx::type::k_maxkey:
        case bsoncxx::type::k_minkey:
        case bsoncxx::type::k_null:
        case bsoncxx::type::k_regex:
        case bsoncxx::type::k_symbol:
        case bsoncxx::type::k_timestamp:
        case bsoncxx::type::k_undefined:
            // Casual lower-case message is what Mongo returns.
            ss << "can't use a " << bsoncxx::to_string(element.type()) << " for _id";
            throw std::runtime_error(ss.str());
        }

        return ss.str();
    }

    mutable int64_t m_nDocuments { 0 };
};

// https://docs.mongodb.com/manual/reference/command/update
class Update : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        GWBUF* pResponse = nullptr;
        stringstream sql;
        sql << "UPDATE " << get_table(mxsmongo::key::UPDATE) << " ";

        auto updates = static_cast<bsoncxx::array::view>(m_doc[mxsmongo::key::UPDATES].get_array());

        // TODO: Deal with multiple updates.
        mxb_assert(!updates[1]);

        sql << "SET doc = ";

        auto update = static_cast<bsoncxx::document::view>(updates[0].get_document());
        auto u = update[mxsmongo::key::U];

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
            auto q = static_cast<bsoncxx::document::view>(update[mxsmongo::key::Q].get_document());
            string where = mxsmongo::filter_to_where_clause(q);

            if (!where.empty())
            {
                sql << "WHERE " << where;
            }

            auto multi = update[mxsmongo::key::MULTI];

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
                const char* pData = field.key().data(); // Not necessarily null-terminated.

                if (*pData == '$')
                {
                    string name(pData, field.key().length());

                    if (name != "$set" && name != "$unset")
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

        return mxsmongo::Command::create_response(builder.extract());
    }

    GWBUF* create_response(int32_t ok, int64_t n, int64_t nModified)
    {
        bsoncxx::builder::basic::document builder;

        return create_response(builder, ok, n, nModified);
    }
};


class IsMaster : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

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

class Unknown : public mxsmongo::Command
{
public:
    using mxsmongo::Command::Command;

    GWBUF* execute() override
    {
        GWBUF* pResponse = nullptr;

        stringstream ss;
        ss << "Command not recognized: '" << bsoncxx::to_json(m_doc) << "'";
        auto s = ss.str();

        switch (m_database.config().on_unknown_command)
        {
        case Config::RETURN_ERROR:
            MXS_ERROR("%s", s.c_str());
            pResponse = create_error_response(s, mxsmongo::error::COMMAND_FAILED);
            break;

        case Config::RETURN_EMPTY:
            MXS_WARNING("%s", s.c_str());
            pResponse = create_empty_response();
            break;
        }

        return pResponse;
    }
};

}

}
