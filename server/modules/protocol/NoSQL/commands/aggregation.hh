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
            sql << query_to_where_clause(query);
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
            auto w = query_to_where_clause(query);

            if (w.empty())
            {
                where = "WHERE ";
            }
            else
            {
                where = w + " AND ";
            }
        }
        else
        {
            where = "WHERE ";
        }

        vector<Path> paths = get_paths(key);

        for (auto it = paths.begin(); it != paths.end(); ++it)
        {
            if (it != paths.begin())
            {
                sql << " UNION ";
            }

            const Path& p = *it;

            string extract = "JSON_EXTRACT(doc, '" + p.path + "')";

            sql << "SELECT DISTINCT(" << extract << ") FROM " << table() << " "
                << where << extract << " IS NOT NULL";

            if (!p.array.empty())
            {
                sql << " AND JSON_TYPE(JSON_EXTRACT(doc, '" << p.array << "')) = 'ARRAY'";
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

private:
    class Path
    {
    public:
        enum Kind
        {
            ELEMENT,
            ARRAY
        };

        Path(const string& path)
            : kind(ELEMENT)
            , path(path)
        {
        }

        Path(Kind kind, const string& path, const string& array)
            : kind(ARRAY)
            , path(path)
            , array(array)
        {
        }

        string to_string() const
        {
            string rv { "kind: " };
            switch (this->kind)
            {
            case ELEMENT:
                rv += "element";
                break;

            case ARRAY:
                rv += "array";
                break;
            }

            rv += "path: " + this->path + ", ";
            rv += "array: " + this->array;

            return rv;
        }

        Kind   kind;
        string path;
        string array;
    };

    static void add_part(vector<Path>& rv, const string& part)
    {
        bool is_number = false;

        char* zEnd;
        auto l = strtol(part.c_str(), &zEnd, 10);

        // Is the part a number?
        if (*zEnd == 0 && l >= 0 && l != LONG_MAX)
        {
            // Yes, so this may refer to a field whose name is a number (e.g. { a.2: 42 })
            // or the n'th element (e.g. { a: [ ... ] }).
            is_number = true;
        }

        vector<Path> tmp;

        for (const auto& p : rv)
        {
            if (p.kind == Path::ELEMENT)
            {
                tmp.push_back(Path(p.path + "." + part));
            }
            else
            {
                tmp.push_back(Path(Path::ELEMENT, p.path + "." + part, p.array));
            }

            if (is_number)
            {
                tmp.push_back(Path(Path::ARRAY, p.path + "[" + part + "]", p.path));
            }

            tmp.push_back(Path(Path::ARRAY, p.path + "[*]." + part, p.path));
        }

        rv.swap(tmp);
    }

    static vector<Path> get_paths(const string& key)
    {
        vector<Path> rv;

        string::size_type i = 0;
        string::size_type j;
        while ((j = key.find_first_of('.', i)) != string::npos)
        {
            string part = key.substr(i, j - i);

            if (rv.empty())
            {
                rv.push_back(Path("$." + part));
            }
            else
            {
                add_part(rv, part);
            }

            i = j + 1;
        }

        if (rv.empty())
        {
            rv.push_back(Path("$." + key));
        }
        else
        {
            add_part(rv, key.substr(i, j));
        }

        return rv;
    }
};

// https://docs.mongodb.com/v4.4/reference/command/mapReduce/


}

}
