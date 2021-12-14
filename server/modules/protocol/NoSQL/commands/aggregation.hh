/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

//
// https://docs.mongodb.com/v4.4/reference/command/nav-aggregation/
//

#include "defs.hh"

namespace nosql
{

namespace command
{

// https://docs.mongodb.com/v4.4/reference/command/aggregate/

// https://docs.mongodb.com/v4.4/reference/command/count/
class Count final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "count";
    static constexpr const char* const HELP = "";

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

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
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

        default:
            ok = 1;
            n = get_n(GWBUF_DATA(mariadb_response.get()));
        }

        DocumentBuilder doc;

        doc.append(kvp(key::N, n));
        doc.append(kvp(key::OK, ok));

        *ppResponse = create_response(doc.extract());
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

// https://docs.mongodb.com/v4.4/reference/command/distinct/
class Distinct final : public SingleCommand
{
public:
    static constexpr const char* const KEY = "distinct";
    static constexpr const char* const HELP = "";

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

    State translate(mxs::Buffer&& mariadb_response, GWBUF** ppResponse) override
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
                        MXS_ERROR("Failed to parse result as individual json value: '%s'", value.c_str());
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

        *ppResponse = create_response(doc);
        return State::READY;
    }
};

// https://docs.mongodb.com/v4.4/reference/command/mapReduce/


}

}
