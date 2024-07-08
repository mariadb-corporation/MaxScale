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
    NOSQL_OPERATOR(First),
    NOSQL_OPERATOR(Max),
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
unique_ptr<Operator> Operator::create(bsoncxx::types::value value)
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

namespace
{

int64_t get_int(const Operator::Number& v)
{
    if (std::holds_alternative<int32_t>(v))
    {
        return std::get<int32_t>(v);
    }
    else if (std::holds_alternative<int64_t>(v))
    {
        return std::get<int64_t>(v);
    }

    mxb_assert(!true);
    return 0;
}

Operator::Number get_number(bsoncxx::types::value v)
{
    switch (v.type())
    {
    case bsoncxx::type::k_double:
        return Operator::Number(v.get_double());

    case bsoncxx::type::k_int32:
        return Operator::Number(v.get_int32());

    case bsoncxx::type::k_int64:
        return Operator::Number(v.get_int64());

    default:
        ;
    }

    mxb_assert(!true);
    return Operator::Number();
}

}

Operator::Number Operator::mul(const Number& lhs, const Number& rhs)
{
    Number rv;

    bool lhs_is_real = std::holds_alternative<double>(lhs);
    bool rhs_is_real = std::holds_alternative<double>(rhs);

    if (lhs_is_real || rhs_is_real)
    {
        double l = lhs_is_real ? std::get<double>(lhs) : get_int(lhs);
        double r = rhs_is_real ? std::get<double>(rhs) : get_int(rhs);

        rv = Number(l * r);
    }
    else
    {
        int64_t v = get_int(lhs) * get_int(rhs);

        if (v >= std::numeric_limits<int32_t>::min()
            && v <= std::numeric_limits<int32_t>::max()
            && std::holds_alternative<int32_t>(lhs)
            && std::holds_alternative<int32_t>(rhs))
        {
            // If both numbers were int32_t and the result fits in an int32_r,
            // lets store it in an int32_t.
            rv = Number((int32_t)v);
        }
        else
        {
            rv = Number(v);
        }
    }

    return rv;
}

/**
 * Accessor
 */
Accessor::Accessor(bsoncxx::types::value value)
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

bsoncxx::types::value Accessor::process(bsoncxx::document::view doc)
{
    bsoncxx::types::value value;

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
            m_value = element.get_value();
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
 * Literal
 */
Literal::Literal(bsoncxx::types::value value)
    : ConcreteOperator(value)
{
}

bsoncxx::types::value Literal::process(bsoncxx::document::view doc)
{
    return m_value;
}

/**
 * Cond
 */
Cond::Cond(bsoncxx::types::value value)
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

bsoncxx::types::value Cond::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 3);

    bsoncxx::types::value rv;
    bsoncxx::types::value cond = m_ops[0]->process(doc);

    if (cond.type() == bsoncxx::type::k_bool)
    {
        if (cond.get_bool())
        {
            rv = m_ops[1]->process(doc);
        }
        else
        {
            rv = m_ops[2]->process(doc);
        }
    }

    return rv;
}

/**
 * Convert
 */
Convert::Convert(bsoncxx::types::value value)
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

bsoncxx::types::value Convert::process(bsoncxx::document::view doc)
{
    m_builder.clear();

    bsoncxx::types::value value = m_sInput->process(doc);

    if (!nobson::is_null(value))
    {
        value = m_to(m_builder, value, m_on_error);
    }
    else if (!nobson::is_null(m_on_null))
    {
        m_builder.append(m_on_null);
        value = (*m_builder.view().begin()).get_value();
    }

    return value;
}

//static
bsoncxx::types::value Convert::to_bool(ArrayBuilder& builder,
                                       bsoncxx::types::value value,
                                       bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_array:
        builder.append(true);
        break;

    case bsoncxx::type::k_binary:
        builder.append(true);
        break;

    case bsoncxx::type::k_bool:
        builder.append(value.get_bool());
        break;

    case bsoncxx::type::k_code:
        builder.append(true);
        break;

    case bsoncxx::type::k_date:
        builder.append(true);
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            builder.append(d.high() != 0 || d.low() != 0 ? true : false);
        }
        break;

    case bsoncxx::type::k_double:
        builder.append(value.get_double() != 0 ? true : false);
        break;

    case bsoncxx::type::k_int32:
        builder.append(value.get_int32() != 0 ? true : false);
        break;

    case bsoncxx::type::k_codewscope:
        builder.append(true);
        break;

    case bsoncxx::type::k_int64:
        builder.append(value.get_int64() != 0 ? true : false);
        break;

    case bsoncxx::type::k_maxkey:
        builder.append(true);
        break;

    case bsoncxx::type::k_minkey:
        builder.append(true);
        break;

    case bsoncxx::type::k_null:
        // TODO: Deal with on_null.
        builder.append(bsoncxx::types::b_null());
        break;

    case bsoncxx::type::k_document:
        builder.append(true);
        break;

    case bsoncxx::type::k_oid:
        builder.append(true);
        break;

    case bsoncxx::type::k_regex:
        builder.append(true);
        break;

    case bsoncxx::type::k_utf8:
        builder.append(true);
        break;

    case bsoncxx::type::k_timestamp:
        builder.append(true);
        break;

    case bsoncxx::type::k_dbpointer:
        builder.append(true);
        break;

    case bsoncxx::type::k_undefined:
        builder.append(false);
        break;

    case bsoncxx::type::k_symbol:
        builder.append(true);
        break;
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_date(ArrayBuilder& builder,
                                       bsoncxx::types::value value,
                                       bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_double:
        {
            std::chrono::milliseconds millis_since_epoch(value.get_double());
            builder.append(bsoncxx::types::b_date(millis_since_epoch));
        }
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 d = value.get_decimal128().value;
            string s = d.to_string();
            std::chrono::milliseconds millis_since_epoch(std::stoll(s));

            builder.append(bsoncxx::types::b_date(millis_since_epoch));
        }
        break;

    case bsoncxx::type::k_int64:
        builder.append(bsoncxx::types::b_date(std::chrono::milliseconds(value.get_int64())));
        break;

    case bsoncxx::type::k_utf8:
        throw SoftError("Cannot convert a string to date in $convert", error::INTERNAL_ERROR);
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_date, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_decimal(ArrayBuilder& builder,
                                          bsoncxx::types::value value,
                                          bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        builder.append(bsoncxx::decimal128(0, value.get_bool() ? 1 : 0));
        break;

    case bsoncxx::type::k_double:
        {
            ostringstream ss;
            ss << std::setprecision(std::numeric_limits<double>::digits10 + 1) << value.get_double();
            builder.append(bsoncxx::decimal128(ss.str()));
        }
        break;

    case bsoncxx::type::k_decimal128:
        builder.append(value.get_decimal128());
        break;

    case bsoncxx::type::k_int32:
        builder.append(bsoncxx::decimal128(std::to_string(value.get_int32())));
        break;

    case bsoncxx::type::k_int64:
        builder.append(bsoncxx::decimal128(std::to_string(value.get_int64())));
        break;

    case bsoncxx::type::k_utf8:
        builder.append(bsoncxx::decimal128(value.get_utf8()));
        break;

    case bsoncxx::type::k_date:
        builder.append(bsoncxx::decimal128(std::to_string(value.get_date().value.count())));
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_decimal128, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_double(ArrayBuilder& builder,
                                         bsoncxx::types::value value,
                                         bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        builder.append((double)(value.get_bool() ? 1 : 0));;
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            double d;
            auto result = nobson::convert(decimal128, &d);

            if (result == nobson::ConversionResult::OK)
            {
                builder.append(d);
            }
            else
            {
                handle_decimal128_error(builder, decimal128, result, on_error);
            }
        }
        break;

    case bsoncxx::type::k_double:
        builder.append(value);
        break;

    case bsoncxx::type::k_int32:
        builder.append((double)value.get_int32());
        break;

    case bsoncxx::type::k_int64:
        builder.append((double)value.get_int64());
        break;

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                double d = strtod(s.c_str(), &pEnd);

                if (*pEnd != 0)
                {
                    if (!nobson::is_null(on_error))
                    {
                        builder.append(on_error);
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }
                }

                if (errno == ERANGE)
                {
                    if (!nobson::is_null(on_error))
                    {
                        builder.append(on_error);
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }
                }

                builder.append(d);
            }
        }
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_double, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_int32(ArrayBuilder& builder,
                                        bsoncxx::types::value value,
                                        bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        builder.append((int32_t)value.get_bool());
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int32_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                builder.append(i);
            }
            else
            {
                handle_decimal128_error(builder, decimal128, result, on_error);
            }
        }
        break;

    case bsoncxx::type::k_double:
        builder.append((int32_t)value.get_double());
        break;

    case bsoncxx::type::k_int32:
        builder.append(value.get_int32());
        break;

    case bsoncxx::type::k_int64:
        {
            int64_t v = value.get_int64();

            if (v < std::numeric_limits<int32_t>::min())
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Conversion would underflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }
            else if (v > std::numeric_limits<int32_t>::max())
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Conversion would overflow target type in $convert with no onError value: "
                       << v;

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }
            else
            {
                builder.append((int32_t)v);
            }
        }
        break;

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (!nobson::is_null(on_error))
                    {
                        builder.append(on_error);
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }
                }
                else
                {
                    if (l < std::numeric_limits<int32_t>::min() || l > std::numeric_limits<int32_t>::max())
                    {
                        errno = ERANGE;
                    }

                    if (errno == ERANGE)
                    {
                        if (!nobson::is_null(on_error))
                        {
                            builder.append(on_error);
                        }
                        else
                        {
                            stringstream ss;
                            ss << "Failed to parse number '" << sv
                               << "' in $convert with no onError value: Out of range";

                            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                        }
                    }
                    else
                    {
                        builder.append((int32_t)l);
                    }
                }
            }
        }
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_int32, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_int64(ArrayBuilder& builder,
                                        bsoncxx::types::value value,
                                        bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_bool:
        builder.append((int64_t)value.get_bool());
        break;

    case bsoncxx::type::k_decimal128:
        {
            bsoncxx::decimal128 decimal128 = value.get_decimal128().value;
            int64_t i;
            auto result = nobson::convert(decimal128, &i);

            if (result == nobson::ConversionResult::OK)
            {
                builder.append(i);
            }
            else
            {
                handle_decimal128_error(builder, decimal128, result, on_error);
            }
        }
        break;

    case bsoncxx::type::k_double:
        builder.append((int64_t)value.get_double());
        break;

    case bsoncxx::type::k_int32:
        builder.append((int64_t)value.get_int32());
        break;

    case bsoncxx::type::k_int64:
        builder.append(value.get_int64());
        break;

    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (!sv.empty() || isspace(sv.front()))
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Failed to parse number '" << sv
                       << "' in $convert with no onError value: Leading whitespace";

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }
            else
            {
                errno = 0;

                string s(sv);
                char* pEnd;
                long l = strtol(s.c_str(), &pEnd, 10);

                if (*pEnd != 0)
                {
                    if (!nobson::is_null(on_error))
                    {
                        builder.append(on_error);
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Did not consume whole string.";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }
                }
                else if (errno == ERANGE)
                {
                    if (!nobson::is_null(on_error))
                    {
                        builder.append(on_error);
                    }
                    else
                    {
                        stringstream ss;
                        ss << "Failed to parse number '" << sv
                           << "' in $convert with no onError value: Out of range";

                        throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                    }
                }
                else
                {
                    builder.append((int64_t)l);
                }
            }
        }
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_int64, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_oid(ArrayBuilder& builder,
                                      bsoncxx::types::value value,
                                      bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

    switch (value.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view sv = value.get_utf8();

            if (sv.length() != 24)
            {
                if (!nobson::is_null(on_error))
                {
                    builder.append(on_error);
                }
                else
                {
                    stringstream ss;
                    ss << "Failed to parse objectId '" << sv << "' in $convert with no onError value: "
                       << "Invalid string length for parsing to OID, expected 24 but found "
                       << sv.length();

                    throw SoftError(ss.str(), error::CONVERSION_FAILURE);
                }
            }

            builder.append(bsoncxx::oid(sv));
        }
        break;

    default:
        handle_default_case(builder, value.type(), bsoncxx::type::k_oid, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
}

//static
bsoncxx::types::value Convert::to_string(ArrayBuilder& builder,
                                         bsoncxx::types::value value,
                                         bsoncxx::types::value on_error)
{
    mxb_assert(builder.view().empty());

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
        handle_default_case(builder, value.type(), bsoncxx::type::k_utf8, on_error);
    }

    auto view = builder.view();
    mxb_assert(!view.empty());

    return (*view.begin()).get_value();
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
void Convert::handle_decimal128_error(ArrayBuilder& builder,
                                      bsoncxx::decimal128 decimal128,
                                      nobson::ConversionResult result,
                                      bsoncxx::types::value on_error)
{
    if (!nobson::is_null(on_error))
    {
        builder.append(on_error);
    }
    else if (result == nobson::ConversionResult::OVERFLOW)
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

//static
void Convert::handle_default_case(ArrayBuilder& builder,
                                  bsoncxx::type from,
                                  bsoncxx::type to,
                                  bsoncxx::types::value on_error)
{
    if (!nobson::is_null(on_error))
    {
        builder.append(on_error);
    }
    else
    {
        stringstream ss;
        ss << "$convert cannot convert a "
           << bsoncxx::to_string(from) << " to a(n) " << bsoncxx::to_string(to);
        throw SoftError(ss.str(), error::BAD_VALUE);
    }
}

/**
 * Divide
 */
Divide::Divide(bsoncxx::types::value value)
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

bsoncxx::types::value Divide::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    m_builder.clear();

    bsoncxx::types::value lhs = m_ops[0]->process(doc);
    bsoncxx::types::value rhs = m_ops[1]->process(doc);

    if (!nobson::is_number(lhs) || !nobson::is_number(rhs))
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

    switch (lhs.type())
    {
    case bsoncxx::type::k_int32:
        switch (rhs.type())
        {
        case bsoncxx::type::k_int32:
            m_builder.append(lhs.get_int32() / rhs.get_int32());
            break;

        case bsoncxx::type::k_int64:
            m_builder.append(lhs.get_int32() / rhs.get_int64());
            break;

        case bsoncxx::type::k_double:
            m_builder.append(lhs.get_int32() / rhs.get_double());
            break;

        default:
            mxb_assert(!true);
        }
        break;

    case bsoncxx::type::k_int64:
        switch (rhs.type())
        {
        case bsoncxx::type::k_int32:
            m_builder.append(lhs.get_int64() / rhs.get_int32());
            break;

        case bsoncxx::type::k_int64:
            m_builder.append(lhs.get_int64() / rhs.get_int64());
            break;

        case bsoncxx::type::k_double:
            m_builder.append(lhs.get_int64() / rhs.get_double());
            break;

        default:
            mxb_assert(!true);
        }
        break;

    case bsoncxx::type::k_double:
        switch (rhs.type())
        {
        case bsoncxx::type::k_int32:
            m_builder.append(lhs.get_double() / rhs.get_int32());
            break;

        case bsoncxx::type::k_int64:
            m_builder.append(lhs.get_double() / rhs.get_int64());
            break;

        case bsoncxx::type::k_double:
            m_builder.append(lhs.get_double() / rhs.get_double());
            break;

        default:
            mxb_assert(!true);
        }
        break;

    default:
        mxb_assert(!true);
    }

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();

    return m_value;
}

/**
 * First
 */
First::First(bsoncxx::types::value value)
    : m_field(value)
{
}

bsoncxx::types::value First::process(bsoncxx::document::view doc)
{
    if (!ready())
    {
        m_field.process(doc);
        m_value = m_field.value();

        set_ready();
    }

    return m_value;
}

/**
 * Max
 */
Max::Max(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value Max::process(bsoncxx::document::view doc)
{
    bsoncxx::types::value value = m_sOp->process(doc);

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
 * Multiply
 */
Multiply::Multiply(bsoncxx::types::value value)
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

bsoncxx::types::value Multiply::process(bsoncxx::document::view doc)
{
    std::optional<Number> result;

    for (auto& sOp : m_ops)
    {
        bsoncxx::types::value value = sOp->process(doc);

        if (nobson::is_number(value))
        {
            if (result)
            {
                result = mul(result.value(), get_number(value));
            }
            else
            {
                result = get_number(value);
            }
        }
    }

    if (result)
    {
        m_builder.clear();

        auto value = result.value();

        if (std::holds_alternative<int32_t>(value))
        {
            m_builder.append(std::get<int32_t>(value));
        }
        else if (std::holds_alternative<int64_t>(value))
        {
            m_builder.append(std::get<int64_t>(value));
        }
        else
        {
            mxb_assert(std::holds_alternative<double>(value));

            m_builder.append(std::get<double>(value));
        }

        auto array = m_builder.view();

        m_value = (*array.begin()).get_value();
    }

    return m_value;
}

/**
 * Ne
 */
Ne::Ne(bsoncxx::types::value value)
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

bsoncxx::types::value Ne::process(bsoncxx::document::view doc)
{
    mxb_assert(m_ops.size() == 2);

    m_builder.clear();

    bsoncxx::types::value lhs = m_ops[0]->process(doc);
    bsoncxx::types::value rhs = m_ops[1]->process(doc);

    m_builder.append(lhs != rhs);

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();

    return m_value;
}

/**
 * Sum
 */
Sum::Sum(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value Sum::process(bsoncxx::document::view doc)
{
    bsoncxx::types::value value = m_sOp->process(doc);

    if (m_builder.view().empty())
    {
        m_builder.append(value);
        m_value = (*m_builder.view().begin()).get_value();
    }
    else
    {
        switch (value.type())
        {
        case bsoncxx::type::k_int32:
            add_int32(value.get_int32());
            break;

        case bsoncxx::type::k_int64:
            add_int64(value.get_int64());
            break;

        case bsoncxx::type::k_double:
            add_double(value.get_double());
            break;

        default:
            break;
        }
    }

    return m_value;
}

void Sum::add_int32(int32_t r)
{
    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();
            m_builder.clear();

            if (std::numeric_limits<int32_t>::max() - r > l)
            {
                m_builder.append((int64_t)l + r);
            }
            else
            {
                m_builder.append(l + r);
            }
        }
        break;

    case bsoncxx::type::k_int64:
        {
            auto l = m_value.get_int64();
            m_builder.clear();

            if (std::numeric_limits<int64_t>::max() - r > l)
            {
                m_builder.append((double)l + r);
            }
            else
            {
                m_builder.append(l + r);
            }
        }
        break;

    case bsoncxx::type::k_double:
        {
            auto l = m_value.get_double();
            m_builder.clear();

            m_builder.append(l + r);
        }
        break;

    default:
        mxb_assert(!true);
    }

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();
}

void Sum::add_int64(int64_t r)
{
    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();
            m_builder.clear();

            m_builder.append((int64_t)l + r);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            auto l = m_value.get_int64();
            m_builder.clear();

            if (std::numeric_limits<int64_t>::max() - r > l)
            {
                m_builder.append((double)l + r);
            }
            else
            {
                m_builder.append(l + r);
            }
        }
        break;

    case bsoncxx::type::k_double:
        {
            auto l = m_value.get_double();
            m_builder.clear();

            m_builder.append(l + r);
        }
        break;

    default:
        mxb_assert(!true);
    }

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();
}

void Sum::add_double(double r)
{
    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();
            m_builder.clear();

            m_builder.append((double)l + r);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            auto l = m_value.get_int64();
            m_builder.clear();

            m_builder.append((double)l + r);
        }
        break;

    case bsoncxx::type::k_double:
        {
            auto l = m_value.get_double();
            m_builder.clear();

            m_builder.append(l + r);
        }
        break;

    default:
        mxb_assert(!true);
    }

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();
}

/**
 * ToBool
 */
ToBool::ToBool(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToBool::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_bool(m_builder, m_sOp->process(doc));
}

/**
 * ToDate
 */
ToDate::ToDate(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToDate::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_date(m_builder, m_sOp->process(doc));
}

/**
 * ToDecimal
 */
ToDecimal::ToDecimal(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToDecimal::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_decimal(m_builder, m_sOp->process(doc));
}

/**
 * ToDouble
 */
ToDouble::ToDouble(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToDouble::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_double(m_builder, m_sOp->process(doc));
}

/**
 * ToInt
 */
ToInt::ToInt(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToInt::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_int32(m_builder, m_sOp->process(doc));
}

/**
 * ToLong
 */
ToLong::ToLong(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToLong::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_int64(m_builder, m_sOp->process(doc));
}

/**
 * ToObjectId
 */
ToObjectId::ToObjectId(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToObjectId::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_oid(m_builder, m_sOp->process(doc));
}

/**
 * ToString
 */
ToString::ToString(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

bsoncxx::types::value ToString::process(bsoncxx::document::view doc)
{
    m_builder.clear();
    return Convert::to_string(m_builder, m_sOp->process(doc));
}

}

}
