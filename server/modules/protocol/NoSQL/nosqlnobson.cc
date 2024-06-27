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

std::string nobson::to_json_expression(bsoncxx::array::view array)
{
    std::stringstream ss;

    ss << "JSON_ARRAY(";

    for (auto it = array.begin(); it != array.end(); ++it)
    {
        if (it != array.begin())
        {
            ss << ", ";
        }

        auto element = *it;
        auto value = element.get_value();

        ss << to_json_expression(value);
    }

    ss << ")";

    return ss.str();
}

std::string nobson::to_json_expression(bsoncxx::document::view doc)
{
    std::stringstream ss;

    ss << "JSON_OBJECT(";

    for (auto it = doc.begin(); it != doc.end(); ++it)
    {
        if (it != doc.begin())
        {
            ss << ", ";
        }

        auto element = *it;
        auto key = element.key();
        auto value = element.get_value();

        ss << "'" << escape_essential_chars(key) << "', " << to_json_expression(value);
    }

    ss << ")";

    return ss.str();
}

std::string nobson::to_json_expression(bsoncxx::types::bson_value::view view)
{
    std::stringstream ss;

    switch (view.type())
    {
    case bsoncxx::type::k_double:
        ss << view.get_double();
        break;

    case bsoncxx::type::k_utf8:
        ss << "'" << escape_essential_chars(static_cast<string_view>(view.get_utf8())) << "'";
        break;

    case bsoncxx::type::k_document:
        ss << to_json_expression(view.get_document());
        break;

    case bsoncxx::type::k_array:
        ss << to_json_expression(view.get_array());
        break;

    case bsoncxx::type::k_oid:
        ss << "JSON_OBJECT('$oid', '" << view.get_oid().value.to_string() << "')";
        break;

    case bsoncxx::type::k_bool:
        ss << view.get_bool();
        break;

    case bsoncxx::type::k_date:
        ss << "JSON_OBJECT('$date', " << view.get_date().to_int64() << ")";
        break;

    case bsoncxx::type::k_null:
        ss << "null";
        break;

    case bsoncxx::type::k_int32:
        ss << view.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << view.get_int64();
        break;

    case bsoncxx::type::k_maxkey:
        ss << "JSON_OBJECT('$maxKey', 1)";
        break;

    case bsoncxx::type::k_minkey:
        ss << "JSON_OBJECT('$minKey', 1)";
        break;

    case bsoncxx::type::k_decimal128:
        ss << "JSON_OBJECT('$numberDecimal', '" << view.get_decimal128().value.to_string() << "')";
        break;

    case bsoncxx::type::k_timestamp:
        {
            auto ts = view.get_timestamp();
            ss << "JSON_OBJECT('$timestamp', JSON_OBJECT("
               << "'t', " << ts.timestamp << ", 'i', " << ts.increment
               << "))";
        }
        break;

    case bsoncxx::type::k_binary:
    case bsoncxx::type::k_undefined:
    case bsoncxx::type::k_regex:
    case bsoncxx::type::k_dbpointer:
    case bsoncxx::type::k_code:
    case bsoncxx::type::k_symbol:
    case bsoncxx::type::k_codewscope:
        ss << "Cannot convert a " << bsoncxx::to_string(view.type()) << " to a JSON expression.";
        throw SoftError(ss.str(), error::INTERNAL_ERROR);
    }

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
