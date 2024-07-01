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
#include <sstream>
#include <maxscale/utils.hh>
#include "nosqlbase.hh"

namespace nosql
{

bool nobson::is_zero(bsoncxx::types::bson_value::view v)
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

bool nobson::is_truthy(bsoncxx::types::bson_value::view v)
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
bool nobson::get_integer(bsoncxx::types::bson_value::view view, int64_t* pValue)
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
int64_t nobson::get_integer(bsoncxx::types::bson_value::view view)
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

double nobson::get_double(bsoncxx::types::bson_value::view view)
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
bool nobson::get_number(bsoncxx::types::bson_value::view view, int64_t* pValue)
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
bool nobson::get_number(bsoncxx::types::bson_value::view view, double* pValue)
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
int64_t nobson::get_number(bsoncxx::types::bson_value::view view)
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
double nobson::get_number(bsoncxx::types::bson_value::view view)
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

void nobson::to_json_expression(std::ostream& out, bsoncxx::types::bson_value::view view)
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

std::string nobson::to_json_expression(bsoncxx::types::bson_value::view view)
{
    std::stringstream ss;

    to_json_expression(ss, view);

    return ss.str();
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
