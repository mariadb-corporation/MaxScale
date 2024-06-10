/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "defs.hh"
#include "../nosqlaggregationstage.hh"
#include "../nosqlcursor.hh"

namespace nosql
{

namespace command
{

// aggregate
class Aggregate final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "aggregate";
    static constexpr const char* const HELP = "";

    using SingleCommand::SingleCommand;

    State execute(Response* pNoSQL_response) override
    {
        State state;

        prepare();

        if (m_explain)
        {
            state = explain(pNoSQL_response);
        }
        else
        {
            state = SingleCommand::execute(pNoSQL_response);
        }

        return state;
    }

    State translate(GWBUF&& mariadb_response, Response* pNoSQL_response) override
    {
        mxb_assert(m_sPost_processor);

        vector<bsoncxx::document::value> docs = m_sPost_processor->post_process(std::move(mariadb_response));

        return process(docs, pNoSQL_response);
    }

private:
    State explain(Response* pNoSQL_response)
    {
        DocumentBuilder doc;
        doc.append(kvp(key::OK, 1));

        pNoSQL_response->reset(create_response(doc.extract()), Response::Status::NOT_CACHEABLE);
        return State::READY;
    }

    std::string generate_sql() override
    {
        mxb_assert(!m_explain);
        mxb_assert(!m_sql.empty());

        return m_sql;
    }

    void prepare() override
    {
        if (m_prepared)
        {
            return;
        }

        MXB_NOTICE("Aggregate: %s", bsoncxx::to_json(m_doc).c_str());

        optional(key::EXPLAIN, &m_explain);

        if (!m_explain)
        {
            m_cursor = required<bsoncxx::document::view>(key::CURSOR);
        }

        m_pipeline = required<bsoncxx::array::view>(key::PIPELINE);

        string database = m_database.name();
        string table = value_as<string>();

        aggregation::Stage* pPrevious = nullptr;
        for (auto it = m_pipeline.begin(); it != m_pipeline.end(); ++it)
        {
            auto array_element = *it;

            if (array_element.type() != bsoncxx::type::k_document)
            {
                throw SoftError("Each element of the 'pipeline' array must be an object",
                                error::TYPE_MISMATCH);
            }

            bsoncxx::document::view doc = array_element.get_document();

            // The iterator does not support arithmetic.
            auto jt = doc.begin();
            auto kt = jt;

            if (jt == doc.end() || ++kt != doc.end())
            {
                throw SoftError("A pipeline stage specification object must contain exactly one field.",
                                error::LOCATION40323);
            }

            auto field = *jt;

            auto sStage = aggregation::Stage::get(field, database, table, pPrevious);
            pPrevious = sStage.get();

            if (sStage->kind() == aggregation::Stage::Kind::SQL)
            {
                aggregation::Stage::Processor processor = sStage->update_sql(m_sql);

                if (!m_sPost_processor || processor == aggregation::Stage::Processor::REPLACE)
                {
                    m_sPost_processor = std::move(sStage);
                }
            }
            else
            {
                m_stages.emplace_back(std::move(sStage));
            }
        }

        m_prepared = true;
    }

    State translate_coll_stats(GWBUF&& mariadb_response, Response* pNoSQL_response)
    {
        uint8_t* pBuffer = mariadb_response.data();

        ComQueryResponse cqr(&pBuffer);
        auto nFields = cqr.nFields();
        mxb_assert(nFields == 4);

        vector<enum_field_types> types;

        for (size_t i = 0; i < nFields; ++i)
        {
            ComQueryResponse::ColumnDef column_def(&pBuffer);

            types.push_back(column_def.type());
        }

        ComResponse eof(&pBuffer);
        mxb_assert(eof.type() == ComResponse::EOF_PACKET);

        vector<bsoncxx::document::value> docs;

        while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
        {
            CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
            auto it = row.begin();

            int64_t nTable_rows = std::stoll((*it++).as_string().to_string());
            int64_t nAvg_row_length = std::stoll((*it++).as_string().to_string());
            int64_t nData_length = std::stoll((*it++).as_string().to_string());
            int64_t nIndex_length = std::stoll((*it++).as_string().to_string());

            DocumentBuilder storage_stats;
            storage_stats.append(kvp("size", nData_length + nIndex_length));
            storage_stats.append(kvp("count", nTable_rows));
            storage_stats.append(kvp("avgObjSize", nAvg_row_length));
            storage_stats.append(kvp("numOrphanDocs", 0));
            storage_stats.append(kvp("storageSize", nData_length + nIndex_length));
            storage_stats.append(kvp("totalIndexSize", nIndex_length));
            storage_stats.append(kvp("freeStorageSize", 0));
            storage_stats.append(kvp("nindexes", 1));
            storage_stats.append(kvp("capped", false));

            DocumentBuilder doc;
            doc.append(kvp("storageStats", storage_stats.extract()));

            docs.emplace_back(doc.extract());
        }

        return process(docs, pNoSQL_response);
    }

    State translate_docs(GWBUF&& mariadb_response, Response* pNoSQL_response)
    {
        uint8_t* pBuffer = mariadb_response.data();

        ComQueryResponse cqr(&pBuffer);
        auto nFields = cqr.nFields();
        mxb_assert(nFields == 1);

        vector<enum_field_types> types;

        for (size_t i = 0; i < nFields; ++i)
        {
            ComQueryResponse::ColumnDef column_def(&pBuffer);

            types.push_back(column_def.type());
        }

        ComResponse eof(&pBuffer);
        mxb_assert(eof.type() == ComResponse::EOF_PACKET);

        vector<bsoncxx::document::value> docs;

        while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
        {
            CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
            auto it = row.begin();

            bsoncxx::document::value doc = bsoncxx::from_json((*it).as_string().to_string());
            docs.emplace_back(std::move(doc));
        }

        return process(docs, pNoSQL_response);
    }

    State process(vector<bsoncxx::document::value>& in, Response* pNoSQL_response)
    {
        for (const auto& sStage : m_stages)
        {
            vector<bsoncxx::document::value> out = sStage->process(in);

            in.swap(out);
        }

        unique_ptr<NoSQLCursor> sCursor = NoSQLCursorBson::create(table(Quoted::NO), std::move(in));

        DocumentBuilder doc;
        sCursor->create_first_batch(worker(), doc, 100, false);

        GWBUF* pResponse = create_response(doc.extract());

        Response::Status status = Response::Status::NOT_CACHEABLE;

        if (!sCursor->exhausted())
        {
            NoSQLCursor::put(std::move(sCursor));
        }
        else
        {
            // If the cursor is exhausted, i.e., either the number of returned items
            // was small enough or 'singleBatch=true' was specified, the result is
            // cacheable. Otherwise things get complicated and no caching is performed.
            status = Response::Status::CACHEABLE;
        }

        pNoSQL_response->reset(pResponse, status);
        return State::READY;
    }

    using Handler = function<State (GWBUF&&, Response*)>;
    using SStage  = unique_ptr<aggregation::Stage>;

    bool                    m_prepared { false };
    bool                    m_explain { false };
    bsoncxx::document::view m_cursor;
    bsoncxx::array::view    m_pipeline;
    vector<SStage>          m_stages;
    std::string             m_sql;
    SStage                  m_sPost_processor;
};

// count
class Count final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "count";
    static constexpr const char* const HELP = "";
    static constexpr const bool IS_CACHEABLE = true;

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        ostringstream sql;

        string limit = convert_skip_and_limit(AcceptAsLimit::INTEGER);

        if (limit.empty())
        {
            sql << "SELECT count(id) FROM " << table() << " ";
        }
        else
        {
            // A simple 'SELECT count(...) ... LIMIT ...' returns an empty set with no information.
            sql << "SELECT count(id) FROM (SELECT id FROM " << table() << " ";
        }

        bsoncxx::document::view query;
        if (optional(key::QUERY, &query))
        {
            sql << where_clause_from_query(query) << " ";
        }

        if (!limit.empty())
        {
            sql << limit << ") AS t";
        }

        return sql.str();
    }

    State translate(GWBUF&& mariadb_response, Response* pNoSQL_response) override
    {
        ComResponse response(mariadb_response.data());

        int32_t ok = 0;
        int32_t n = 0;

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_NO_SUCH_TABLE)
                {
                    ok = 1;
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
            break;

        default:
            ok = 1;
            n = get_n(mariadb_response.data());
        }

        DocumentBuilder doc;

        doc.append(kvp(key::N, n));
        doc.append(kvp(key::OK, ok));

        pNoSQL_response->reset(create_response(doc.extract()), Response::Status::CACHEABLE);
        return State::READY;
    }

private:
    int32_t get_n(uint8_t* pBuffer)
    {
        int32_t n = 0;

        ComQueryResponse cqr(&pBuffer);
        mxb_assert(cqr.nFields());

        ComQueryResponse::ColumnDef column_def(&pBuffer);
        vector<enum_field_types> types { column_def.type() };

        ComResponse eof(&pBuffer);
        mxb_assert(eof.type() == ComResponse::EOF_PACKET);

        CQRTextResultsetRow row(&pBuffer, types);

        auto it = row.begin();
        mxb_assert(it != row.end());

        const auto& value = *it++;
        mxb_assert(it == row.end());

        n = std::stoi(value.as_string().to_string());

        return n;
    }
};

// distinct
class Distinct final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "distinct";
    static constexpr const char* const HELP = "";
    static constexpr const bool IS_CACHEABLE = true;

    using SingleCommand::SingleCommand;

    string generate_sql() override
    {
        ostringstream sql;

        string key = required<string>(key::KEY);

        // TODO: Move these key checks somewhere more common.
        if (key.size() == 0)
        {
            throw SoftError("FieldPath cannot be constructed with empty string", error::LOCATION40352);
        }

        if (key.find('\0') != string::npos)
        {
            throw SoftError("Key field cannot contain an embedded null byte", error::LOCATION31032);
        }

        auto i = key.find_last_of('.');

        if (i == key.length() - 1)
        {
            throw SoftError("FieldPath must not end with a '.'.", error::LOCATION40353);
        }

        string where;
        bsoncxx::document::view query;
        if (optional(key::QUERY, &query, Conversion::RELAXED))
        {
            where = where_clause_from_query(query) + " AND ";
        }
        else
        {
            where = "WHERE ";
        }

        vector<Path::Incarnation> paths = Path::get_incarnations(key);

        for (auto it = paths.begin(); it != paths.end(); ++it)
        {
            if (it != paths.begin())
            {
                sql << " UNION ";
            }

            const Path::Incarnation& p = *it;

            string extract = "JSON_EXTRACT(doc, '$." + p.path() + "')";

            sql << "SELECT DISTINCT(" << extract << ") FROM " << table() << " "
                << where << extract << " IS NOT NULL";

            if (p.has_array_demand())
            {
                sql << " AND JSON_TYPE(JSON_EXTRACT(doc, '$." << p.array_path() << "')) = 'ARRAY'";
            }
        }

        return sql.str();
    }

    State translate(GWBUF&& mariadb_response, Response* pNoSQL_response) override
    {
        uint8_t* pBuffer = mariadb_response.data();

        ComResponse response(pBuffer);

        int32_t ok = 0;
        ostringstream json;
        json << "{ \"values\": [";

        switch (response.type())
        {
        case ComResponse::ERR_PACKET:
            {
                ComERR err(response);

                auto code = err.code();

                if (code == ER_NO_SUCH_TABLE)
                {
                    ok = 1;
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
            break;

        default:
            {
                ok = 1;

                ComQueryResponse cqr(&pBuffer);
                mxb_assert(cqr.nFields() == 1);

                ComQueryResponse::ColumnDef column_def(&pBuffer);
                vector<enum_field_types> types { column_def.type() };

                ComResponse eof(&pBuffer);
                mxb_assert(eof.type() == ComResponse::EOF_PACKET);

                set<string> values;
                while (ComResponse(pBuffer).type() != ComResponse::EOF_PACKET)
                {
                    CQRTextResultsetRow row(&pBuffer, types); // Advances pBuffer
                    auto it = row.begin();

                    auto value = (*it).as_string().to_string();

                    json_error_t error;
                    json_t* pJson = json_loadb(value.c_str(), value.length(), JSON_DECODE_ANY, &error);

                    if (pJson)
                    {
                        // If an individual value is an array, then it will be unwrapped.
                        // TODO: Should an array recursively be unwrapped?

                        if (json_is_array(pJson))
                        {
                            size_t index;
                            json_t* pValue;

                            json_array_foreach(pJson, index, pValue) {
                                char* zValue = json_dumps(pValue, JSON_ENCODE_ANY);
                                values.insert(zValue);
                                free(zValue);
                            }
                        }
                        else
                        {
                            values.insert(value);
                        }

                        json_decref(pJson);
                    }
                    else
                    {
                        MXB_ERROR("Failed to parse result as individual json value: '%s'", value.c_str());
                        values.insert(value);
                    }
                }

                bool first = true;
                for (const auto& value : values)
                {
                    if (!first)
                    {
                        json << ", ";
                    }
                    else
                    {
                        first = false;
                    }

                    json << value;
                }
            }
        }

        json << "], \"ok\": " << ok << "}";

        auto doc = bsoncxx::from_json(json.str());

        pNoSQL_response->reset(create_response(doc), Response::Status::CACHEABLE);
        return State::READY;
    }
};

// mapReduce

}

}
