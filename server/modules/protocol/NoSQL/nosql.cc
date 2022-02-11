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

#include "nosql.hh"
#include <sstream>
#include <set>
#include <map>
#include <bsoncxx/json.hpp>
#include <maxscale/dcb.hh>
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "../../filter/masking/mysql.hh"
#include "clientconnection.hh"
#include "nosqldatabase.hh"
#include "nosqlupdateoperator.hh"
#include "crc32.h"

using namespace std;

namespace
{

uint32_t (*crc32_func)(const void *, size_t) = wiredtiger_crc32c_func();

}

namespace
{

using namespace nosql;

string get_condition(const bsoncxx::document::view& doc);

// https://docs.mongodb.com/manual/reference/operator/query/and/#op._S_and
string get_and_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& item = *it;

        if (item.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(item.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " AND ";
                }

                condition += sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/nor/#op._S_nor
string get_nor_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& element = *it;

        if (element.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(element.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " AND ";
                }

                condition += "NOT " + sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/or/#op._S_or
string get_or_condition(const bsoncxx::array::view& array)
{
    string condition;

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& element = *it;

        if (element.type() == bsoncxx::type::k_document)
        {
            string sub_condition = get_condition(element.get_document().view());

            if (sub_condition.empty())
            {
                condition.clear();
                break;
            }
            else
            {
                if (!condition.empty())
                {
                    condition += " OR ";
                }

                condition += sub_condition;
            }
        }
        else
        {
            throw nosql::SoftError("$or/$and/$nor entries need to be full objects",
                                   nosql::error::BAD_VALUE);
        }
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

// https://docs.mongodb.com/manual/reference/operator/query/#logical
string get_logical_condition(const bsoncxx::document::element& element)
{
    string condition;

    const auto& key = element.key();

    auto get_array = [](const char* zOp, const bsoncxx::document::element& element)
    {
        if (element.type() != bsoncxx::type::k_array)
        {
            ostringstream ss;
            ss << zOp << " must be an array";

            throw SoftError(ss.str(), error::BAD_VALUE);
        }

        auto array = static_cast<bsoncxx::array::view>(element.get_array());

        auto begin = array.begin();
        auto end = array.end();

        if (begin == end)
        {
            throw SoftError("$and/$or/$nor must be a nonempty array", error::BAD_VALUE);
        }

        return array;
    };

    if (key.compare("$and") == 0)
    {
        condition = get_and_condition(get_array("$and", element));
    }
    else if (key.compare("$nor") == 0)
    {
        condition = get_nor_condition(get_array("$nor", element));
    }
    else if (key.compare("$or") == 0)
    {
        condition = get_or_condition(get_array("$or", element));
    }
    else if (key.compare("$alwaysFalse") == 0)
    {
        double d;
        if (!get_number_as_double(element, &d) || d != 1)
        {
            ostringstream ss;
            ss << "Expected a number in : $alwaysFalse: " << element_to_string(element);
            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }

        condition = "(false)";
    }
    else if (key.compare("$alwaysTrue") == 0)
    {
        double d;
        if (!get_number_as_double(element, &d) || d != 1)
        {
            ostringstream ss;
            ss << "Expected a number in : $alwaysTrue: " << element_to_string(element);
            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }

        condition = "(true)";
    }
    else
    {
        ostringstream ss;
        ss << "unknown top level operator: " << key;

        throw nosql::SoftError(ss.str(), nosql::error::BAD_VALUE);
    }

    return condition;
}

using ElementValueToString = string (*)(const bsoncxx::document::element& element,
                                        ValueFor,
                                        const string& op);
using FieldAndElementValueToComparison = string (*) (const Path::Incarnation& p,
                                                     const bsoncxx::document::element& element,
                                                     mariadb::Op mariadb_op,
                                                     const string& nosql_op,
                                                     ElementValueToString value_to_string);

struct ElementValueInfo
{
    mariadb::Op                      mariadb_op;
    ElementValueToString             value_to_string;
    FieldAndElementValueToComparison field_and_value_to_comparison;
};

void double_to_string(double d, ostream& os)
{
    // printf("%.20g\n", -std::numeric_limits<double>::max()) => "-1.7976931348623157081e+308"
    char buffer[28];

    sprintf(buffer, "%.20g", d);

    os << buffer;

    if (strpbrk(buffer, ".e") == nullptr)
    {
        // No decimal point, add ".0" to prevent this number from being an integer.
        os << ".0";
    }
}

string double_to_string(double d)
{
    ostringstream ss;
    double_to_string(d, ss);
    return ss.str();
}

string elemMatch_to_json_contain(const string& subfield,
                                 const Path::Incarnation& p,
                                 const bsoncxx::document::element& elemMatch)
{
    auto key = elemMatch.key();

    string value;
    if (key.compare("$eq") == 0)
    {
        value = "1";
    }
    else if (key.compare("$ne") == 0)
    {
        value = "0";
    }
    else
    {
        throw SoftError("$elemMatch supports only operators $eq and $ne (MaxScale)",
                        error::BAD_VALUE);
    }

    return "(JSON_CONTAINS(doc, JSON_OBJECT(\"" + subfield + "\", "
        + element_to_value(elemMatch, ValueFor::JSON_NESTED, "$elemMatch") + "), '$."
        + p.path() + "') = " + value + ")";
}

string elemMatch_to_json_contain(const string& subfield,
                                 const Path::Incarnation& p,
                                 const bsoncxx::document::view& elemMatch)
{
    string rv;

    if (elemMatch.empty())
    {
        rv = "false";
    }
    else
    {
        for (const auto& element : elemMatch)
        {
            rv = elemMatch_to_json_contain(subfield, p, element);
        }
    }

    return rv;
}

string elemMatch_to_json_contain(const Path::Incarnation& p, const bsoncxx::document::element& elemMatch)
{
    string rv;

    auto key = elemMatch.key();

    if (key.find("$") == 0)
    {
        string value;

        if (key.compare("$eq") == 0)
        {
            value = "1";
        }
        else if (key.compare("$ne") == 0)
        {
            value = "0";
        }
        else
        {
            throw SoftError("$elemMatch supports only operators $eq and $ne (MaxScale)",
                            error::BAD_VALUE);
        }

        rv = "(JSON_CONTAINS(doc, "
            + element_to_value(elemMatch, ValueFor::JSON, "$elemMatch") + ", '$." + p.path() + "') = " + value
            + ")";
    }
    else
    {
        if (elemMatch.type() == bsoncxx::type::k_document)
        {
            bsoncxx::document::view doc = elemMatch.get_document();
            rv = elemMatch_to_json_contain((string)key, p, doc);
        }
        else
        {
            rv = "(JSON_CONTAINS(doc, JSON_OBJECT(\"" + (string)key + "\", "
                + element_to_value(elemMatch, ValueFor::JSON_NESTED, "$elemMatch") + "), '$."
                + p.path() + "') = 1)";

            if (elemMatch.type() == bsoncxx::type::k_null)
            {
                rv += " OR (JSON_EXTRACT(doc, '$." + p.path() + "." + (string)key + "') IS NULL)";
            }
        }
    }

    return rv;
}

string elemMatch_to_json_contains(const Path::Incarnation& p, const bsoncxx::document::view& doc)
{
    string condition;

    for (const auto& elemMatch : doc)
    {
        if (!condition.empty())
        {
            condition += " AND ";
        }

        condition += elemMatch_to_json_contain(p, elemMatch);
    }

    if (!condition.empty())
    {
        condition = "(" + condition + ")";
    }

    return condition;
}

bool is_scalar_value(const bsoncxx::document::element& element)
{
    bool rv;

    switch (element.type())
    {
    case bsoncxx::type::k_array:
    case bsoncxx::type::k_document:
        rv = false;
        break;

    default:
        rv = true;
        break;
    }

    return rv;
}

string timestamp_to_condition(const Path::Incarnation& p,
                              mariadb::Op op,
                              const bsoncxx::types::b_timestamp& ts)
{
    ostringstream ss;

    string f = "$." + p.path() + ".$timestamp";

    ss << "(JSON_QUERY(doc, '" << f << "') IS NOT NULL AND ";

    switch (op)
    {
    case mariadb::Op::EQ:
    case mariadb::Op::NE:
        ss << "JSON_VALUE(doc, '" << f << ".t') " << op << ts.timestamp << " AND "
           << "JSON_VALUE(doc, '" << f << ".i') " << op << ts.increment;
        break;

    case mariadb::Op::LT:
    case mariadb::Op::GT:
        ss << "JSON_VALUE(doc, '" << f << ".t') " << op << ts.timestamp;
        break;

    case mariadb::Op::LTE:
        ss << "(JSON_VALUE(doc, '" << f << ".t') < " << ts.timestamp << " OR "
           << "(JSON_VALUE(doc, '" << f << ".t') = " << ts.timestamp << " AND "
           << "JSON_VALUE(doc, '" << f << ".i') = " << ts.increment << "))";
        break;

    case mariadb::Op::GTE:
        ss << "(JSON_VALUE(doc, '" << f << ".t') > " << ts.timestamp << " OR "
           << "(JSON_VALUE(doc, '" << f << ".t') = " << ts.timestamp << " AND "
           << "JSON_VALUE(doc, '" << f << ".i') = " << ts.increment << "))";
        break;
    }

    ss << ")";

    return ss.str();
}

string timestamp_to_condition(const Path::Incarnation& p, const bsoncxx::types::b_timestamp& timestamp)
{
    return timestamp_to_condition(p, mariadb::Op::EQ, timestamp);
}

string default_field_and_value_to_comparison(const Path::Incarnation& p,
                                             const bsoncxx::document::element& element,
                                             mariadb::Op mariadb_op,
                                             const string& nosql_op,
                                             ElementValueToString value_to_string)
{
    string rv;
    string path;

    switch (element.type())
    {
    case bsoncxx::type::k_binary:
        path = p.path() + ".$binary";
        break;

    case bsoncxx::type::k_date:
        path = p.path() + ".$date";
        break;

    case bsoncxx::type::k_code:
        path = p.path() + ".$code";
        break;

    case bsoncxx::type::k_timestamp:
        rv = timestamp_to_condition(p, mariadb_op, element.get_timestamp());
        break;

    case bsoncxx::type::k_regex:
        if (nosql_op != "$eq")
        {
            ostringstream ss;
            ss << "Can't have regex as arg to " << nosql_op;

            throw SoftError(ss.str(), error::BAD_VALUE);
        }
        // Fallthrough
    default:
        path = p.path();
    }

    if (rv.empty())
    {
        ostringstream ss;
        if (mariadb_op == mariadb::Op::NE)
        {
            ss << "(JSON_EXTRACT(doc, '$." << path << "') IS NULL OR ";
        }
        else
        {
            ss << "(JSON_EXTRACT(doc, '$." << path << "') IS NOT NULL AND ";
        }

        ss << "(JSON_EXTRACT(doc, '$." << path << "') " << mariadb_op << " "
           << value_to_string(element, ValueFor::SQL, nosql_op)
           << "))";

        rv = ss.str();
    }

    return rv;
}

string field_and_value_to_eq_comparison(const Path::Incarnation& p,
                                        const bsoncxx::document::element& element,
                                        mariadb::Op mariadb_op,
                                        const string& nosql_op,
                                        ElementValueToString value_to_string)
{
    string rv;

    if (element.type() == bsoncxx::type::k_null)
    {
        if (nosql_op == "$eq")
        {
            rv = "(JSON_EXTRACT(doc, '$." + p.path() + "') IS NULL "
                + "OR (JSON_CONTAINS(JSON_QUERY(doc, '$." + p.path() + "'), null) = 1) "
                + "OR (JSON_VALUE(doc, '$." + p.path() + "') = 'null'))";
        }
        else if (nosql_op == "$ne")
        {
            rv = "(JSON_EXTRACT(doc, '$." + p.path() + "') IS NOT NULL "
                + "AND (JSON_CONTAINS(JSON_QUERY(doc, '$." + p.path() + "'), 'null') = 0) "
                + "OR (JSON_VALUE(doc, '$." + p.path() + "') != 'null'))";
        }
    }
    else
    {
        rv = default_field_and_value_to_comparison(p, element, mariadb_op, nosql_op, value_to_string);
    }

    return rv;
}

const unordered_map<string, ElementValueInfo> converters =
{
    { "$eq",     { mariadb::Op::EQ,  &element_to_value, field_and_value_to_eq_comparison } },
    { "$gt",     { mariadb::Op::GT,  &element_to_value, default_field_and_value_to_comparison } },
    { "$gte",    { mariadb::Op::GTE, &element_to_value, default_field_and_value_to_comparison } },
    { "$lt",     { mariadb::Op::LT,  &element_to_value, default_field_and_value_to_comparison } },
    { "$lte",    { mariadb::Op::LTE, &element_to_value, default_field_and_value_to_comparison } },
    { "$ne",     { mariadb::Op::NE,  &element_to_value, field_and_value_to_eq_comparison } },
};

inline const char* to_description(Path::Incarnation::ArrayOp op)
{
    switch (op)
    {
    case Path::Incarnation::ArrayOp::AND:
        return "$and";

    case Path::Incarnation::ArrayOp::OR:
        return "$or";
    }

    mxb_assert(!true);
    return nullptr;
}

inline const char* to_logical_operator(Path::Incarnation::ArrayOp op)
{
    switch (op)
    {
    case Path::Incarnation::ArrayOp::AND:
        return " AND ";

    case Path::Incarnation::ArrayOp::OR:
        return " OR ";
    }

    mxb_assert(!true);
    return nullptr;
}


void add_element_array(ostream& ss,
                       bool is_scoped,
                       const string& field,
                       const char* zDescription,
                       const bsoncxx::array::view& all_elements)
{
    vector<bsoncxx::document::view> elem_matches;

    ss << "(JSON_CONTAINS(";

    if (is_scoped)
    {
        // JSON_EXTRACT has to be used here, because, given a
        // document like '{"a" : [ { "x" : 1.0 }, { "x" : 2.0 } ] }'}
        // and a query like 'c.find({ "a.x" : { "$all" : [ 1, 2 ] } }',
        // the JSON_EXTRACT below will with the path '$.a[*].x' return
        // for that document the array '[1.0, 2.0]', which will match
        // the array, which is what we want.
        ss << "JSON_EXTRACT(doc, '$." << field << "'), JSON_ARRAY(";
    }
    else
    {
        ss << "doc, JSON_ARRAY(";
    }

    bool is_single =
        all_elements.begin() != all_elements.end() &&
        ++all_elements.begin() == all_elements.end();
    bool is_null = false;
    bool first_element = true;
    for (const auto& one_element : all_elements)
    {
        string value;

        switch (one_element.type())
        {
        case bsoncxx::type::k_null:
            is_null = true;
            break;

        case bsoncxx::type::k_regex:
            // Regexes cannot be added, as they are not values to be compared.
            break;

        case bsoncxx::type::k_document:
            {
                auto doc = static_cast<bsoncxx::document::view>(one_element.get_document());

                auto it = doc.begin();
                auto end = doc.end();

                if (it != end && it->key().compare("$elemMatch") == 0)
                {
                    auto element = *it;

                    if (element.type() != bsoncxx::type::k_document)
                    {
                        throw SoftError("$elemMatch needs an Object", error::BAD_VALUE);
                    }

                    elem_matches.push_back(element.get_document());
                }
                else
                {
                    value = element_to_value(one_element, ValueFor::JSON_NESTED, zDescription);
                }
            }
            break;

        default:
            value = element_to_value(one_element, ValueFor::JSON_NESTED, zDescription);
        }

        if (!value.empty())
        {
            if (first_element)
            {
                first_element = false;
            }
            else
            {
                ss << ", ";
            }

            ss << value;
        }
    }

    if (is_scoped)
    {
        ss << ")) = 1";
    }
    else
    {
        ss << "), '$." << field << "') = 1";
    }

    // With [*][*] we e.g. exclude [[2]] when looking for [2].
    ss << " AND JSON_EXTRACT(doc, '$." << field << "[*][*]') IS NULL";


    for(const auto& elem_match : elem_matches)
    {
        for (auto it = elem_match.begin(); it != elem_match.end(); ++it)
        {
            ss << " AND ";

            auto element = *it;

            ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << field << "')) = 'ARRAY' AND "
               << "((JSON_CONTAINS(JSON_EXTRACT(doc, '$." << field << "[*]'), "
               << "JSON_OBJECT(\"" << element.key() << "\", "
               << element_to_value(*it, ValueFor::JSON_NESTED, zDescription)
               << ")) = 1) OR "
               << "(JSON_QUERY(doc, '$." << field << "[*]') IS NOT NULL AND "
               << "JSON_EXTRACT(doc, '$." << field << "[*]." << element.key() << "') IS NULL)))";
        }
    }

    ss << ")";

    if (is_single)
    {
        auto element = *all_elements.begin();
        if (element.type() != bsoncxx::type::k_document)
        {
            ss << " OR (JSON_VALUE(doc, '$." << field << "') = "
               << element_to_value(element, ValueFor::SQL, zDescription)
               << ")";
        }
    }

    if (is_null)
    {
        ss << " OR (JSON_EXTRACT(doc, '$." << field << "') IS NULL)";
    }
}

string protocol_type_to_mariadb_type(int32_t number)
{
    switch (number)
    {
    case protocol::type::DOUBLE:
        return "'DOUBLE'";

    case protocol::type::STRING:
        return "'STRING'";

    case protocol::type::OBJECT:
        return "'OBJECT'";

    case protocol::type::ARRAY:
        return "'ARRAY'";

    case protocol::type::BOOL:
        return "'BOOLEAN'";

    case protocol::type::NULL_TYPE:
        return "'NULL'";

    case protocol::type::INT32:
    case protocol::type::INT64:
        return "'INTEGER'";

    case protocol::type::BIN_DATA:
    case protocol::type::UNDEFINED:
    case protocol::type::OBJECT_ID:
    case protocol::type::DATE:
    case protocol::type::REGEX:
    case protocol::type::DB_POINTER:
    case protocol::type::JAVASCRIPT:
    case protocol::type::SYMBOL:
    case protocol::type::JAVASCRIPT_SCOPE:
    case protocol::type::TIMESTAMP:
    case protocol::type::DECIMAL128:
    case protocol::type::MIN_KEY:
    case protocol::type::MAX_KEY:
        break;

    default:
        {
            ostringstream ss;
            ss << "Invalid numerical type code: " << number;
            throw SoftError(ss.str(), error::BAD_VALUE);
        };
    }

    ostringstream ss;
    ss << "Unsupported type code: " << number << " (\"" << protocol::type::to_alias(number) << "\")";
    throw SoftError(ss.str(), error::BAD_VALUE);

    return nullptr;
}

string type_to_condition_from_number(const Path::Incarnation& p, int32_t number)
{
    ostringstream ss;

    switch (number)
    {
    case protocol::type::BIN_DATA:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$binary')) = 'STRING' AND "
           << "JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$type')) = 'STRING')";
        break;

    case protocol::type::DATE:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$date')) = 'INTEGER')";
        break;

    case protocol::type::JAVASCRIPT:
    case protocol::type::JAVASCRIPT_SCOPE:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$code')) = 'STRING')";
        break;

    case protocol::type::REGEX:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$regex')) = 'STRING' AND "
           << "JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$options')) = 'STRING')";
        break;

    case protocol::type::TIMESTAMP:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$timestamp.t')) = 'INTEGER' AND "
           << "JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$timestamp.i')) = 'INTEGER')";
        break;

    case protocol::type::UNDEFINED:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << ".$undefined')) = 'BOOLEAN')";
        break;

    default:
        ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = "
           << protocol_type_to_mariadb_type(number)
           << ")";
    }

    return ss.str();
}

template<class document_or_array_element>
string type_to_condition_from_value(const Path::Incarnation& p, const document_or_array_element& element)
{
    string rv;

    int32_t type = -1;

    switch (element.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view alias = element.get_utf8();

            if (alias.compare("number") == 0)
            {
                ostringstream ss;

                ss << "(JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = 'DOUBLE' OR "
                   << "JSON_TYPE(JSON_EXTRACT(doc, '$." << p.path() << "')) = 'INTEGER')";

                rv = ss.str();
            }
            else
            {
                type = protocol::alias::to_type(string(alias.data(), alias.length()));
            }
        }
        break;

    case bsoncxx::type::k_double:
        {
            double d = element.get_double();
            type = d;

            if (d != (double)type)
            {
                ostringstream ss;
                ss << "Invalid numerical type code: " << d;
                throw SoftError(ss.str(), error::BAD_VALUE);
            }
        };
        break;

    case bsoncxx::type::k_int32:
        type = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        type = element.get_int32();
        break;

    default:
        throw SoftError("type must be represented as a number or a string", error::TYPE_MISMATCH);
    }

    if (rv.empty())
    {
        rv = type_to_condition_from_number(p, type);
    }

    return rv;
}

string regex_to_condition(const Path::Incarnation& p,
                          const string_view& regex,
                          const string_view& options)
{
    ostringstream ss1;

    ss1 << "(JSON_VALUE(doc, '$." << p.path() << "') ";

    ostringstream ss2;
    if (options.length() != 0)
    {
        ss2 << "(?" << options << ")";
    }

    ss2 << regex;

    ss1 << "REGEXP '" << escape_essential_chars(ss2.str()) << "' OR ";

    ss1 << "(JSON_QUERY(doc, '$." << p.path() << "') IS NOT NULL AND "
        << "JSON_COMPACT(JSON_QUERY(doc, '$." << p.path() << "')) = "
        << "JSON_COMPACT(JSON_OBJECT(\"$regex\", \"" << regex << "\", \"$options\", \"" << options <<"\"))))";

    return ss1.str();
}

string regex_to_condition(const Path::Incarnation& p, const bsoncxx::types::b_regex& regex)
{
    return regex_to_condition(p, regex.regex, regex.options);
}

string regex_to_condition(const Path::Incarnation& p,
                          const bsoncxx::document::element& regex,
                          const bsoncxx::document::element& options)
{
    if (options && !regex)
    {
        throw SoftError("$options needs a $regex", error::BAD_VALUE);
    }

    if (regex.type() != bsoncxx::type::k_utf8)
    {
        throw SoftError("$regex has to be a string", error::BAD_VALUE);
    }

    string_view o;
    if (options)
    {
        if (options.type() != bsoncxx::type::k_utf8)
        {
            throw SoftError("$options has to be a string", error::BAD_VALUE);
        }

        o = options.get_utf8();
    }

    return regex_to_condition(p, regex.get_utf8(), o);
}

bool is_hex(const string& s)
{
    auto isxdigit = [](char c)
    {
        return std::isxdigit(c);
    };

    return std::all_of(s.begin(), s.end(), isxdigit);
}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string get_comparison_condition(const bsoncxx::document::element& element)
{
    string condition;

    string field = static_cast<string>(element.key());
    auto type = element.type();

    if (field == "_id" && type != bsoncxx::type::k_document)
    {
        condition = "( id = '";

        bool is_utf8 = (type == bsoncxx::type::k_utf8);

        if (is_utf8)
        {
            condition += "\"";
        }

        auto id = to_string(element);

        condition += id;

        if (is_utf8)
        {
            condition += "\"";
        }

        condition += "'";

        if (is_utf8 && id.length() == 24 && is_hex(id))
        {
            // This sure looks like an ObjectId. And this is the way it will appear
            // if a search is made using a DBPointer. So we'll cover that case as well.

            condition += " OR id = '{\"$oid\":\"" + id + "\"}'";
        }

        condition += ")";
    }
    else
    {
        Path path(element);

        condition = path.get_comparison_condition();
    }

    return condition;
}

string get_condition(const bsoncxx::document::element& element)
{
    string condition;

    const auto& key = element.key();

    if (key.size() == 0)
    {
        return condition;
    }

    if (key.front() == '$')
    {
        condition = get_logical_condition(element);
    }
    else
    {
        condition = get_comparison_condition(element);
    }

    return condition;
}

string get_condition(const bsoncxx::document::view& doc)
{
    string where;

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        const auto& element = *it;

        string condition = get_condition(element);

        if (condition.empty())
        {
            where.clear();
            break;
        }
        else
        {
            if (!where.empty())
            {
                where += " AND ";
            }

            where += condition;
        }
    }

    return where;
}

enum class UpdateKind
{
    AGGREGATION_PIPELINE, // Element is an array
    REPLACEMENT_DOCUMENT, // Element is a document
    UPDATE_OPERATORS,     // Element is a document
    INVALID
};

UpdateKind get_update_kind(const bsoncxx::document::view& update_specification)
{
    UpdateKind kind = UpdateKind::INVALID;

    if (update_specification.empty())
    {
        kind = UpdateKind::REPLACEMENT_DOCUMENT;
    }
    else
    {
        for (auto field : update_specification)
        {
            auto key = field.key();
            string name(key.data(), key.length());
            char front = (name.empty() ? 0 : name.front());

            if (front == '$')
            {
                if (kind == UpdateKind::INVALID || kind == UpdateKind::UPDATE_OPERATORS)
                {
                    if (!update_operator::is_supported(name))
                    {
                        ostringstream ss;
                        ss << "Unknown modifier: " << name
                           << ". Expected a valid update modifier or "
                           << "pipeline-style update specified as an array. "
                           << "Currently the only supported update operators are: ";

                        ss << mxb::join(update_operator::supported_operators());

                        throw SoftError(ss.str(), error::COMMAND_FAILED);
                    }

                    kind = UpdateKind::UPDATE_OPERATORS;
                }
                else
                {
                    // TODO: See above.
                    ostringstream ss;
                    ss << "The dollar ($) prefixed field '" << name << "' in '" << name << "' "
                       << "is not valid for storage.";

                    throw SoftError(ss.str(), error::DOLLAR_PREFIXED_FIELD_NAME);
                }
            }
            else
            {
                if (kind == UpdateKind::INVALID)
                {
                    kind = UpdateKind::REPLACEMENT_DOCUMENT;
                }
                else if (kind != UpdateKind::REPLACEMENT_DOCUMENT)
                {
                    // TODO: See above.
                    ostringstream ss;
                    ss << "Unknown modifier: " << name
                       << ". Expected  a valid update modifier or "
                       << "pipeline-style update specified as an array";

                    throw SoftError(ss.str(), error::FAILED_TO_PARSE);
                }
            }
        }
    }

    mxb_assert(kind != UpdateKind::INVALID);

    return kind;
}

UpdateKind get_update_kind(const bsoncxx::document::element& update_specification)
{
    UpdateKind kind = UpdateKind::INVALID;

    switch (update_specification.type())
    {
    case bsoncxx::type::k_array:
        kind = UpdateKind::AGGREGATION_PIPELINE;
        break;

    default:
        kind = get_update_kind(update_specification.get_document());
    }

    mxb_assert(kind != UpdateKind::INVALID);

    return kind;
}

void set_value_from_update_specification(UpdateKind kind,
                                         const bsoncxx::document::view& update_specification,
                                         ostream& sql)
{
    switch (kind)
    {
    case UpdateKind::REPLACEMENT_DOCUMENT:
        {
            if (update_specification.length() > protocol::MAX_BSON_OBJECT_SIZE)
            {
                ostringstream ss;
                ss << "Document to upsert is larger than " << protocol::MAX_BSON_OBJECT_SIZE;
                throw SoftError(ss.str(), error::LOCATION17420);
            }

            auto json = bsoncxx::to_json(update_specification);
            json = escape_essential_chars(std::move(json));

            sql << "JSON_SET('" << json << "', '$._id', JSON_EXTRACT(id, '$'))";
        }
        break;

    case UpdateKind::UPDATE_OPERATORS:
        {
            // TODO: With update operators the correct behavior is not
            // TODO: obtained with protocol::MAX_BSON_OBJECT_SIZE, but
            // TODO: with slightly less.
            const int max_bson_object_size = 16777210;

            if (update_specification.length() > max_bson_object_size)
            {
                ostringstream ss;
                ss << "Document to upsert is larger than " << protocol::MAX_BSON_OBJECT_SIZE;
                throw SoftError(ss.str(), error::LOCATION17419);
            }

            sql << update_operator::convert(update_specification);
        }
        break;

    default:
        mxb_assert(!true);
    }
}

string extract_database(const string& collection)
{
    auto i = collection.find('.');

    if (i == string::npos)
    {
        return collection;
    }

    return collection.substr(0, i);
}

bool get_object_id(json_t* pObject, const char** pzOid, size_t* pLen)
{
    mxb_assert(json_typeof(pObject) == JSON_OBJECT);

    bool rv = false;

    if (json_object_size(pObject) == 1)
    {
        json_t* pOid = json_object_get(pObject, "$oid");

        if (pOid && json_typeof(pOid) == JSON_STRING)
        {
            *pzOid = json_string_value(pOid);
            *pLen = strlen(*pzOid);

            rv = true;
        }
    }

    return rv;
}

bool append_objectid(ArrayBuilder& array, json_t* pObject)
{
    bool rv = false;

    const char* zOid;
    size_t len;

    if (get_object_id(pObject, &zOid, &len))
    {
        try
        {
            // bxoncxx::oid would also take directly "zOid, len", but
            // with that constructor the conversion fails.
            array.append(bsoncxx::oid(string_view(zOid, len)));
            rv = true;
        }
        catch (const std::exception&)
        {
            // Ignored, this just means that the value of $oid was not valid.
        }
    }

    return rv;
}

bool append_objectid(DocumentBuilder& doc, const string_view& key, json_t* pObject)
{
    bool rv = false;

    const char* zOid;
    size_t len;

    if (get_object_id(pObject, &zOid, &len))
    {
        try
        {
            doc.append(kvp(key, bsoncxx::oid(string_view(zOid, len))));
            rv = true;
        }
        catch (const std::exception&)
        {
            // Ignored, this just means that the value of $oid was not valid.
        }
    }

    return rv;
}

}

namespace mariadb
{

const char* to_string(Op op)
{
    switch (op)
    {
    case Op::EQ:
        return "=";

    case Op::GT:
        return ">";

    case Op::GTE:
        return ">=";

    case Op::LT:
        return "<";

    case Op::LTE:
        return "<=";

    case Op::NE:
        return "!=";
    };

    mxb_assert(!true);
    return "unknown";
}

string get_account(string db, string user, const string& host)
{
    ostringstream ss;
    ss << "'"
       << get_user_name(std::move(db), std::move(user))
       << "'@'"
       << host
       << "'";

    return ss.str();
}

string get_user_name(std::string db, std::string user)
{
    ostringstream ss;

    if (db != "mariadb")
    {
        ss << nosql::escape_essential_chars(db)
           << ".";
    }

    ss << nosql::escape_essential_chars(user);

    return ss.str();
}

}

namespace nosql
{

//
// nosql::protocol
//
namespace protocol
{

namespace
{

const std::unordered_map<string, int32_t> alias_type_mapping =
{
    { alias::DOUBLE,           type::DOUBLE },
    { alias::STRING,           type::STRING },
    { alias::OBJECT,           type::OBJECT },
    { alias::ARRAY,            type::ARRAY },
    { alias::BIN_DATA,         type::BIN_DATA },
    { alias::UNDEFINED,        type::UNDEFINED },
    { alias::OBJECT_ID,        type::OBJECT_ID },
    { alias::BOOL,             type::BOOL },
    { alias::DATE,             type::DATE },
    { alias::NULL_ALIAS,       type::NULL_TYPE },
    { alias::REGEX,            type::REGEX },
    { alias::DB_POINTER,       type::DB_POINTER },
    { alias::JAVASCRIPT,       type::JAVASCRIPT },
    { alias::SYMBOL,           type::SYMBOL },
    { alias::JAVASCRIPT_SCOPE, type::JAVASCRIPT_SCOPE },
    { alias::INT32,            type::INT32 },
    { alias::TIMESTAMP,        type::TIMESTAMP },
    { alias::INT64,            type::INT64 },
    { alias::DECIMAL128,       type::DECIMAL128 },
    { alias::MIN_KEY,          type::MIN_KEY },
    { alias::MAX_KEY,          type::MAX_KEY },
};

}

namespace alias
{

const char* DOUBLE           = "double";
const char* STRING           = "string";
const char* OBJECT           = "object";
const char* ARRAY            = "array";
const char* BIN_DATA         = "binData";
const char* UNDEFINED        = "undefined";
const char* OBJECT_ID        = "objectId";
const char* BOOL             = "bool";
const char* DATE             = "date";
const char* NULL_ALIAS       = "date";
const char* REGEX            = "regex";
const char* DB_POINTER       = "dbPointer";
const char* JAVASCRIPT       = "javacript";
const char* SYMBOL           = "symbol";
const char* JAVASCRIPT_SCOPE = "javacriptWithScope";
const char* INT32            = "int";
const char* TIMESTAMP        = "timestamp";
const char* INT64            = "long";
const char* DECIMAL128       = "decimal";
const char* MIN_KEY          = "minKey";
const char* MAX_KEY          = "maxKey";

}

string type::to_alias(int32_t type)
{
    // Slow, but only needed during error reporting.

    for (const auto& kv : alias_type_mapping)
    {
        if (kv.second == type)
        {
            return kv.first;
        }
    }

    mxb_assert(!true);
    return "unknown";
}

int32_t alias::to_type(const string& alias)
{
    auto it = alias_type_mapping.find(alias);

    if (it == alias_type_mapping.end())
    {
        ostringstream ss;
        ss << "Unknown type name alias: " << alias;

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return it->second;
}

}

int32_t protocol::get_document(const uint8_t* pData, const uint8_t* pEnd, bsoncxx::document::view* pView)
{
    if (pEnd - pData < 4)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, expecting document, but not even document length received.";

        throw std::runtime_error(ss.str());
    }

    uint32_t size;
    get_byte4(pData, &size);

    if (pData + size > pEnd)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, document claimed to be " << size << " bytes, but only "
           << pEnd - pData << " available.";

        throw std::runtime_error(ss.str());
    }

    *pView = bsoncxx::document::view(pData, size);

    return size;
}

//
// nosql::packet
//
namespace packet
{

Insert::Insert(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_INSERT);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_zstring(pData, &m_zCollection);

    while (pData < m_pEnd)
    {
        if (m_pEnd - pData < 4)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, expecting document, but not even document length received.";

            throw std::runtime_error(ss.str());
        }

        uint32_t size;
        protocol::get_byte4(pData, &size);

        if (pData + size > m_pEnd)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, document claimed to be " << size << " bytes, but only "
               << m_pEnd - pData << " available.";

            throw std::runtime_error(ss.str());
        }

        auto view = bsoncxx::document::view { pData, size };
        m_documents.push_back(view);

        pData += size;
    }
}

Delete::Delete(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_DELETE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += 4; // ZERO int32
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_document(pData, m_pEnd, &m_selector);

    mxb_assert(pData == m_pEnd);
}

Update::Update(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_UPDATE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += 4; // ZERO int32
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_document(pData, m_pEnd, &m_selector);
    pData += protocol::get_document(pData, m_pEnd, &m_update);

    mxb_assert(pData == m_pEnd);
}

Query::Query(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_QUERY);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_nSkip);
    pData += protocol::get_byte4(pData, &m_nReturn);

    uint32_t size;
    protocol::get_byte4(pData, &size);
    m_query = bsoncxx::document::view { pData, size };
    pData += size;

    if (pData < m_pEnd)
    {
        protocol::get_byte4(pData, &size);
        if (m_pEnd - pData != size)
        {
            mxb_assert(!true);
            std::ostringstream ss;
            ss << "Malformed packet, expected " << size << " bytes for document, "
               << m_pEnd - pData << " found.";

            throw std::runtime_error(ss.str());
        }
        m_fields = bsoncxx::document::view { pData, size };
        pData += size;
    }

    if (pData != m_pEnd)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, " << m_pEnd - pData << " trailing bytes found.";

        throw std::runtime_error(ss.str());
    }
}

GetMore::GetMore(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_GET_MORE);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    int32_t zero;
    pData += protocol::get_byte4(pData, &zero);
    pData += protocol::get_zstring(pData, &m_zCollection);
    pData += protocol::get_byte4(pData, &m_nReturn);
    pData += protocol::get_byte8(pData, &m_cursor_id);

    if (m_nReturn == 0)
    {
        m_nReturn = DEFAULT_CURSOR_RETURN;
    }
}

KillCursors::KillCursors(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_KILL_CURSORS);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    int32_t zero;
    pData += protocol::get_byte4(pData, &zero);
    int32_t nCursors;
    pData += protocol::get_byte4(pData, &nCursors);

    for (int32_t i = 0; i < nCursors; ++i)
    {
        int64_t cursor_id;
        pData += protocol::get_byte8(pData, &cursor_id);
        m_cursor_ids.push_back(cursor_id);
    }
}

Msg::Msg(const Packet& packet)
    : Packet(packet)
{
    mxb_assert(opcode() == MONGOC_OPCODE_MSG);

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_pHeader) + sizeof(protocol::HEADER);

    pData += protocol::get_byte4(pData, &m_flags);

    if (checksum_present())
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(m_pHeader);

        uint32_t checksum = crc32_func(p, m_pHeader->msg_len - sizeof(uint32_t));

        p += (m_pHeader->msg_len - sizeof(uint32_t));
        const uint32_t* pChecksum = reinterpret_cast<const uint32_t*>(p);

        if (checksum != *pChecksum)
        {
            std::ostringstream ss;
            ss << "Invalid checksum, expected " << checksum << ", got " << *pChecksum << ".";
            throw std::runtime_error(ss.str());
        }
    }

    const uint8_t* pSections_end = m_pEnd - (checksum_present() ? sizeof(uint32_t) : 0);
    size_t sections_size = pSections_end - pData;

    while (pData < pSections_end)
    {
        uint8_t kind;
        pData += protocol::get_byte1(pData, &kind);

        switch (kind)
        {
        case 0:
            // Body section encoded as a single BSON object.
            {
                mxb_assert(m_document.empty());
                uint32_t size;
                protocol::get_byte4(pData, &size);

                if (pData + size > pSections_end)
                {
                    std::ostringstream ss;
                    ss << "Malformed packet, section(0) size " << size << " larger "
                       << "than available amount " << pSections_end - pData << " of data.";
                    throw std::runtime_error(ss.str());
                }

                m_document = bsoncxx::document::view { pData, size };
                pData += size;
            }
            break;

        case 1:
            {
                uint32_t total_size;
                protocol::get_byte4(pData, &total_size);

                if (pData + total_size > pSections_end)
                {
                    std::ostringstream ss;
                    ss << "Malformed packet, section(1) size " << total_size << " larger "
                       << "than available amount " << pSections_end - pData << " of data.";
                    throw std::runtime_error(ss.str());
                }

                auto* pEnd = pData + total_size;
                pData += 4;

                const char* zIdentifier = reinterpret_cast<const char*>(pData); // NULL-terminated
                while (*pData && pData != pEnd)
                {
                    ++pData;
                }

                if (pData != pEnd)
                {
                    ++pData; // NULL-terminator

                    auto& documents = m_arguments[zIdentifier];

                    // And now there are documents all the way down...
                    while (pData < pEnd)
                    {
                        uint32_t size;
                        protocol::get_byte4(pData, &size);
                        if (pData + size <= pEnd)
                        {
                            bsoncxx::document::view doc { pData, size };
                            documents.push_back(doc);
                            pData += size;
                        }
                        else
                        {
                            mxb_assert(!true);
                            std::ostringstream ss;
                            ss << "Malformed packet, expected " << size << " bytes for document, "
                               << pEnd - pData << " found.";
                            throw std::runtime_error(ss.str());
                        }
                    }
                }
                else
                {
                    mxb_assert(!true);
                    throw std::runtime_error("Malformed packet, 'identifier' not NULL-terminated.");
                }
            }
            break;

        default:
            {
                mxb_assert(!true);
                std::ostringstream ss;
                ss << "Malformed packet, expected a 'kind' of 0 or 1, received " << kind << ".";
                throw std::runtime_error(ss.str());
            }
        }
    }

    if (pData != pSections_end)
    {
        mxb_assert(!true);
        std::ostringstream ss;
        ss << "Malformed packet, " << pSections_end - pData << " trailing bytes found.";
        throw std::runtime_error(ss.str());
    }
}

}

//
// Path::Incarnation
//
string Path::Incarnation::get_comparison_condition(const bsoncxx::document::element& element) const
{
    string field = path();
    string condition;

    switch (element.type())
    {
    case bsoncxx::type::k_document:
        condition = get_comparison_condition(element.get_document());
        break;

    case bsoncxx::type::k_regex:
        condition = regex_to_condition(*this, element.get_regex());
        break;

    case bsoncxx::type::k_null:
        {
            if (has_array_demand())
            {
                condition = "(JSON_TYPE(JSON_QUERY(doc, '$." + m_array_path + "')) = 'ARRAY' AND ";
            }

            condition += "(JSON_EXTRACT(doc, '$." + field + "') IS NULL " +
                "OR (JSON_CONTAINS(JSON_QUERY(doc, '$." + field + "'), null) = 1) " +
                "OR (JSON_VALUE(doc, '$." + field + "') = 'null'))";

            if (has_array_demand())
            {
                condition += ")";
            }
        }
        break;

    case bsoncxx::type::k_date:
        condition = "(JSON_VALUE(doc, '$." + field + ".$date') = "
            + element_to_value(element, ValueFor::SQL) + ")";
        break;

    case bsoncxx::type::k_timestamp:
        condition = timestamp_to_condition(*this, element.get_timestamp());
        break;

    case bsoncxx::type::k_array:
        // TODO: This probably needs to be dealt with explicitly.
    default:
        {
            condition
                // Without the explicit check for NULL, this does not work when NOT due to $nor
                // is stashed in front of the whole thing.
                = "((JSON_QUERY(doc, '$." + field + "') IS NOT NULL"
                + " AND JSON_CONTAINS(JSON_QUERY(doc, '$." + field + "'), "
                + element_to_value(element, ValueFor::JSON) + ") = 1)"
                + " OR "
                + "(JSON_VALUE(doc, '$." + field + "') = "
                + element_to_value(element, ValueFor::SQL) + "))";
        }
    }

    return condition;
}

string Path::Incarnation::get_comparison_condition(const bsoncxx::document::view& doc) const
{
    string rv;

    // TODO: The fact that $regex and $options are not independent but used together,
    // TODO: means that, although that is handled here, it will, due to how things are
    // TODO: handled at an upper level lead to the same condition being generated twice.
    // TODO: It seems that all arguments should be investigated first, and only then should
    // TODO: SQL be generated.
    bool ignore_options = false;
    bool ignore_regex = false;

    auto it = doc.begin();
    auto end = doc.end();
    for (; it != end; ++it)
    {
        string condition;
        string separator;

        if (rv.empty())
        {
            rv += "(";
        }
        else
        {
            separator = " AND ";
        }

        const auto& element = *it;
        const auto nosql_op = static_cast<string>(element.key());

        auto jt = converters.find(nosql_op);

        if (jt != converters.end())
        {
            const auto mariadb_op = jt->second.mariadb_op;
            const auto& value_to_string = jt->second.value_to_string;

            condition = jt->second.field_and_value_to_comparison(*this, element, mariadb_op,
                                                                 nosql_op, value_to_string);
        }
        else if (nosql_op == "$nin")
        {
            condition = nin_to_condition(element);
        }
        else if (nosql_op == "$not")
        {
            condition = not_to_condition(element);
        }
        else if (nosql_op == "$elemMatch")
        {
            condition = elemMatch_to_condition(element);
        }
        else if (nosql_op == "$exists")
        {
            condition = exists_to_condition(element);
        }
        else if (nosql_op == "$size")
        {
            condition = "(JSON_LENGTH(doc, '$." + path() + "') = " +
                element_to_value(element, ValueFor::SQL, nosql_op) + ")";
        }
        else if (nosql_op == "$all")
        {
            condition = array_op_to_condition(element, ArrayOp::AND);
        }
        else if (nosql_op == "$in")
        {
            condition = array_op_to_condition(element, ArrayOp::OR);
        }
        else if (nosql_op == "$type")
        {
            condition = type_to_condition(element);
        }
        else if (nosql_op == "$mod")
        {
            condition = mod_to_condition(element);
        }
        else if (nosql_op == "$regex")
        {
            if (!ignore_regex)
            {
                bsoncxx::document::element options;

                auto jt = it;
                ++jt;
                while (jt != end)
                {
                    if (jt->key().compare("$options") == 0)
                    {
                        ignore_options = true;
                        options = *jt;
                        break;
                    }
                    ++jt;
                }

                condition = regex_to_condition(*this, element, options);
            }
        }
        else if (nosql_op == "$options")
        {
            if (!ignore_options)
            {
                bsoncxx::document::element regex;

                auto jt = it;
                ++jt;
                while (jt != end)
                {
                    if (jt->key().compare("$regex") == 0)
                    {
                        ignore_regex = true;
                        regex = *jt;
                        break;
                    }
                    ++jt;
                }

                condition = regex_to_condition(*this, regex, element);
            }
        }
        else if (nosql_op.front() == '$')
        {
            ostringstream ss;
            ss << "unknown operator: " << nosql_op;

            throw SoftError(ss.str(), error::BAD_VALUE);
        }
        else
        {
            break;
        }

        if (!condition.empty())
        {
            rv += separator + condition;
        }
    }

    if (it == end)
    {
        rv += ")";
    }
    else
    {
        // We are simply looking for an object.
        // TODO: Given two objects '{"a": [{"x": 1}]}' and '{"a": [{"x": 1, "y": 2}]}'
        // TODO: a query like '{"a": {x: 1}}' will return them both, although MongoDB
        // TODO: returns just the former.

        ostringstream ss;
        ss << "JSON_CONTAINS(JSON_QUERY(doc, '$." << path() << "'), JSON_OBJECT(";

        while (it != end)
        {
            auto element = *it;

            ss << "\"" << element.key() << "\", ";
            ss << element_to_value(element, ValueFor::JSON_NESTED);

            if (++it != end)
            {
                ss << ", ";
            }
        }

        ss << "))";

        rv = ss.str();
    }

    return rv;
}

string Path::Incarnation::array_op_to_condition(const bsoncxx::document::element& element,
                                                ArrayOp array_op) const
{
    const char* zDescription = to_description(array_op);

    if (element.type() != bsoncxx::type::k_array)
    {
        ostringstream ss;
        ss << zDescription << " needs an array";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    ostringstream ss;

    bsoncxx::array::view all_elements = element.get_array();

    if (all_elements.empty())
    {
        ss << "(true = false)";
    }
    else
    {
        // TODO: We have this information higher up already.
        string field = path();
        auto i = field.find_last_of('.');
        bool is_scoped = (i != string::npos);

        ss << "(";

        if (array_op == ArrayOp::AND)
        {
            if (is_scoped)
            {
                string path;
                path = field.substr(0, i);
                path += "[*].";
                path += field.substr(i + 1);

                ss << "(";
                bool add_or = false;
                for (auto f : { field, path })
                {
                    if (add_or)
                    {
                        ss << " OR ";
                    }
                    else
                    {
                        add_or = true;
                    }

                    add_element_array(ss, is_scoped, f, zDescription, all_elements);
                };
                ss << ")";
            }
            else
            {
                add_element_array(ss, is_scoped, field, zDescription, all_elements);
            }
        }
        else
        {
            mxb_assert(array_op == ArrayOp::OR);

            ss << "(";

            bool first_element = true;
            for (const auto& one_element : all_elements)
            {
                if (first_element)
                {
                    first_element = false;
                }
                else
                {
                    ss << " OR ";
                }

                auto type = one_element.type();

                switch (type)
                {
                case bsoncxx::type::k_null:
                    ss << "(JSON_EXTRACT(doc, '$." << field << "') IS NULL)";
                    break;

                case bsoncxx::type::k_regex:
                    ss << "(false)";
                    break;

                default:
                    {
                        if (is_scoped)
                        {
                            string path;
                            path = field.substr(0, i);
                            path += "[*].";
                            path += field.substr(i + 1);

                            ss << "(";
                            bool add_or = false;
                            for (auto p : { field, path })
                            {
                                if (add_or)
                                {
                                    ss << " OR ";
                                }
                                else
                                {
                                    add_or = true;
                                }

                                if (one_element.type() != bsoncxx::type::k_regex)
                                {
                                    ss << "(JSON_CONTAINS(";
                                    ss << "JSON_EXTRACT(doc, '$." << p << "'), JSON_ARRAY("
                                       << element_to_value(one_element, ValueFor::JSON_NESTED, zDescription)
                                       << ")) = 1)";
                                }
                                else
                                {
                                    ss << "false";
                                }

                                if (one_element.type() != bsoncxx::type::k_document)
                                {
                                    ss << " OR (JSON_VALUE(doc, '$." << p << "') = "
                                       << element_to_value(one_element, ValueFor::SQL, zDescription)
                                       << ")";
                                }
                            }
                            ss << ")";
                        }
                        else
                        {
                            ss << "(JSON_CONTAINS(doc, JSON_ARRAY("
                               << element_to_value(one_element, ValueFor::JSON_NESTED, zDescription)
                               << "), '$." << field << "') = 1)";

                            if (one_element.type() != bsoncxx::type::k_document)
                            {
                                ss << " OR (JSON_VALUE(doc, '$." << field << "') = "
                                   << element_to_value(one_element, ValueFor::SQL, zDescription)
                                   << ")";
                            }
                        }
                    }
                }
            }

            ss << ")";
        }

        ss << ")";
    }

    return ss.str();
}

string Path::Incarnation::nin_to_condition(const bsoncxx::document::element& element) const
{
    string condition;

    if (element.type() != bsoncxx::type::k_array)
    {
        throw SoftError("$nin needs an array", error::BAD_VALUE);
    }

    vector<string> values;

    bsoncxx::array::view array = element.get_array();

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        const auto& array_element = *it;

        string value = element_to_value(array_element, ValueFor::SQL, "$nin");
        mxb_assert(!value.empty());

        values.push_back(value);
    }

    if (!values.empty())
    {
        string s = "(" + mxb::join(values) + ")";

        condition = "(JSON_EXTRACT(doc, '$." + path() + "') IS NULL OR "
            + "JSON_EXTRACT(doc, '$." + path() + "') NOT IN " + s + ")";
    }
    else
    {
        condition = "(true)";
    }

    return condition;

}

string Path::Incarnation::not_to_condition(const bsoncxx::document::element& element) const
{
    string condition;

    auto type = element.type();

    if (type != bsoncxx::type::k_document && type != bsoncxx::type::k_regex)
    {
        ostringstream ss;
        ss << "$not needs a document or a regex";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    bsoncxx::document::view doc;

    if (type == bsoncxx::type::k_document)
    {
        doc = element.get_document();

        if (doc.begin() == doc.end())
        {
            throw SoftError("$not cannot be empty", error::BAD_VALUE);
        }
    }

    condition += "(NOT ";

    if (type == bsoncxx::type::k_document)
    {
        condition += get_comparison_condition(doc);
    }
    else
    {
        condition += regex_to_condition(*this, element.get_regex());
    }

    condition += ")";

    return condition;
}

string Path::Incarnation::elemMatch_to_condition(const bsoncxx::document::element& element) const
{
    string condition;

    if (element.type() != bsoncxx::type::k_document)
    {
        throw SoftError("$elemMatch needs an Object", error::BAD_VALUE);
    }

    bsoncxx::document::view doc = element.get_document();

    if (doc.empty())
    {
        condition = "true";
    }
    else
    {
        condition = elemMatch_to_json_contains(*this, doc);
    }

    return condition;
}

string Path::Incarnation::exists_to_condition(const bsoncxx::document::element& element) const
{
    string rv("(");

    bool b = nosql::element_as<bool>("?", "$exists", element, nosql::Conversion::RELAXED);

    if (b)
    {
        rv += "JSON_EXTRACT(doc, '$." + path() + "') IS NOT NULL";
    }
    else
    {
        bool close = false;
        if (!has_array_demand())
        {
            if (has_parent())
            {
                rv += "JSON_QUERY(doc, '$." + parent_path() + "') IS NULL OR "
                    "(JSON_TYPE(JSON_EXTRACT(doc, '$." + parent_path() + "')) = 'OBJECT'"
                    " AND ";
                close = true;
            }
        }
        else
        {
            rv += "JSON_TYPE(JSON_QUERY(doc, '$." + array_path() + "')) = 'ARRAY' AND ";
        }

        rv += "JSON_EXTRACT(doc, '$." + path() + "') IS NULL";

        if (close)
        {
            rv += ")";
        }
    }

    rv += ")";

    return rv;
}

string Path::Incarnation::mod_to_condition(const bsoncxx::document::element& element) const
{
    if (element.type() != bsoncxx::type::k_array)
    {
        throw SoftError("malformed mod, needs to be an array", error::BAD_VALUE);
    }

    bsoncxx::array::view arguments = element.get_array();

    auto n = std::distance(arguments.begin(), arguments.end());

    const char* zMessage = nullptr;
    switch (n)
    {
    case 0:
    case 1:
        zMessage = "malformed mod, not enough elements";
        break;

    case 2:
        break;

    default:
        zMessage = "malformed mod, too many elements";
    }

    if (zMessage)
    {
        throw SoftError(zMessage, error::BAD_VALUE);
    }

    int64_t divisor;
    if (!get_number_as_integer(arguments[0], &divisor))
    {
        throw SoftError("malformed mod, divisor is not a number", error::BAD_VALUE);
    }

    if (divisor == 0)
    {
        throw SoftError("divisor cannot be 0", error::BAD_VALUE);
    }

    int64_t remainder;
    if (!get_number_as_integer(arguments[1], &remainder))
    {
        throw SoftError("malformed mod, remainder is not a number", error::BAD_VALUE);
    }

    ostringstream ss;
    ss << "((JSON_TYPE(JSON_VALUE(doc, '$." << path() << "')) = 'INTEGER' || "
       << "JSON_TYPE(JSON_VALUE(doc, '$." << path() << "')) = 'DOUBLE') AND "
       << "(MOD(JSON_VALUE(doc, '$." << path() << "'), " << divisor << ") = " << remainder << "))";

    return ss.str();
}

string Path::Incarnation::type_to_condition(const bsoncxx::document::element& element) const
{
    string rv;

    if (element.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view all_elements = element.get_array();

        if (all_elements.empty())
        {
            // Yes, this is what MongoDB returns.
            throw SoftError("a must match at least one type", error::FAILED_TO_PARSE);
        }

        ostringstream ss;
        ss << "(";

        bool first = true;
        for (const auto& one_element : all_elements)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                ss << " OR ";
            }

            ss << type_to_condition_from_value(*this, one_element);
        }

        ss << ")";

        rv = ss.str();
    }
    else
    {
        rv = type_to_condition_from_value(*this, element);
    }

    return rv;
}

//
// Path::Part
//
string Path::Part::name() const
{
    string rv;

    switch (m_kind)
    {
    case Part::ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case Part::ARRAY:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case INDEXED_ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path();
        }
        rv += "[" + m_name + "]";
        break;
    }

    return rv;
}

string Path::Part::path() const
{
    string rv;

    switch (m_kind)
    {
    case Part::ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name;
        break;

    case Part::ARRAY:
        if (m_pParent)
        {
            rv = m_pParent->path() + ".";
        }
        rv += m_name + "[*]";
        break;

    case INDEXED_ELEMENT:
        if (m_pParent)
        {
            rv = m_pParent->path();
        }
        rv += "[" + m_name + "]";
        break;
    }

    return rv;
}

//static
vector<Path::Part*> Path::Part::get_leafs(const string& path, vector<std::unique_ptr<Part>>& parts)
{
    string::size_type i = 0;
    string::size_type j = path.find_first_of('.', i);

    vector<Part*> leafs;

    while (j != string::npos)
    {
        string part = path.substr(i, j - i);

        i = j + 1;
        j = path.find_first_of('.', i);

        add_part(part, false, leafs, parts);
    }

    add_part(path.substr(i, j), true, leafs, parts);

    return leafs;
}

//static
void Path::Part::add_leaf(const string& part,
                          bool last,
                          bool is_number,
                          Part* pParent,
                          vector<Part*>& leafs,
                          vector<std::unique_ptr<Part>>& parts)
{
    parts.push_back(std::make_unique<Part>(Part::ELEMENT, part, pParent));
    leafs.push_back(parts.back().get());

    if (!last)
    {
        parts.push_back(std::make_unique<Part>(Part::ARRAY, part, pParent));
        leafs.push_back(parts.back().get());
    }

    if (is_number && pParent && pParent->is_element())
    {
        parts.push_back(std::make_unique<Part>(Part::INDEXED_ELEMENT, part, pParent));
        leafs.push_back(parts.back().get());
    }
}

//static
void Path::Part::add_part(const string& part,
                          bool last,
                          vector<Part*>& leafs,
                          vector<std::unique_ptr<Part>>& parts)
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

    vector<Part*> tmp;

    if (leafs.empty())
    {
        add_leaf(part, last, is_number, nullptr, tmp, parts);
    }
    else
    {
        for (auto& pLeaf : leafs)
        {
            add_leaf(part, last, is_number, pLeaf, tmp, parts);
        }
    }

    tmp.swap(leafs);
}

//
// Path
//
Path::Path(const bsoncxx::document::element& element)
    : m_element(element)
    , m_paths(get_incarnations(static_cast<string>(element.key())))
{
}

// https://docs.mongodb.com/manual/reference/operator/query/#comparison
string Path::get_comparison_condition() const
{
    string condition;

    if (m_element.type() == bsoncxx::type::k_document)
    {
        condition = get_document_condition(m_element.get_document());
    }
    else
    {
        condition = get_element_condition(m_element);
    }

    return condition;
}

//static
std::vector<Path::Incarnation> Path::get_incarnations(const std::string& path)
{
    vector<Incarnation> rv;
    vector<std::unique_ptr<Part>> parts;
    vector<Part*> leafs = Part::get_leafs(path, parts);

    for (const Part* pLeaf : leafs)
    {
        string path = pLeaf->path();
        Part* pParent = pLeaf->parent();

        string parent_path;
        string array_path;

        if (pParent)
        {
            parent_path = pParent->name();

            while (pLeaf && array_path.empty())
            {
                if (pLeaf->is_indexed_element() || (pParent && pParent->is_array()))
                {
                    array_path = pParent->name();
                }
                else if (pLeaf->is_element() && (pParent && pParent->is_indexed_element()))
                {
                    auto* pGramps = pParent->parent();
                    if (pGramps)
                    {
                        array_path = pGramps->name();
                    }
                }

                pLeaf = pParent;
                if (pParent)
                {
                    pParent = pParent->parent();
                }
            }
        }

        rv.push_back(Incarnation(std::move(path), std::move(parent_path), std::move(array_path)));
    }

    return rv;
}

string Path::get_element_condition(const bsoncxx::document::element& element) const
{
    string condition;

    if (m_paths.size() > 1)
    {
        condition += "(";
    }

    bool first = true;
    for (const auto& p : m_paths)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            condition += " OR ";
        }

        condition += "(" + p.get_comparison_condition(m_element) + ")";
    }

    if (m_paths.size() > 1)
    {
        condition += ")";
    }

    return condition;
}

string Path::get_document_condition(const bsoncxx::document::view& doc) const
{
    string condition;

    auto it = doc.begin();
    auto end = doc.end();

    if (it == end)
    {
        bool first = true;
        for (const auto& p : m_paths)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                condition += " OR ";
            }

            condition += "(JSON_EXTRACT(doc, '$." + p.path() + "') = JSON_OBJECT() OR ";
            condition += "(JSON_TYPE(JSON_EXTRACT(doc, '$." + p.path() + "')) = 'ARRAY' AND ";
            condition += "JSON_CONTAINS(JSON_EXTRACT(doc, '$." + p.path() + "'), JSON_OBJECT())))";
        }
    }
    else
    {
        for (; it != end; ++it)
        {
            auto element = *it;

            if (!condition.empty())
            {
                condition += " AND ";
            }

            const auto nosql_op = static_cast<string>(element.key());

            if (nosql_op == "$not")
            {
                condition += not_to_condition(element);
            }
            else
            {
                condition += get_element_condition(element);
            }
        }
    }

    return "(" + condition + ")";
}

string Path::not_to_condition(const bsoncxx::document::element& element) const
{
    string condition;

    auto type = element.type();

    if (type != bsoncxx::type::k_document && type != bsoncxx::type::k_regex)
    {
        ostringstream ss;
        ss << "$not needs a document or a regex";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    bsoncxx::document::view doc;

    if (type == bsoncxx::type::k_document)
    {
        doc = element.get_document();

        if (doc.begin() == doc.end())
        {
            throw SoftError("$not cannot be empty", error::BAD_VALUE);
        }
    }

    condition += "(NOT ";

    if (m_paths.size() > 1)
    {
        condition += "(";
    }

    bool first = true;
    for (const auto& p : m_paths)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            condition += " OR ";
        }

        if (type == bsoncxx::type::k_document)
        {
            condition += "(" + p.get_comparison_condition(doc) + ")";
        }
        else
        {
            condition += "(" + regex_to_condition(p, element.get_regex()) + ")";
        }
    }

    if (m_paths.size() > 1)
    {
        condition += ")";
    }

    condition += ")";

    return condition;
}

//
// NoSQL
//
NoSQL::NoSQL(MXS_SESSION* pSession,
             ClientConnection* pClient_connection,
             mxs::Component* pDownstream,
             Config* pConfig,
             UserManager* pUm)
    : m_context(pUm, pSession, pClient_connection, pDownstream)
    , m_config(*pConfig)
{
}

NoSQL::~NoSQL()
{
}

State NoSQL::handle_request(GWBUF* pRequest, GWBUF** ppResponse)
{
    State state = State::READY;
    GWBUF* pResponse = nullptr;

    if (!m_sDatabase)
    {
        try
        {
            // If no database operation is in progress, we proceed.
            packet::Packet req(pRequest);

            mxb_assert(req.msg_len() == (int)gwbuf_length(pRequest));

            switch (req.opcode())
            {
            case MONGOC_OPCODE_COMPRESSED:
            case MONGOC_OPCODE_REPLY:
                {
                    ostringstream ss;
                    ss << "Unsupported packet " << nosql::opcode_to_string(req.opcode()) << " received.";
                    throw std::runtime_error(ss.str());
                }
                break;

            case MONGOC_OPCODE_GET_MORE:
                state = handle_get_more(pRequest, packet::GetMore(req), &pResponse);
                break;

            case MONGOC_OPCODE_KILL_CURSORS:
                state = handle_kill_cursors(pRequest, packet::KillCursors(req), &pResponse);
                break;

            case MONGOC_OPCODE_DELETE:
                state = handle_delete(pRequest, packet::Delete(req), &pResponse);
                break;

            case MONGOC_OPCODE_INSERT:
                state = handle_insert(pRequest, packet::Insert(req), &pResponse);
                break;

            case MONGOC_OPCODE_MSG:
                state = handle_msg(pRequest, packet::Msg(req), &pResponse);
                break;

            case MONGOC_OPCODE_QUERY:
                state = handle_query(pRequest, packet::Query(req), &pResponse);
                break;

            case MONGOC_OPCODE_UPDATE:
                state = handle_update(pRequest, packet::Update(req), &pResponse);
                break;

            default:
                {
                    mxb_assert(!true);
                    ostringstream ss;
                    ss << "Unknown packet " << req.opcode() << " received.";
                    throw std::runtime_error(ss.str());
                }
            }
        }
        catch (const std::exception& x)
        {
            MXB_ERROR("Closing client connection: %s", x.what());
            kill_client();
        }

        gwbuf_free(pRequest);
    }
    else
    {
        // Otherwise we push it on the request queue.
        m_requests.push_back(pRequest);
    }

    *ppResponse = pResponse;
    return state;
}

int32_t NoSQL::clientReply(GWBUF* pMariadb_response, DCB* pDcb)
{
    mxb_assert(m_sDatabase.get());

    // TODO: Remove need for making resultset contiguous.
    pMariadb_response = gwbuf_make_contiguous(pMariadb_response);

    mxs::Buffer mariadb_response(pMariadb_response);
    GWBUF* pProtocol_response = m_sDatabase->translate(std::move(mariadb_response));

    if (m_sDatabase->is_ready())
    {
        m_sDatabase.reset();

        if (pProtocol_response)
        {
            pDcb->writeq_append(pProtocol_response);
        }

        if (!m_requests.empty())
        {
            // Loop as long as responses to requests can be generated immediately.
            // If it can't then we'll continue once clientReply() is called anew.
            State state = State::READY;
            do
            {
                mxb_assert(!m_sDatabase.get());

                GWBUF* pRequest = m_requests.front();
                m_requests.pop_front();

                state = handle_request(pRequest, &pProtocol_response);

                if (pProtocol_response)
                {
                    // The response could be generated immediately, just send it.
                    pDcb->writeq_append(pProtocol_response);
                }
            }
            while (state == State::READY && !m_requests.empty());
        }
    }
    else
    {
        // If the database is not ready, there cannot be a response.
        mxb_assert(pProtocol_response == nullptr);
    }

    return 0;
}

void NoSQL::kill_client()
{
    m_context.client_connection().dcb()->session()->kill();
}

State NoSQL::handle_delete(GWBUF* pRequest, packet::Delete&& req, GWBUF** ppResponse)
{
    log_in("Request(Delete)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_delete(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_insert(GWBUF* pRequest, packet::Insert&& req, GWBUF** ppResponse)
{
    log_in("Request(Insert)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_insert(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_update(GWBUF* pRequest, packet::Update&& req, GWBUF** ppResponse)
{
    log_in("Request(Update)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_update(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_query(GWBUF* pRequest, packet::Query&& req, GWBUF** ppResponse)
{
    log_in("Request(Query)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_query(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_get_more(GWBUF* pRequest, packet::GetMore&& req, GWBUF** ppResponse)
{
    log_in("Request(GetMore)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create(extract_database(req.collection()), &m_context, &m_config));

    State state = m_sDatabase->handle_get_more(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_kill_cursors(GWBUF* pRequest, packet::KillCursors&& req, GWBUF** ppResponse)
{
    log_in("Request(KillCursors)", req);

    mxb_assert(!m_sDatabase.get());
    m_sDatabase = std::move(Database::create("admin", &m_context, &m_config));

    State state = m_sDatabase->handle_kill_cursors(pRequest, std::move(req), ppResponse);

    if (state == State::READY)
    {
        m_sDatabase.reset();
    }

    return state;
}

State NoSQL::handle_msg(GWBUF* pRequest, packet::Msg&& req, GWBUF** ppResponse)
{
    log_in("Request(Msg)", req);

    State state = State::READY;

    const auto& doc = req.document();

    auto element = doc["$db"];

    if (element)
    {
        if (element.type() == bsoncxx::type::k_utf8)
        {
            auto utf8 = element.get_utf8();

            string name(utf8.value.data(), utf8.value.size());

            mxb_assert(!m_sDatabase.get());
            m_sDatabase = std::move(Database::create(name, &m_context, &m_config));

            state = m_sDatabase->handle_msg(pRequest, std::move(req), ppResponse);

            if (state == State::READY)
            {
                m_sDatabase.reset();
            }
        }
        else
        {
            MXB_ERROR("Closing client connection; key '$db' found, but value is not utf8.");
            kill_client();
        }
    }
    else
    {
        MXB_ERROR("Closing client connection; document did not "
                  "contain the expected key '$db': %s",
                  req.to_string().c_str());
        kill_client();
    }

    return state;
}

}

//
// nosql free functions
//
void nosql::append(DocumentBuilder& doc,
                   const string_view& key,
                   const bsoncxx::document::element& element)
{
    auto i = key.find('.');

    if (i != string::npos)
    {
        auto head = key.substr(0, i);
        auto tail = key.substr(i + 1);

        DocumentBuilder child;
        append(child, tail, element);

        doc.append(kvp(head, child.extract()));
    }
    else
    {
        switch (element.type())
        {
        case bsoncxx::type::k_array:
            doc.append(kvp(key, element.get_array()));
            break;

        case bsoncxx::type::k_binary:
            doc.append(kvp(key, element.get_binary()));
            break;

        case bsoncxx::type::k_bool:
            doc.append(kvp(key, element.get_bool()));
            break;

        case bsoncxx::type::k_code:
            doc.append(kvp(key, element.get_code()));
            break;

        case bsoncxx::type::k_codewscope:
            doc.append(kvp(key, element.get_codewscope()));
            break;

        case bsoncxx::type::k_date:
            doc.append(kvp(key, element.get_date()));
            break;

        case bsoncxx::type::k_dbpointer:
            doc.append(kvp(key, element.get_dbpointer()));
            break;

        case bsoncxx::type::k_decimal128:
            doc.append(kvp(key, element.get_decimal128()));
            break;

        case bsoncxx::type::k_document:
            doc.append(kvp(key, element.get_document()));
            break;

        case bsoncxx::type::k_double:
            doc.append(kvp(key, element.get_double()));
            break;

        case bsoncxx::type::k_int32:
            doc.append(kvp(key, element.get_int32()));
            break;

        case bsoncxx::type::k_int64:
            doc.append(kvp(key, element.get_int64()));
            break;

        case bsoncxx::type::k_maxkey:
            doc.append(kvp(key, element.get_maxkey()));
            break;

        case bsoncxx::type::k_minkey:
            doc.append(kvp(key, element.get_minkey()));
            break;

        case bsoncxx::type::k_null:
            doc.append(kvp(key, element.get_null()));
            break;

        case bsoncxx::type::k_oid:
            doc.append(kvp(key, element.get_oid()));
            break;

        case bsoncxx::type::k_regex:
            doc.append(kvp(key, element.get_regex()));
            break;

        case bsoncxx::type::k_symbol:
            doc.append(kvp(key, element.get_symbol()));
            break;

        case bsoncxx::type::k_timestamp:
            doc.append(kvp(key, element.get_timestamp()));
            break;

        case bsoncxx::type::k_undefined:
            doc.append(kvp(key, element.get_undefined()));
            break;

        case bsoncxx::type::k_utf8:
            doc.append(kvp(key, element.get_utf8()));
            break;
        }
    }
}

const char* nosql::opcode_to_string(int code)
{
    switch (code)
    {
    case MONGOC_OPCODE_REPLY:
        return "MONGOC_OPCODE_REPLY";

    case MONGOC_OPCODE_UPDATE:
        return "MONGOC_OPCODE_UPDATE";

    case MONGOC_OPCODE_INSERT:
        return "MONGOC_OPCODE_INSERT";

    case MONGOC_OPCODE_QUERY:
        return "MONGOC_OPCODE_QUERY";

    case MONGOC_OPCODE_GET_MORE:
        return "MONGOC_OPCODE_GET_MORE";

    case MONGOC_OPCODE_DELETE:
        return "MONGOC_OPCODE_DELETE";

    case MONGOC_OPCODE_KILL_CURSORS:
        return "MONGOC_OPCODE_KILL_CURSORS";

    case MONGOC_OPCODE_COMPRESSED:
        return "MONGOC_OPCODE_COMPRESSED";

    case MONGOC_OPCODE_MSG:
        return "MONGOC_OPCODE_MSG";

    default:
        mxb_assert(!true);
        return "MONGOC_OPCODE_UNKNOWN";
    }
}

vector<string> nosql::extractions_from_projection(const bsoncxx::document::view& projection)
{
    vector<string> extractions;

    auto it = projection.begin();
    auto end = projection.end();

    if (it != end)
    {
        bool id_seen = false;

        for (; it != end; ++it)
        {
            const auto& element = *it;
            const auto& key = element.key();

            if (key.size() == 0)
            {
                continue;
            }

            if (key.compare("_id") == 0)
            {
                id_seen = true;

                bool include_id = false;

                switch (element.type())
                {
                case bsoncxx::type::k_int32:
                    include_id = static_cast<int32_t>(element.get_int32());
                    break;

                case bsoncxx::type::k_int64:
                    include_id = static_cast<int64_t>(element.get_int64());
                    break;

                case bsoncxx::type::k_bool:
                    include_id = static_cast<bool>(element.get_bool());
                    break;

                case bsoncxx::type::k_double:
                    include_id = static_cast<double>(element.get_double());
                    break;

                default:
                    ;
                }

                if (!include_id)
                {
                    continue;
                }
            }

            auto extraction = escape_essential_chars(static_cast<string>(key));

            extractions.push_back(static_cast<string>(key));
        }

        if (!id_seen)
        {
            // _id was not specifically mentioned, so it must be added, but it
            // must be added to the front.
            vector<string> e;
            e.push_back("_id");

            copy(extractions.begin(), extractions.end(), std::back_inserter(e));

            extractions.swap(e);
        }
    }

    return extractions;
}

string nosql::columns_from_extractions(const vector<string>& extractions)
{
    string columns;

    if (!extractions.empty())
    {
        for (auto extraction : extractions)
        {
            if (!columns.empty())
            {
                columns += ", ";
            }

            columns += "JSON_EXTRACT(doc, '$." + extraction + "')";
        }
    }
    else
    {
        columns = "doc";
    }

    return columns;
}

string nosql::to_string(const bsoncxx::document::element& element)
{
    return element_to_string(element);
}

string nosql::where_condition_from_query(const bsoncxx::document::view& query)
{
    string condition = get_condition(query);

    if (condition.empty())
    {
        condition = "true";
    }

    return condition;
}

string nosql::where_clause_from_query(const bsoncxx::document::view& query)
{
    return "WHERE " + where_condition_from_query(query);
}


// https://docs.mongodb.com/manual/reference/method/cursor.sort/
string nosql::order_by_value_from_sort(const bsoncxx::document::view& sort)
{
    string order_by;

    for (auto it = sort.begin(); it != sort.end(); ++it)
    {
        const auto& element = *it;
        const auto& key = element.key();

        if (key.size() == 0)
        {
            throw nosql::SoftError("FieldPath cannot be constructed with empty string",
                                   nosql::error::LOCATION40352);
        }

        int64_t value = 0;

        if (!nosql::get_number_as_integer(element, &value))
        {
            ostringstream ss;
            // TODO: Should actually be the value itself, and not its type.
            ss << "Illegal key in $sort specification: "
               << element.key() << ": " << bsoncxx::to_string(element.type());

            throw nosql::SoftError(ss.str(), nosql::error::LOCATION15974);
        }

        if (value != 1 && value != -1)
        {
            throw nosql::SoftError("$sort key ordering must be 1 (for ascending) or -1 (for descending)",
                                   nosql::error::LOCATION15975);
        }

        if (!order_by.empty())
        {
            order_by += ", ";
        }

        order_by += "JSON_EXTRACT(doc, '$." + static_cast<string>(element.key()) + "')";

        if (value == -1)
        {
            order_by += " DESC";
        }
    }

    return order_by;
}

string nosql::set_value_from_update_specification(const bsoncxx::document::view& update_command,
                                                  const bsoncxx::document::element& update_specification)
{
    ostringstream sql;

    auto kind = get_update_kind(update_specification);

    switch (kind)
    {
    case UpdateKind::AGGREGATION_PIPELINE:
        {
            string message("Aggregation pipeline not supported: '");
            message += bsoncxx::to_json(update_command);
            message += "'.";

            MXB_ERROR("%s", message.c_str());
            throw HardError(message, error::COMMAND_FAILED);
        }
        break;

    default:
        set_value_from_update_specification(kind, update_specification.get_document(), sql);
    }

    return sql.str();
}

string nosql::set_value_from_update_specification(const bsoncxx::document::view& update_specification)
{
    ostringstream sql;

    auto kind = get_update_kind(update_specification);

    set_value_from_update_specification(kind, update_specification, sql);

    return sql.str();
}

bool nosql::get_integer(const bsoncxx::document::element& element, int64_t* pInt)
{
    bool rv = true;

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        *pInt = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pInt = element.get_int64();
        break;

    default:
        rv = false;
    }

    return rv;
}

bool nosql::get_number_as_double(const bsoncxx::document::element& element, double_t* pDouble)
{
    bool rv = true;

    switch (element.type())
    {
    case bsoncxx::type::k_int32:
        *pDouble = element.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pDouble = element.get_int64();
        break;

    case bsoncxx::type::k_double:
        *pDouble = element.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}

string nosql::table_create_statement(const std::string& table_name, int64_t id_length, bool if_not_exists)
{
    ostringstream ss;
    ss << "CREATE TABLE ";

    if (if_not_exists)
    {
        ss << "IF NOT EXISTS ";
    }

    ss << table_name << " ("
       << "id VARCHAR(" << id_length << ") "
       << "AS (JSON_COMPACT(JSON_EXTRACT(doc, \"$._id\"))) UNIQUE KEY, "
       << "doc JSON, "
       << "CONSTRAINT id_not_null CHECK(id IS NOT NULL))";

    return ss.str();
}

bsoncxx::array::value nosql::bson_from_json_array(json_t* pArray)
{
    mxb_assert(json_typeof(pArray) == JSON_ARRAY);

    ArrayBuilder array;

    size_t index;
    json_t* pValue;
    json_array_foreach(pArray, index, pValue)
    {
        switch (json_typeof(pValue))
        {
        case JSON_OBJECT:
            if (!append_objectid(array, pValue))
            {
                array.append(bson_from_json(pValue));
            }
            break;

        case JSON_ARRAY:
            array.append(bson_from_json_array(pValue));
            break;

        case JSON_STRING:
            array.append(json_string_value(pValue));
            break;

        case JSON_INTEGER:
            array.append((int64_t)json_integer_value(pValue));
            break;

        case JSON_REAL:
            array.append(json_number_value(pValue));
            break;

        case JSON_TRUE:
            array.append(true);
            break;

        case JSON_FALSE:
            array.append(false);
            break;

        case JSON_NULL:
            array.append(bsoncxx::types::b_null());
            break;
        }
    }

    return array.extract();
}

bsoncxx::document::value nosql::bson_from_json(json_t* pObject)
{
    mxb_assert(json_typeof(pObject) == JSON_OBJECT);

    DocumentBuilder doc;

    const char* zKey;
    json_t* pValue;
    json_object_foreach(pObject, zKey, pValue)
    {
        string_view key(zKey);

        switch (json_typeof(pValue))
        {
        case JSON_OBJECT:
            if (!append_objectid(doc, key, pValue))
            {
                doc.append(kvp(key, bson_from_json(pValue)));
            }
            break;

        case JSON_ARRAY:
            doc.append(kvp(key, bson_from_json_array(pValue)));
            break;

        case JSON_STRING:
            doc.append(kvp(key, json_string_value(pValue)));
            break;

        case JSON_INTEGER:
            doc.append(kvp(key, (int64_t)json_integer_value(pValue)));
            break;

        case JSON_REAL:
            doc.append(kvp(key, json_number_value(pValue)));
            break;

        case JSON_TRUE:
            doc.append(kvp(key, true));
            break;

        case JSON_FALSE:
            doc.append(kvp(key, false));
            break;

        case JSON_NULL:
            doc.append(kvp(key, bsoncxx::types::b_null()));
            break;
        }
    }

    return doc.extract();
}

bsoncxx::document::value nosql::bson_from_json(const string& json)
{
    // A bsoncxx::document::value cannot be default constructed, so we just have
    // to return from many places.

    try
    {
        return bsoncxx::from_json(json);
    }
    catch (const std::exception& x)
    {
        MXB_WARNING("Could not default convert JSON to BSON: %s. JSON: %s",
                    x.what(), json.c_str());
    }

    // Ok, so the default JSON->BSON conversion failed. Probably due to there being JSON
    // sub-object that "by convention" should be converted into a particular BSON object,
    // but cannot be due to it not containing everything that is needed.

    mxb::Json j;

    if (j.load_string(json))
    {
        return bson_from_json(j.get_json());
    }
    else
    {
        MXB_ERROR("Could not load JSON data, returning empty document: %s. JSON: %s",
                  j.error_msg().c_str(), json.c_str());
    }

    DocumentBuilder doc;
    return doc.extract();
}

namespace
{

void add_value(json_t* pParent, const string& key, const string& value)
{
    json_error_t error;
    json_t* pItem = json_loadb(value.data(), value.length(), JSON_DECODE_ANY, &error);

    if (pItem)
    {
        json_object_set_new(pParent, key.c_str(), pItem);
    }
    else
    {
        MXS_ERROR("Could not decode JSON value '%s': %s", value.c_str(), error.text);
    }
}

void create_entry(json_t* pRoot, const string& extraction, const std::string& value)
{
    string key = extraction;
    json_t* pParent = pRoot;

    string::size_type i;

    while ((i = key.find('.')) != string::npos)
    {
        auto child = key.substr(0, i);
        key = key.substr(i + 1);

        json_t* pChild = json_object_get(pParent, child.c_str());

        if (!pChild)
        {
            pChild = json_object();
            json_object_set_new(pParent, child.c_str(), pChild);
        }

        pParent = pChild;
    }

    add_value(pParent, key, value);
}

}

std::string nosql::resultset_row_to_json(const CQRTextResultsetRow& row,
                                         const std::vector<std::string>& extractions)
{
    return resultset_row_to_json(row, row.begin(), extractions);
}

std::string nosql::resultset_row_to_json(const CQRTextResultsetRow& row,
                                         CQRTextResultsetRow::iterator it,
                                         const std::vector<std::string>& extractions)
{
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
        mxb::Json j;

        auto jt = extractions.begin();

        for (; it != row.end(); ++it, ++jt)
        {
            const auto& value = *it;
            auto extraction = *jt;

            auto s = value.as_string();

            if (!s.is_null())
            {
                create_entry(j.get_json(), extraction, value.as_string().to_string());
            }
        }

        json = j.to_string(mxb::Json::Format::NORMAL);
    }

    return json;
}
