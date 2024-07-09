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

#include "nosqlaggregationoperator.hh"
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include "nosqlnobson.hh"
#include <bsoncxx/types/bson_value/value.hpp>

using namespace std;
namespace json = mxb::json;

namespace nosql
{

namespace aggregation
{

namespace
{

#define NOSQL_OPERATOR(O) { O::NAME, O::create }

map<string, Operator::Creator, less<>> operators =
{
    NOSQL_OPERATOR(Cond),
    NOSQL_OPERATOR(Convert),
    NOSQL_OPERATOR(Divide),
    NOSQL_OPERATOR(Max),
    NOSQL_OPERATOR(Min),
    NOSQL_OPERATOR(Multiply),
    NOSQL_OPERATOR(Ne),
    NOSQL_OPERATOR(Sum),
    NOSQL_OPERATOR(ToBool),
    NOSQL_OPERATOR(ToDate),
    NOSQL_OPERATOR(ToDecimal),
    NOSQL_OPERATOR(ToDouble),
    NOSQL_OPERATOR(ToInt),
    NOSQL_OPERATOR(ToLong),
    NOSQL_OPERATOR(ToObjectId),
    NOSQL_OPERATOR(ToString),
};

map<string, Operator::Creator, less<>> expression_operator =
{
};

}


/**
 * Operator
 */
Operator::~Operator()
{
}

//static
void Operator::unsupported(string_view key)
{
    stringstream ss;
    ss << "Unsupported operator '" << key << "'";

    throw SoftError(ss.str(), error::INTERNAL_ERROR);
}

//static
unique_ptr<Operator> Operator::create(BsonView value)
{
    unique_ptr<Operator> sOp;

    switch (value.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view s = value.get_utf8();

            if (!s.empty() && s.front() == '$')
            {
                sOp = Accessor::create(value);
            }
            else
            {
                sOp = Literal::create(value);
            }
        }
        break;

    case bsoncxx::type::k_document:
        {
            bsoncxx::document::view doc = value.get_document();

            auto it = doc.begin();

            if (it == doc.end())
            {
                sOp = Literal::create(value);
            }
            else
            {
                bsoncxx::document::element op = *it;

                auto jt = operators.find(op.key());

                if (jt == operators.end())
                {
                    stringstream ss;
                    ss << "Unrecognized expression '" << op.key() << "'";

                    throw SoftError(ss.str(), error::INVALID_PIPELINE_OPERATOR);
                }

                sOp = jt->second(op.get_value());
            }
        }
        break;

        // TODO: bsoncxx::type::k_array will need specific handling.

    default:
        sOp = Literal::create(value);
    }

    return sOp;
}

/**
 * Operator::Accessor
 */
Operator::Accessor::Accessor(BsonView value)
{
    mxb_assert(value.type() == bsoncxx::type::k_utf8);

    string_view field = value.get_utf8();

    mxb_assert(!field.empty() && field.front() == '$');

    size_t from = 1; // Skip the '$'
    auto to = field.find_first_of('.');

    if (to != string_view::npos)
    {
        do
        {
            m_fields.emplace_back(string(field.substr(from, to - from)));
            from = to + 1;
            to = field.find_first_of('.', from);
        }
        while (to != string_view::npos);
    }

    m_fields.emplace_back(string(field.substr(from)));
}

bsoncxx::types::bson_value::value Operator::Accessor::process(bsoncxx::document::view doc)
{
    m_value = BsonValue(nullptr);

    bsoncxx::document::element element;

    auto it = m_fields.begin();
    do
    {
        element = doc[*it];

        if (!element)
        {
            break;
        }

        ++it;

        if (it == m_fields.end())
        {
            m_value = BsonValue(element.get_value());
        }
        else
        {
            if (element.type() == bsoncxx::type::k_document)
            {
                doc = element.get_document();
            }
            else
            {
                doc = bsoncxx::document::view();
            }
        }
    }
    while (!doc.empty() && it != m_fields.end());

    return m_value;
}

/**
 * Operator::Literal
 */
Operator::Literal::Literal(BsonView value)
    : ConcreteOperator(value)
{
}

bsoncxx::types::bson_value::value Operator::Literal::process(bsoncxx::document::view doc)
{
    return m_value;
}

/**
 * Avg
 */
bsoncxx::types::bson_value::value Avg::process(bsoncxx::document::view doc)
{
    auto value = m_sOp->process(doc);

    if (nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
    {
        ++m_count;

        if (m_count == 1)
        {
            m_value = value;
        }
        else
        {
            // mean = mean + (x - mean) / count
            bsoncxx::types::bson_value::value count(m_count);
            m_value = nobson::add(m_value, nobson::div(nobson::sub(value, m_value), count));
        }
    }

    return m_value;
}

/**
 * Cond
 */
Cond::Cond(BsonView value)
{
    int nArgs = 0;

    switch (value.type())
    {
    case bsoncxx::type::k_document:
        {
            m_ops.resize(3);

            bsoncxx::document::view doc = value.get_document();
            for (auto element : doc)
            {
                auto sOp = Operator::create(element.get_value());
                int index = -1;

                if (element.key() == "if")
                {
                    ++nArgs;
                    index = 0;
                }
                else if (element.key() == "then")
                {
                    ++nArgs;
                    index = 1;
                }
                else if (element.key() == "else")
                {
                    ++nArgs;
                    index = 2;
                }
                else
                {
                    ++nArgs;
                }

                if (index != -1)
                {
                    m_ops[index] = std::move(sOp);
                }
            }
        }
        break;

    case bsoncxx::type::k_array:
        {
            bsoncxx::array::view array = value.get_array();
            for (auto element : array)
            {
                m_ops.emplace_back(Operator::create(element.get_value()));
            }

            nArgs = m_ops.size();
        }
        break;

    default:
        nArgs = 1;
    }

    if (nArgs != 3)
    {
        stringstream ss;
        ss << "Invalid $addFields :: caused by :: Expression $cond takes "
           << "exactly 3 arguments. " << nArgs << " were passed in.";

        throw SoftError(ss.str(), error::LOCATION16020);
    }
}

bsoncxx::types::bson_value::value Cond::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 3);

    m_value = BsonValue(nullptr);

    BsonView cond = m_ops[0]->process(doc);

    if (cond.type() == bsoncxx::type::k_bool)
    {
        if (cond.get_bool())
        {
            m_value = m_ops[1]->process(doc);
        }
        else
        {
            m_value = m_ops[2]->process(doc);
        }
    }

    return m_value;
}

/**
 * Convert
 */
Convert::Convert(BsonView value)
{
    if (value.type() != bsoncxx::type::k_document)
    {
        stringstream ss;
        ss << "$convert expects an object of named arguments but found: "
           << bsoncxx::to_string(value.type());

        throw SoftError(ss.str(), error::FAILED_TO_PARSE);
    }

    bsoncxx::document::view convert = value.get_document();

    bsoncxx::document::element input;
    bsoncxx::document::element to;

    for (auto a : convert)
    {
        string_view key = a.key();

        if (key == "input")
        {
            input = a;
        }
        else if (key == "to")
        {
            to = a;
        }
        else if (key == "onError")
        {
            m_on_error = a.get_value();
        }
        else if (key == "onNull")
        {
            m_on_null = a.get_value();
        }
        else
        {
            stringstream ss;
            ss << "$convert found an unknown argument: " << key;

            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }
    }

    if (!input)
    {
        throw SoftError("Missing 'input' parameter to $convert", error::FAILED_TO_PARSE);
    }

    if (!to)
    {
        throw SoftError("Missing 'input' parameter to $convert", error::FAILED_TO_PARSE);
    }

    m_sInput = Operator::create(input.get_value());
    m_to = get_converter(to);
}

bsoncxx::types::bson_value::value Convert::process(bsoncxx::document::view doc)
{
    auto value = m_sInput->process(doc);

    if (!nobson::is_null(value))
    {
        m_value = m_to(value, m_on_error);
    }
    else if (!nobson::is_null(m_on_null))
    {
        m_value = BsonValue(m_on_null);
    }

    return m_value;
}

//static
bsoncxx::types::bson_value::value Convert::to_bool(BsonView value,
                                                   BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_array:
        return BsonValue(true);

    case bsoncxx::type::k_binary:
        return BsonValue(true);

    case bsoncxx::type::k_bool:
        return BsonValue(value.get_bool());

    case bsoncxx::type::k_code:
        return BsonValue(true);

    case bsoncxx::type::k_date:
        return BsonValue(true);

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            return BsonValue(d.high() != 0 || d.low() != 0 ? true : false);
        }

    case bsoncxx::type::k_double:
        return BsonValue(value.get_double() != 0);

    case bsoncxx::type::k_int32:
        return BsonValue(value.get_int32() != 0);

    case bsoncxx::type::k_codewscope:
        return BsonValue(true);

    case bsoncxx::type::k_int64:
        return BsonValue(value.get_int64() != 0);

    case bsoncxx::type::k_maxkey:
        return BsonValue(true);

    case bsoncxx::type::k_minkey:
        return BsonValue(true);

    case bsoncxx::type::k_null:
        // TODO: Deal with on_null.
        return BsonValue(nullptr);

    case bsoncxx::type::k_document:
        return BsonValue(true);

    case bsoncxx::type::k_oid:
        return BsonValue(true);

    case bsoncxx::type::k_regex:
        return BsonValue(true);

    case bsoncxx::type::k_utf8:
        return BsonValue(true);

    case bsoncxx::type::k_timestamp:
        return BsonValue(true);

    case bsoncxx::type::k_dbpointer:
        return BsonValue(true);

    case bsoncxx::type::k_undefined:
        return BsonValue(false);

    case bsoncxx::type::k_symbol:
        return BsonValue(true);
    }

    mxb_assert(!true);

    return BsonValue(nullptr);
}

//static
bsoncxx::types::bson_value::value Convert::to_date(BsonView value,
                                                   BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_double:
        {
            std::chrono::milliseconds millis_since_epoch(value.get_double());
            return BsonValue(bsoncxx::types::b_date(millis_since_epoch));
        }

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            string s = d.to_string();
            std::chrono::milliseconds millis_since_epoch(std::stoll(s));
            return BsonValue(bsoncxx::types::b_date(millis_since_epoch));
        }

    case bsoncxx::type::k_int64:
        return BsonValue(bsoncxx::types::b_date(std::chrono::milliseconds(value.get_int64())));

    case bsoncxx::type::k_int32:
        return BsonValue(bsoncxx::types::b_date(std::chrono::milliseconds(value.get_int32())));

    case bsoncxx::type::k_utf8:
        throw SoftError("Cannot convert a string to date in $convert", error::INTERNAL_ERROR);

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_date, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_decimal(BsonView value,
                                                      BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue(bsoncxx::decimal128(0, value.get_bool() ? 1 : 0));

    case bsoncxx::type::k_double:
        {
            ostringstream ss;
            ss << std::setprecision(std::numeric_limits<double>::digits10 + 1) << value.get_double();
            return BsonValue(bsoncxx::decimal128(ss.str()));
        }

    case bsoncxx::type::k_decimal128:
        return BsonValue(value.get_decimal128());

    case bsoncxx::type::k_int32:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_int32())));

    case bsoncxx::type::k_int64:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_int64())));

    case bsoncxx::type::k_utf8:
        return BsonValue(bsoncxx::decimal128(value.get_utf8()));

    case bsoncxx::type::k_date:
        return BsonValue(bsoncxx::decimal128(std::to_string(value.get_date().value.count())));

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_decimal128, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_double(BsonView value,
                                                     BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((double)(value.get_bool() ? 1 : 0));;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            double d;
            auto result = nobson::convert(decimal128, &d);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(d);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue(value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue((double)value.get_int32());

    case bsoncxx::type::k_int64:
        return BsonValue((double)value.get_int64());

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                double d = strtod(s.c_str(), &pEnd);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }

                if (errno == ERANGE)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }

                return BsonValue(d);
            }
        }
        mxb_assert(!true);

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_double, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_int32(BsonView value,
                                                    BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((int32_t)value.get_bool());

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int32_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(i);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue((int32_t)value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue(value.get_int32());

    case bsoncxx::type::k_int64:
        {
            int64_t v = value.get_int64();

            if (v < std::numeric_limits<int32_t>::min())
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Conversion would underflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else if (v > std::numeric_limits<int32_t>::max())
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Conversion would overflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                return BsonValue((int32_t)v);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else
                {
                    if (l < std::numeric_limits<int32_t>::min() || l > std::numeric_limits<int32_t>::max())
                    {
                        errno = ERANGE;
                    }

                    if (errno == ERANGE)
                    {
                        if (nobson::is_null(on_error))
                        {
                            stringstream ss;
                            ss << "Failed to parse number '" << sv
                               << "' in $convert with no onError value: Out of range";

                            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                        }

                        return BsonValue(on_error);
                    }
                    else
                    {
                        return BsonValue((int32_t)l);
                    }
                }
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_int32, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_int64(BsonView value,
                                                    BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        return BsonValue((int64_t)value.get_bool());

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int64_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                return BsonValue(i);
            }
            else
            {
                return handle_decimal128_error(decimal128, result, on_error);
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    case bsoncxx::type::k_double:
        return BsonValue((int64_t)value.get_double());

    case bsoncxx::type::k_int32:
        return BsonValue((int64_t)value.get_int32());

    case bsoncxx::type::k_int64:
        return BsonValue(value.get_int64());

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else if (errno == ERANGE)
                {
                    if (nobson::is_null(on_error))
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }

                    return BsonValue(on_error);
                }
                else
                {
                    return BsonValue((int64_t)l);
                }
            }
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_int64, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_oid(BsonView value,
                                                  BsonView on_error)
{
    switch (value.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (sv.length() != 24)
            {
                if (nobson::is_null(on_error))
                {
                    stringstream ss;
                    ss << "Failed to parse objectId '" << sv << "' in $convert with no onError value: "
                       << "Invalid string length for parsing to OID, expected 24 but found "
                       << sv.length();

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }

                return BsonValue(on_error);
            }

            return BsonValue(bsoncxx::oid(sv));
        }
        mxb_assert(!true);
        [[fallthrough]];

    default:
        break;
    }

    return handle_default_case(value.type(), bsoncxx::type::k_oid, on_error);
}

//static
bsoncxx::types::bson_value::value Convert::to_string(BsonView value,
                                                     BsonView on_error)
{
    stringstream ss;

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        ss << value.get_bool();
        break;

    case bsoncxx::type::k_double:
        ss << value.get_double();
        break;

    case bsoncxx::type::k_decimal128:
        ss << value.get_decimal128().value.to_string();
        break;

    case bsoncxx::type::k_int32:
        ss << value.get_int32();
        break;

    case bsoncxx::type::k_int64:
        ss << value.get_int64();
        break;

    case bsoncxx::type::k_oid:
        ss << value.get_oid().value.to_string();
        break;

    case bsoncxx::type::k_utf8:
        ss << value.get_utf8().value;
        break;

    case bsoncxx::type::k_date:
        ss << value.get_date().value.count();
        break;

    default:
        return handle_default_case(value.type(), bsoncxx::type::k_utf8, on_error);
    }

    return BsonValue(ss.str());
}

//static
Convert::Converter Convert::get_converter(bsoncxx::document::element e)
{
    Converter rv;

    if (nobson::is_integer(e))
    {
        rv = get_converter(static_cast<bsoncxx::type>(nobson::get_integer<int32_t>(e)));
    }
    else if (nobson::is_string(e))
    {
        rv = get_converter(e.get_string());
    }
    else
    {
        throw SoftError("$convert's 'to' argument must be a string or number, but is object",
                        error::FAILED_TO_PARSE);
    }

    return rv;
}

//static
Convert::Converter Convert::get_converter(bsoncxx::type type)
{
    Converter c;

    switch (type)
    {
    case bsoncxx::type::k_double:
        c = to_double;
        break;

    case bsoncxx::type::k_string:
        c = to_string;
        break;

    case bsoncxx::type::k_oid:
        c = to_oid;
        break;

    case bsoncxx::type::k_bool:
        c = to_bool;
        break;

    case bsoncxx::type::k_date:
        c = to_date;
        break;

    case bsoncxx::type::k_int32:
        c = to_int32;
        break;

    case bsoncxx::type::k_int64:
        c = to_int64;
        break;

    case bsoncxx::type::k_decimal128:
        c = to_decimal;
        break;

    default:
        {
            stringstream ss;
            ss << "In $convert, numeric value for 'to' does not correspond to a BSON type: "
               << static_cast<int32_t>(type);

            throw SoftError(ss.str(), error::FAILED_TO_PARSE);
        }
    }

    return c;
}

//static
Convert::Converter Convert::get_converter(std::string_view type)
{
    Converter rv;

    static std::map<string_view, bsoncxx::type> types =
        {
            { "double", bsoncxx::type::k_double },
            { "string", bsoncxx::type::k_utf8 },
            { "objectId", bsoncxx::type::k_oid },
            { "bool", bsoncxx::type::k_bool },
            { "date", bsoncxx::type::k_date },
            { "int", bsoncxx::type::k_int32 },
            { "long", bsoncxx::type::k_int64 },
            { "decimal", bsoncxx::type::k_decimal128 },
        };

    auto it = types.find(type);

    if (it != types.end())
    {
        rv = get_converter(it->second);
    }
    else
    {
        stringstream ss;
        ss << "Unknown type name: " << type;

        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return rv;
}

//static
bsoncxx::types::bson_value::value Convert::handle_decimal128_error(bsoncxx::decimal128 decimal128,
                                                                   nobson::ConversionResult result,
                                                                   BsonView on_error)
{
    if (nobson::is_null(on_error))
    {
        if (result == nobson::ConversionResult::OVERFLOW)
        {
            stringstream ss;
            ss << "Conversion would overflow target type in $convert with no onError value: "
               << decimal128.to_string();

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
        else
        {
            mxb_assert(result == nobson::ConversionResult::UNDERFLOW);

            stringstream ss;
            ss << "Conversion would underflow target type in $convert with no onError value: "
               << decimal128.to_string();

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
    }

    return BsonValue(on_error);
}

//static
bsoncxx::types::bson_value::value Convert::handle_default_case(bsoncxx::type from,
                                                               bsoncxx::type to,
                                                               BsonView on_error)
{
    if (nobson::is_null(on_error))
    {
        stringstream ss;
        ss << "$convert cannot convert a "
           << bsoncxx::to_string(from) << " to a(n) " << bsoncxx::to_string(to);
        throw SoftError(ss.str(), error::BAD_VALUE);
    }

    return BsonValue(on_error);
}

/**
 * Divide
 */
Divide::Divide(BsonView value)
{
    int nArgs = 1;

    if (value.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = value.get_array();

        for (auto element : array)
        {
            m_ops.emplace_back(Operator::create(element.get_value()));
        }

        nArgs = m_ops.size();
    }

    if (nArgs != 2)
    {
        stringstream ss;
        ss << "Expression $divide takes exactly 2 arguments. " << nArgs << " were passed in.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }
}

bsoncxx::types::bson_value::value Divide::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    m_value = BsonValue(nullptr);

    BsonView lhs = m_ops[0]->process(doc);
    BsonView rhs = m_ops[1]->process(doc);

    const auto approach = nobson::NumberApproach::REJECT_DECIMAL128;
    if (!nobson::is_number(lhs, approach) || !nobson::is_number(rhs, approach))
    {
        stringstream ss;
        ss << "Failed to optimize pipeline :: caused by :: $divide only supports numeric types, not "
           << bsoncxx::to_string(lhs.type()) << " and " << bsoncxx::to_string(rhs.type());

        throw SoftError(ss.str(), error::TYPE_MISMATCH);
    }

    if (nobson::is_zero(rhs))
    {
        throw SoftError("Failed to optimize pipeline :: caused by :: can't $divide by zero",
                        error::BAD_VALUE);
    }

    m_value = nobson::div(lhs, rhs);

    return m_value;
}

/**
 * First
 */
bsoncxx::types::bson_value::value First::process(bsoncxx::document::view doc)
{
    if (!ready())
    {
        m_value = m_sOp->process(doc);

        set_ready();
    }

    return m_value;
}

/**
 * Last
 */
bsoncxx::types::bson_value::value Last::process(bsoncxx::document::view doc)
{
    // TODO: The position of the doc should be passed, no point in
    // TODO: processing and assigning at every stage.
    m_value = m_sOp->process(doc);

    return m_value;
}

/**
 * Max
 */
bsoncxx::types::bson_value::value Max::process(bsoncxx::document::view doc)
{
    bsoncxx::types::bson_value::value value = m_sOp->process(doc);

    if (m_first)
    {
        m_value = value;
        m_first = false;
    }
    else if (value > m_value)
    {
        m_value = value;
    }

    return m_value;
}

/**
 * Min
 */
bsoncxx::types::bson_value::value Min::process(bsoncxx::document::view doc)
{
    bsoncxx::types::bson_value::value value = m_sOp->process(doc);

    if (m_first)
    {
        m_value = value;
        m_first = false;
    }
    else if (value < m_value)
    {
        m_value = value;
    }

    return m_value;
}

/**
 * Multiply
 */
Multiply::Multiply(BsonView value)
{
    switch (value.type())
    {
    case bsoncxx::type::k_double:
    case bsoncxx::type::k_int32:
    case bsoncxx::type::k_int64:
        m_ops.emplace_back(Literal::create(value));
        break;

    case bsoncxx::type::k_array:
        {
            bsoncxx::array::view array = value.get_array();

            for (auto item : array)
            {
                switch (item.type())
                {
                case bsoncxx::type::k_double:
                case bsoncxx::type::k_int32:
                case bsoncxx::type::k_int64:
                    m_ops.emplace_back(Literal::create(item.get_value()));
                    break;

                case bsoncxx::type::k_document:
                    m_ops.emplace_back(Operator::create(item.get_value()));
                    break;

                case bsoncxx::type::k_utf8:
                    {
                        string_view s = item.get_utf8();

                        if (!s.empty() && s.front() == '$')
                        {
                            m_ops.emplace_back(Operator::create(item.get_value()));
                            break;
                        }
                    }
                    [[fallthrough]];
                default:
                    {
                        stringstream ss;
                        ss << "Failed to optimize pipeline :: caused by :: "
                           << "$multiply only supports numeric types, "
                           << "not " << bsoncxx::to_string(item.type());

                        throw SoftError(ss.str(), error::TYPE_MISMATCH);
                    }
                }
            }
        }
        break;

    case bsoncxx::type::k_utf8:
        {
            string_view s = value.get_utf8();

            if (!s.empty() && s.front() == '$')
            {
                m_ops.emplace_back(Operator::create(value));
                break;
            }
        }
        [[fallthrough]];
    default:
        {
            stringstream ss;
            ss << "Failed to optimize pipeline :: caused by :: $multiply only supports numeric types, "
               << "not " << bsoncxx::to_string(value.type());

            throw SoftError(ss.str(), error::TYPE_MISMATCH);
        }
    }
}

bsoncxx::types::bson_value::value Multiply::process(bsoncxx::document::view doc)
{
    for (auto& sOp : m_ops)
    {
        BsonValue value = sOp->process(doc);

        if (nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128))
        {
            if (nobson::is_null(m_value))
            {
                m_value = value;
            }
            else
            {
                m_value = nobson::mul(m_value, value);
            }
        }
    }

    return m_value;
}

/**
 * Ne
 */
Ne::Ne(BsonView value)
{
    int nArgs = 1;

    if (value.type() == bsoncxx::type::k_array)
    {
        bsoncxx::array::view array = value.get_array();

        for (auto element : array)
        {
            m_ops.emplace_back(Operator::create(element.get_value()));
        }

        nArgs = m_ops.size();
    }

    if (nArgs != 2)
    {
        stringstream ss;
        ss << "Expression $ne takes exactly 2 arguments. " << nArgs << " were passed in.";

        throw SoftError(ss.str(), error::BAD_VALUE);
    }
}

bsoncxx::types::bson_value::value Ne::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    BsonView lhs = m_ops[0]->process(doc);
    BsonView rhs = m_ops[1]->process(doc);

    m_value = BsonValue(lhs != rhs);

    return m_value;
}

/**
 * Sum
 */
bsoncxx::types::bson_value::value Sum::process(bsoncxx::document::view doc)
{
    bsoncxx::types::bson_value::value value = m_sOp->process(doc);

    bool is_number = nobson::is_number(value, nobson::NumberApproach::REJECT_DECIMAL128);

    if (nobson::is_null(m_value) && is_number)
    {
        m_value = value;
    }
    else if (is_number)
    {
        m_value = nobson::add(m_value, value);
    }

    return m_value;
}

/**
 * ToBool
 */
bsoncxx::types::bson_value::value ToBool::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_bool(m_sOp->process(doc));
    return m_value;
}

/**
 * ToDate
 */
bsoncxx::types::bson_value::value ToDate::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_date(m_sOp->process(doc));
    return m_value;
}

/**
 * ToDecimal
 */
bsoncxx::types::bson_value::value ToDecimal::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_decimal(m_sOp->process(doc));
    return m_value;
}

/**
 * ToDouble
 */
bsoncxx::types::bson_value::value ToDouble::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_double(m_sOp->process(doc));
    return m_value;
}

/**
 * ToInt
 */
bsoncxx::types::bson_value::value ToInt::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_int32(m_sOp->process(doc));
    return m_value;
}

/**
 * ToLong
 */
bsoncxx::types::bson_value::value ToLong::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_int64(m_sOp->process(doc));
    return m_value;
}

/**
 * ToObjectId
 */
bsoncxx::types::bson_value::value ToObjectId::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_oid(m_sOp->process(doc));
    return m_value;
}

/**
 * ToString
 */
bsoncxx::types::bson_value::value ToString::process(bsoncxx::document::view doc)
{
    m_value = Convert::to_string(m_sOp->process(doc));
    return m_value;
}

}

}
