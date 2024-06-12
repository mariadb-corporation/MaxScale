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
