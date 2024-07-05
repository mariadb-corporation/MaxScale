/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "nosqlnobson.hh"
#include <cmath>
#include <map>
#include <sstream>
#include <maxscale/utils.hh>
#include "nosqlbase.hh"

namespace nosql
{

bool nobson::is_zero(const bsoncxx::types::bson_value::view& v)
{
    bool rv = false;

    switch (v.type())
    {
    case bsoncxx::type::k_double:
        rv = v.get_double() == 0;
        break;

    case bsoncxx::type::k_int32:
        rv = v.get_int32() == 0;
        break;

    case bsoncxx::type::k_int64:
        rv = v.get_int64() == 0;
        break;

    default:
        ;
    }

    return rv;
}

bool nobson::is_truthy(const bsoncxx::types::bson_value::view& v)
{
    bool rv;

    switch (v.type())
    {
    case bsoncxx::type::k_bool:
        rv = v.get_bool();
        break;

    case bsoncxx::type::k_double:
        rv = v.get_double() != 0;
        break;

    case bsoncxx::type::k_int32:
        rv = v.get_int32() != 0;
        break;

    case bsoncxx::type::k_int64:
        rv = v.get_int64() != 0;
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = v.get_decimal128().value;

            rv = d.high() != 0 || d.low() != 0;
        }
        break;

    case bsoncxx::type::k_null:
        rv = false;
        break;

    default:
        // Everything else is truthy.
        rv = true;
    }

    return rv;
}

template<>
bool nobson::get_integer(const bsoncxx::types::bson_value::view& view, int64_t* pValue)
{
    bool rv = true;

    switch (view.type())
    {
    case bsoncxx::type::k_int32:
        *pValue = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pValue = view.get_int64();
        break;

    default:
        rv = false;
    }

    return rv;
}

template<>
int64_t nobson::get_integer(const bsoncxx::types::bson_value::view& view)
{
    int64_t rv;

    if (!get_integer(view, &rv))
    {
        std::stringstream ss;
        ss << "Attempting to access a " << bsoncxx::to_string(view.type()) << " as an integer.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return rv;
}

double nobson::get_double(const bsoncxx::types::bson_value::view& view)
{
    double rv;
    if (!get_double(view, &rv))
    {
        std::stringstream ss;
        ss << "Attempting to access a " << bsoncxx::to_string(view.type()) << " as a double.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return rv;
}

template<>
bool nobson::get_number(const bsoncxx::types::bson_value::view& view, int64_t* pValue)
{
    bool rv = true;

    switch (view.type())
    {
    case bsoncxx::type::k_int32:
        *pValue = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pValue = view.get_int64();
        break;

    case bsoncxx::type::k_double:
        *pValue = view.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}

template<>
bool nobson::get_number(const bsoncxx::types::bson_value::view& view, double* pValue)
{
    bool rv = true;

    switch (view.type())
    {
    case bsoncxx::type::k_int32:
        *pValue = view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        *pValue = view.get_int64();
        break;

    case bsoncxx::type::k_double:
        *pValue = view.get_double();
        break;

    default:
        rv = false;
    }

    return rv;
}

template<>
int64_t nobson::get_number(const bsoncxx::types::bson_value::view& view)
{
    int64_t rv;
    if (!get_number(view, &rv))
    {
        std::stringstream ss;
        ss << "Attempting to access a " << bsoncxx::to_string(view.type()) << " as number.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return rv;
}

template<>
double nobson::get_number(const bsoncxx::types::bson_value::view& view)
{
    double rv;
    if (!get_number(view, &rv))
    {
        std::stringstream ss;
        ss << "Attempting to access a " << bsoncxx::to_string(view.type()) << " as a number.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return rv;
}

/**
 * to_json_expression();
 */
void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_array x)
{
    auto array = x.value;

    out << "JSON_ARRAY(";

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        if (it != array.begin())
        {
            out << ", ";
        }

        to_json_expression(out, it->get_value());
    }

    out << ")";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_binary x)
{
    out << "JSON_OBJECT('$binary', '"
        << mxs::to_base64(x.bytes, x.size) << "', '$type', " << std::hex << (int)x.sub_type << "')";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_bool x)
{
    out << x.value;
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_code x)
{
    out << "JSON_OBJECT('$code', '" << escape_essential_chars(x.code) << "')";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_codewscope x)
{
    out << "JSON_OBJECT('$code', '" << escape_essential_chars(x.code) << ", '$scope', ";

    to_json_expression(out, x.scope);

    out << ")";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_date x)
{
    out << "JSON_OBJECT('$date', " << x.to_int64() << ")";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_dbpointer x)
{
    out << "JSON_OBJECT('$ref', '"
        << escape_essential_chars(x.collection) << "', '$id', ";

    to_json_expression(out, x.value);

    out << ")";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_decimal128 x)
{
    out << "JSON_OBJECT('$numberDecimal', '" << x.value.to_string() << "')";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_document x)
{
    to_json_expression(out, x.value);
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_double x)
{
    out << x.value;
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_int32 x)
{
    out << x.value;
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_int64 x)
{
    out << x.value;
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_maxkey x)
{
    out << "JSON_OBJECT('$maxKey', 1)";

}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_minkey x)
{
    out << "JSON_OBJECT('$minKey', 1)";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_null x)
{
    out << "null";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_oid x)
{
    to_json_expression(out, x.value);
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_regex x)
{
    out << "JSON_OBJECT('$regex', '"
        << escape_essential_chars(x.regex)
        << "', '$options', '"
        << escape_essential_chars(x.options)
        << "')";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_string x)
{
    out << "'" << escape_essential_chars(x.value) << "'";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_symbol x)
{
    out << "'$$symbol', '" << escape_essential_chars(x.symbol) << "'";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_timestamp x)
{
    out << "JSON_OBJECT('$timestamp', JSON_OBJECT("
        << "'t', " << x.timestamp << ", 'i', " << x.increment
        << "))";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::b_undefined x)
{
    out << "JSON_OBJECT('$undefined', true)";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::oid oid)
{
    out << "JSON_OBJECT('$oid', '" << oid.to_string() << "')";
}

void nobson::to_json_expression(std::ostream& out, bsoncxx::document::view doc)
{
    out << "JSON_OBJECT(";

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        if (it != doc.begin())
        {
            out << ", ";
        }

        auto key = it->key();
        auto value = it->get_value();

        out << "'" << escape_essential_chars(key) << "', ";

        to_json_expression(out, value);
    }

    out << ")";
}

void nobson::to_json_expression(std::ostream& out, const bsoncxx::types::bson_value::view& view)
{
    switch (view.type())
    {
    case bsoncxx::type::k_array:
        to_json_expression(out, view.get_array());
        break;

    case bsoncxx::type::k_binary:
        to_json_expression(out, view.get_binary());
        break;

    case bsoncxx::type::k_bool:
        to_json_expression(out, view.get_bool());
        break;

    case bsoncxx::type::k_code:
        to_json_expression(out, view.get_code());
        break;

    case bsoncxx::type::k_codewscope:
        to_json_expression(out, view.get_codewscope());
        break;

    case bsoncxx::type::k_date:
        to_json_expression(out, view.get_date());
        break;

    case bsoncxx::type::k_dbpointer:
        to_json_expression(out, view.get_dbpointer());
        break;

    case bsoncxx::type::k_decimal128:
        to_json_expression(out, view.get_decimal128());
        break;

    case bsoncxx::type::k_document:
        to_json_expression(out, view.get_document());
        break;

    case bsoncxx::type::k_double:
        to_json_expression(out, view.get_double());
        break;

    case bsoncxx::type::k_oid:
        to_json_expression(out, view.get_oid());
        break;

    case bsoncxx::type::k_int32:
        to_json_expression(out, view.get_int32());
        break;

    case bsoncxx::type::k_int64:
        to_json_expression(out, view.get_int64());
        break;

    case bsoncxx::type::k_maxkey:
        to_json_expression(out, view.get_maxkey());
        break;

    case bsoncxx::type::k_minkey:
        to_json_expression(out, view.get_minkey());
        break;

    case bsoncxx::type::k_null:
        to_json_expression(out, view.get_null());
        break;

    case bsoncxx::type::k_regex:
        to_json_expression(out, view.get_regex());
        break;

    case bsoncxx::type::k_string:
        to_json_expression(out, view.get_utf8());
        break;

    case bsoncxx::type::k_symbol:
        to_json_expression(out, view.get_symbol());
        break;

    case bsoncxx::type::k_timestamp:
        to_json_expression(out, view.get_timestamp());
        break;

    case bsoncxx::type::k_undefined:
        to_json_expression(out, view.get_undefined());
        break;

    default:
        mxb_assert(!true);
    }
}

std::string nobson::to_json_expression(const bsoncxx::types::bson_value::view& view)
{
    std::stringstream ss;

    to_json_expression(ss, view);

    return ss.str();
}

/**
 * to_bson_expression();
 */
void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_array x)
{
    auto array = x.value;

    out << "[";

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        if (it != array.begin())
        {
            out << ", ";
        }

        to_bson_expression(out, it->get_value());
    }

    out << "]";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_binary x)
{
    out << "BinData(...)";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_bool x)
{
    out << x.value;
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_code x)
{
    // TODO: Escape '"' characters.
    out << "Code(\"" << x.code << "\")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_codewscope x)
{
    // TODO: Escape '"' characters.
    out << "Code(\"" << x.code << "\", ";

    to_bson_expression(out, x.scope);

    out << ")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_date x)
{
    out << "Date(" << x.to_int64() << ")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_dbpointer x)
{
    out << "DBPointer(" << x.collection << ", ";

    to_bson_expression(out, x.value);

    out << ")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_decimal128 x)
{
    out << "NumberDecimal(\"" << x.value.to_string() << "\")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_document x)
{
    to_bson_expression(out, x.value);
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_double x)
{
    out << x.value;
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_int32 x)
{
    out << x.value;
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_int64 x)
{
    out << x.value;
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_maxkey x)
{
    out << "MaxKey()";

}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_minkey x)
{
    out << "MinKey()";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_null x)
{
    out << "null";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_oid x)
{
    to_bson_expression(out, x.value);
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_regex x)
{
    out << "RegExp(\"" << x.regex << ", " << "\"" << x.options << "\")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_string x)
{
    // TODO: Escape '"' characters.
    out << "\"" << x.value << "\"";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_symbol x)
{
    out << "BSON.Symbol(\"" << x.symbol << "\")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_timestamp x)
{
    out << "Timestamp(" << x.timestamp << ", " << x.increment << ")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::types::b_undefined x)
{
    out << "undefined";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::oid oid)
{
    out << "ObjectId(" << oid.to_string() << ")";
}

void nobson::to_bson_expression(std::ostream& out, bsoncxx::document::view doc)
{
    out << "{";

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        if (it != doc.begin())
        {
            out << ", ";
        }

        auto key = it->key();
        auto value = it->get_value();

        out << "'" << escape_essential_chars(key) << "', ";

        to_bson_expression(out, value);
    }

    out << "}";
}

void nobson::to_bson_expression(std::ostream& out, const bsoncxx::types::bson_value::view& view)
{
    switch (view.type())
    {
    case bsoncxx::type::k_array:
        to_bson_expression(out, view.get_array());
        break;

    case bsoncxx::type::k_binary:
        to_bson_expression(out, view.get_binary());
        break;

    case bsoncxx::type::k_bool:
        to_bson_expression(out, view.get_bool());
        break;

    case bsoncxx::type::k_code:
        to_bson_expression(out, view.get_code());
        break;

    case bsoncxx::type::k_codewscope:
        to_bson_expression(out, view.get_codewscope());
        break;

    case bsoncxx::type::k_date:
        to_bson_expression(out, view.get_date());
        break;

    case bsoncxx::type::k_dbpointer:
        to_bson_expression(out, view.get_dbpointer());
        break;

    case bsoncxx::type::k_decimal128:
        to_bson_expression(out, view.get_decimal128());
        break;

    case bsoncxx::type::k_document:
        to_bson_expression(out, view.get_document());
        break;

    case bsoncxx::type::k_double:
        to_bson_expression(out, view.get_double());
        break;

    case bsoncxx::type::k_oid:
        to_bson_expression(out, view.get_oid());
        break;

    case bsoncxx::type::k_int32:
        to_bson_expression(out, view.get_int32());
        break;

    case bsoncxx::type::k_int64:
        to_bson_expression(out, view.get_int64());
        break;

    case bsoncxx::type::k_maxkey:
        to_bson_expression(out, view.get_maxkey());
        break;

    case bsoncxx::type::k_minkey:
        to_bson_expression(out, view.get_minkey());
        break;

    case bsoncxx::type::k_null:
        to_bson_expression(out, view.get_null());
        break;

    case bsoncxx::type::k_regex:
        to_bson_expression(out, view.get_regex());
        break;

    case bsoncxx::type::k_string:
        to_bson_expression(out, view.get_utf8());
        break;

    case bsoncxx::type::k_symbol:
        to_bson_expression(out, view.get_symbol());
        break;

    case bsoncxx::type::k_timestamp:
        to_bson_expression(out, view.get_timestamp());
        break;

    case bsoncxx::type::k_undefined:
        to_bson_expression(out, view.get_undefined());
        break;

    default:
        mxb_assert(!true);
    }
}

std::string nobson::to_bson_expression(const bsoncxx::types::bson_value::view& view)
{
    std::stringstream ss;

    to_bson_expression(ss, view);

    return ss.str();
}

namespace
{

const int ORDER_MINKEY     = 1;
const int ORDER_UNDEFINED  = 2;
const int ORDER_NULL       = 3;
const int ORDER_NUMBER     = 4;
const int ORDER_STRING     = 5;
const int ORDER_DOCUMENT   = 6;
const int ORDER_ARRAY      = 7;
const int ORDER_BINARY     = 8;
const int ORDER_OID        = 9;
const int ORDER_BOOL       = 10;
const int ORDER_DATE       = 11;
const int ORDER_TIMESTAMP  = 12;
const int ORDER_REGEX      = 13;
const int ORDER_CODE       = 14;
const int ORDER_CODEWSCOPE = 15;
const int ORDER_DBPOINTER  = 16;
const int ORDER_MAXKEY     = 17;

std::map<bsoncxx::type, int> type_order =
{
    { bsoncxx::type::k_minkey,     ORDER_MINKEY },
    { bsoncxx::type::k_undefined,  ORDER_UNDEFINED },
    { bsoncxx::type::k_null,       ORDER_NULL },
    { bsoncxx::type::k_int32,      ORDER_NUMBER },
    { bsoncxx::type::k_int64,      ORDER_NUMBER },
    { bsoncxx::type::k_double,     ORDER_NUMBER },
    { bsoncxx::type::k_decimal128, ORDER_NUMBER },
    { bsoncxx::type::k_symbol,     ORDER_STRING },
    { bsoncxx::type::k_string,     ORDER_STRING },
    { bsoncxx::type::k_document,   ORDER_DOCUMENT },
    { bsoncxx::type::k_array,      ORDER_ARRAY },
    { bsoncxx::type::k_binary,     ORDER_BINARY },
    { bsoncxx::type::k_oid,        ORDER_OID },
    { bsoncxx::type::k_bool,       ORDER_BOOL },
    { bsoncxx::type::k_date,       ORDER_DATE },
    { bsoncxx::type::k_timestamp,  ORDER_TIMESTAMP },
    { bsoncxx::type::k_regex,      ORDER_REGEX },
    { bsoncxx::type::k_code,       ORDER_CODE },
    { bsoncxx::type::k_codewscope, ORDER_CODEWSCOPE },
    { bsoncxx::type::k_dbpointer,  ORDER_DBPOINTER },
    { bsoncxx::type::k_maxkey,     ORDER_MAXKEY },
};

int get_order(bsoncxx::type type)
{
    auto it = type_order.find(type);
    mxb_assert(it != type_order.end());

    return it->second;
}

template<class N>
int compare_decimal128(nosql::nobson::ConversionResult cr, N l, N r)
{
    int rv = 0;

    switch (cr)
    {
    case nosql::nobson::ConversionResult::OK:
        rv = l == r ? 0 : (l < r ? -1 : 1);
        break;

    case nosql::nobson::ConversionResult::UNDERFLOW:
        rv = -1;
        break;

    case nosql::nobson::ConversionResult::OVERFLOW:
        rv = 1;
        break;
    }

    return rv;
}

int compare_decimal128(bsoncxx::decimal128 lhs, bsoncxx::types::value rhs)
{
    int rv;

    switch (rhs.type())
    {
    case bsoncxx::type::k_decimal128:
        {
            auto l = lhs;
            auto r = rhs.get_decimal128().value;

            rv = l == r
                ? 0
                : (l.high() == r.high()
                   ? (l.low() == r.low()
                      ? 0
                      : (l.low() < r.low() ? -1 : 1))
                   : (l.high() < r.high() ? -1 : 1));
        }
        break;

    case bsoncxx::type::k_double:
        {
            double l;
            double r = rhs.get_double();
            auto cr = nosql::nobson::convert(lhs, &l);
            rv = compare_decimal128(cr, l, r);
        }
        break;

    case bsoncxx::type::k_int32:
        {
            int32_t l;
            int32_t r = rhs.get_int32();
            auto cr = nosql::nobson::convert(lhs, &l);
            rv = compare_decimal128(cr, l, r);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            int64_t l;
            int64_t r = rhs.get_int64();
            auto cr = nosql::nobson::convert(lhs, &l);
            rv = compare_decimal128(cr, l, r);
        }
        break;

    default:
        mxb_assert(!true);
    }

    return rv;
}

int compare_double(double l, bsoncxx::types::value rhs)
{
    int rv;

    switch (rhs.type())
    {
    case bsoncxx::type::k_decimal128:
        {
            double r;
            auto cr = nosql::nobson::convert(rhs.get_decimal128(), &r);
            rv = compare_decimal128(cr, l, r);
        }
        break;

    case bsoncxx::type::k_double:
        {
            double r = rhs.get_double();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    case bsoncxx::type::k_int32:
        {
            int32_t r = rhs.get_int32();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            int64_t r = rhs.get_int64();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    default:
        mxb_assert(!true);
    }

    return rv;
}

int compare_int64(int64_t l, bsoncxx::types::value rhs)
{
    int rv;

    switch (rhs.type())
    {
    case bsoncxx::type::k_decimal128:
        {
            int64_t r;
            auto cr = nosql::nobson::convert(rhs.get_decimal128(), &r);
            rv = compare_decimal128(cr, l, r);
        }
        break;

    case bsoncxx::type::k_double:
        {
            double r = rhs.get_double();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    case bsoncxx::type::k_int32:
        {
            int32_t r = rhs.get_int32();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            int64_t r = rhs.get_int64();
            rv = l == r ? 0 : (l < r ? -1 : 1);
        }
        break;

    default:
        mxb_assert(!true);
    }

    return rv;
}

int compare_int32(int64_t l, bsoncxx::types::value rhs)
{
    return compare_int64(l, rhs);
}


int compare_number(bsoncxx::types::value lhs, bsoncxx::types::value rhs)
{
    int rv = 0;

    switch (lhs.type())
    {
    case bsoncxx::type::k_decimal128:
        rv = compare_decimal128(lhs.get_decimal128().value, rhs);
        break;

    case bsoncxx::type::k_double:
        rv = compare_double(lhs.get_double(), rhs);
        break;

    case bsoncxx::type::k_int32:
        rv = compare_int32(lhs.get_int32(), rhs);
        break;

    case bsoncxx::type::k_int64:
        rv = compare_int64(lhs.get_int64(), rhs);
        break;

    default:
        mxb_assert(!true);
    }

    return rv;
}

int compare_string(bsoncxx::types::value lhs, bsoncxx::types::value rhs)
{
    string_view l;

    if (lhs.type() == bsoncxx::type::k_string)
    {
        l = lhs.get_string();
    }
    else
    {
        mxb_assert(lhs.type() == bsoncxx::type::k_symbol);
        l = lhs.get_symbol();
    }

    string_view r;

    if (rhs.type() == bsoncxx::type::k_string)
    {
        r = rhs.get_string();
    }
    else
    {
        mxb_assert(rhs.type() == bsoncxx::type::k_symbol);
        r = lhs.get_symbol();
    }

    return l.compare(r);
}

int compare_document(bsoncxx::document::view lhs, bsoncxx::document::view rhs)
{
    auto it = lhs.begin();
    auto jt = rhs.begin();

    while (it != lhs.end() && jt != rhs.end())
    {
        auto le = *it;
        auto re = *jt;

        // First compare the types.
        int lorder = get_order(le.type());
        int rorder = get_order(re.type());

        if (lorder < rorder)
        {
            return -1;
        }
        else if (lorder > rorder)
        {
            return 1;
        }

        // Then compare the field names.
        auto lname = le.key();
        auto rname = re.key();

        if (lname < rname)
        {
            return -1;
        }
        else if (lname > rname)
        {
            return 1;
        }

        // Then compare the values.
        int rv = nosql::nobson::compare(le.get_value(), re.get_value());

        if (rv != 0)
        {
            return rv;
        }

        ++it;
        ++jt;
    }

    if (it == lhs.end() && jt != rhs.end())
    {
        return -1;
    }
    else if (it != lhs.end() && jt == rhs.end())
    {
        return 1;
    }

    return 0;
}

int compare_array(bsoncxx::array::view lhs, bsoncxx::array::view rhs)
{
    auto it = lhs.begin();
    auto jt = rhs.begin();

    while (it != lhs.end() && jt != rhs.end())
    {
        auto le = *it;
        auto re = *jt;

        // First compare the types.
        int lorder = get_order(le.type());
        int rorder = get_order(re.type());

        if (lorder < rorder)
        {
            return -1;
        }
        else if (lorder > rorder)
        {
            return 1;
        }

        // Then compare the values.
        int rv = nosql::nobson::compare(le.get_value(), re.get_value());

        if (rv != 0)
        {
            return rv;
        }

        ++it;
        ++jt;
    }

    if (it == lhs.end() && jt != rhs.end())
    {
        return -1;
    }
    else if (it != lhs.end() && jt == rhs.end())
    {
        return 1;
    }

    return 0;
}

int compare_binary(bsoncxx::types::b_binary lhs, bsoncxx::types::b_binary rhs)
{
    // The size
    if (lhs.size < rhs.size)
    {
        return -1;
    }
    else if (lhs.size > rhs.size)
    {
        return 1;
    }

    // The subtype
    if (lhs.sub_type < rhs.sub_type)
    {
        return -1;
    }
    else if (lhs.sub_type > rhs.sub_type)
    {
        return 1;
    }

    // The content
    return memcmp(lhs.bytes, rhs.bytes, lhs.size);
}

int compare_oid(bsoncxx::types::b_oid lhs, bsoncxx::types::b_oid rhs)
{
    bsoncxx::oid l = lhs.value;
    bsoncxx::oid r = rhs.value;

    return l == r ? 0 : (l < r ? -1 : 1);
}

int compare_bool(bool lhs, bool rhs)
{
    return lhs == rhs ? 0 : (lhs < rhs ? -1 : 1);
}

int compare_date(bsoncxx::types::b_date lhs, bsoncxx::types::b_date rhs)
{
    return lhs.value == rhs.value ? 0 : (lhs.value < rhs.value ? -1 : 1);
}

int compare_timestamp(bsoncxx::types::b_timestamp lhs, bsoncxx::types::b_timestamp rhs)
{
    return lhs.timestamp == rhs.timestamp
        ? (lhs.increment == rhs.increment ? 0 : (lhs.increment < rhs.increment ? -1 : 1))
        : (lhs.timestamp < rhs.timestamp ? -1 : 1);
}

int compare_regex(bsoncxx::types::b_regex lhs, bsoncxx::types::b_regex rhs)
{
    int rv = lhs.regex.compare(rhs.regex);

    if (rv == 0)
    {
        lhs.options.compare(rhs.options);
    }

    return rv;
}

int compare_code(bsoncxx::types::b_code lhs, bsoncxx::types::b_code rhs)
{
    return lhs.code.compare(rhs.code);
}

int compare_codewscope(bsoncxx::types::b_codewscope lhs, bsoncxx::types::b_codewscope rhs)
{
    int rv = lhs.code.compare(rhs.code);

    if (rv == 0)
    {
        rv = compare_document(lhs.scope, rhs.scope);
    }

    return rv;
}

int compare_dbpointer(bsoncxx::types::b_dbpointer lhs, bsoncxx::types::b_dbpointer rhs)
{
    int rv = lhs.collection.compare(rhs.collection);

    if (rv == 0)
    {
        rv = lhs.value == rhs.value ? 0 : (lhs.value < rhs.value ? -1 : 1);
    }

    return rv;
}

}

int nobson::compare(const bsoncxx::types::bson_value::view& lhs, const bsoncxx::types::bson_value::view& rhs)
{
    int rv;

    auto lit = type_order.find(lhs.type());
    mxb_assert(lit != type_order.end());
    int lorder = lit->second;

    auto rit = type_order.find(rhs.type());
    mxb_assert(rit != type_order.end());
    int rorder = rit->second;

    if (lorder != rorder)
    {
        int diff = lorder - rorder;

        rv = diff < 0 ? -1 : (diff > 0 ? 1 : 0);
    }
    else
    {
        mxb_assert(lorder == rorder);

        switch (lorder)
        {
        case ORDER_MINKEY:
            rv = 0;
            break;

        case ORDER_UNDEFINED:
            rv = 0;
            break;

        case ORDER_NULL:
            rv = 0;
            break;

        case ORDER_NUMBER:
            rv = compare_number(lhs, rhs);
            break;

        case ORDER_STRING:
            rv = compare_string(lhs, rhs);
            break;

        case ORDER_DOCUMENT:
            rv = compare_document(lhs.get_document(), rhs.get_document());
            break;

        case ORDER_ARRAY:
            rv = compare_array(lhs.get_array(), rhs.get_array());
            break;

        case ORDER_BINARY:
            rv = compare_binary(lhs.get_binary(), rhs.get_binary());
            break;

        case ORDER_OID:
            rv = compare_oid(lhs.get_oid(), rhs.get_oid());
            break;

        case ORDER_BOOL:
            rv = compare_bool(lhs.get_bool(), rhs.get_bool());
            break;

        case ORDER_DATE:
            rv = compare_date(lhs.get_date(), rhs.get_date());
            break;

        case ORDER_TIMESTAMP:
            rv = compare_timestamp(lhs.get_timestamp(), rhs.get_timestamp());
            break;

        case ORDER_REGEX:
            rv = compare_regex(lhs.get_regex(), rhs.get_regex());
            break;

        case ORDER_CODE:
            rv = compare_code(lhs.get_code(), rhs.get_code());
            break;

        case ORDER_CODEWSCOPE:
            rv = compare_codewscope(lhs.get_codewscope(), rhs.get_codewscope());
            break;

        case ORDER_DBPOINTER:
            rv = compare_dbpointer(lhs.get_dbpointer(), rhs.get_dbpointer());
            break;

        case ORDER_MAXKEY:
            rv = 0;
        }
    }

    return rv;
}

//static
nobson::ConversionResult nobson::convert(bsoncxx::decimal128 decimal128, double* pValue)
{
    ConversionResult rv = ConversionResult::OK;

    std::string s = decimal128.to_string();

    errno = 0;
    double d = strtod(s.c_str(), nullptr);

    if (errno == 0)
    {
        *pValue = d;
    }
    else
    {
        mxb_assert(errno == ERANGE);

        if (d == HUGE_VAL || d == -HUGE_VAL)
        {
            rv = ConversionResult::OVERFLOW;
        }
        else
        {
            rv = ConversionResult::UNDERFLOW;
        }
    }

    return rv;
}

//static
nobson::ConversionResult nobson::convert(bsoncxx::decimal128 decimal128, int32_t* pValue)
{
    double d;
    auto result = convert(decimal128, &d);

    if (result == ConversionResult::OK)
    {
        if (d < static_cast<double>(std::numeric_limits<int32_t>::min()))
        {
            result = ConversionResult::UNDERFLOW;
        }
        else if (d > static_cast<double>(std::numeric_limits<int32_t>::max()))
        {
            result = ConversionResult::OVERFLOW;
        }
        else
        {
            *pValue = d;
        }
    }

    return result;
}

//static
nobson::ConversionResult nobson::convert(bsoncxx::decimal128 decimal128, int64_t* pValue)
{
    double d;
    auto result = convert(decimal128, &d);

    if (result == ConversionResult::OK)
    {
        if (d < static_cast<double>(std::numeric_limits<int64_t>::min()))
        {
            result = ConversionResult::UNDERFLOW;
        }
        else if (d > static_cast<double>(std::numeric_limits<int64_t>::max()))
        {
            result = ConversionResult::OVERFLOW;
        }
        else
        {
            *pValue = d;
        }
    }

    return result;
}


}
