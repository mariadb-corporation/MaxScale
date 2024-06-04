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
#include <map>
#include <optional>
#include <sstream>

using namespace std;
namespace json = mxb::json;

namespace nosql
{

namespace aggregation
{

namespace
{

map<string, Operator::Creator, less<>> operators =
{
    { First::NAME, First::create },
    { Multiply::NAME, Multiply::create },
    { Sum::NAME, Sum::create },
    { ToDouble::NAME, ToDouble::create },
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

bool is_real(bsoncxx::types::value v)
{
    return v.type() == bsoncxx::type::k_double;
}

bool is_numeric(bsoncxx::types::value v)
{
    bool rv = false;

    switch (v.type())
    {
    case bsoncxx::type::k_double:
    case bsoncxx::type::k_int32:
    case bsoncxx::type::k_int64:
        rv = true;

    default:
        ;
    }

    return rv;
}

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

//static
unique_ptr<Operator> Accessor::create(bsoncxx::types::value value)
{
    return make_unique<Accessor>(value);
}

bsoncxx::types::value Accessor::process(bsoncxx::document::view doc)
{
    bsoncxx::types::value value;

    bsoncxx::document::element element;

    auto it = m_fields.begin();
    do
    {
        element = doc[*it];

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
    : Operator(value)
{
}

//static
std::unique_ptr<Operator> Literal::create(bsoncxx::types::value value)
{
    return make_unique<Literal>(value);
}

bsoncxx::types::value Literal::process(bsoncxx::document::view doc)
{
    return m_value;
}

/**
 * First
 */
First::First(bsoncxx::types::value value)
    : m_field(value)
{
}

//static
unique_ptr<Operator> First::create(bsoncxx::types::value value)
{
    return make_unique<First>(value);
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
 * Multiply
 */
Multiply::Multiply(bsoncxx::types::value value)
{
    switch (value.type())
    {
    case bsoncxx::type::k_double:
    case bsoncxx::type::k_int32:
    case bsoncxx::type::k_int64:
        m_sOps.emplace_back(Literal::create(value));
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
                    m_sOps.emplace_back(Literal::create(item.get_value()));
                    break;

                case bsoncxx::type::k_utf8:
                    {
                        string_view s = item.get_utf8();

                        if (!s.empty() && s.front() == '$')
                        {
                            m_sOps.emplace_back(Operator::create(item.get_value()));
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
                m_sOps.emplace_back(Operator::create(value));
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

//static
std::unique_ptr<Operator> Multiply::create(bsoncxx::types::value value)
{
    return make_unique<Multiply>(value);
}

bsoncxx::types::value Multiply::process(bsoncxx::document::view doc)
{
    std::optional<Number> result;

    for (auto& sOp : m_sOps)
    {
        bsoncxx::types::value value = sOp->process(doc);

        if (is_numeric(value))
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
 * Sum
 */
Sum::Sum(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

//static
unique_ptr<Operator> Sum::create(bsoncxx::types::value value)
{
    return make_unique<Sum>(value);
}

bsoncxx::types::value Sum::process(bsoncxx::document::view doc)
{
    bsoncxx::types::value value = m_sOp->process(doc);

    switch (value.type())
    {
    case bsoncxx::type::k_int32:
        if (m_builder.view().empty())
        {
            m_value = value;
        }
        else
        {
            add_int32(value.get_int32());
        }
        break;

    case bsoncxx::type::k_int64:
        if (m_builder.view().empty())
        {
            m_value = value;
        }
        else
        {
            add_int64(value.get_int64());
        }
        break;

    case bsoncxx::type::k_double:
        if (m_builder.view().empty())
        {
            m_value = value;
        }
        else
        {
            add_int64(value.get_int64());
        }
        break;

    default:
        break;
    }

    return m_value;
}

void Sum::add_int32(int32_t r)
{
    m_builder.clear();

    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();

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
    m_builder.clear();

    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();

            m_builder.append((int64_t)l + r);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            auto l = m_value.get_int64();

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
    m_builder.clear();

    switch (m_value.type())
    {
    case bsoncxx::type::k_int32:
        {
            auto l = m_value.get_int32();

            m_builder.append((double)l + r);
        }
        break;

    case bsoncxx::type::k_int64:
        {
            auto l = m_value.get_int64();

            m_builder.append((double)l + r);
        }
        break;

    case bsoncxx::type::k_double:
        {
            auto l = m_value.get_double();

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
 * ToDouble
 */
ToDouble::ToDouble(bsoncxx::types::value value)
    : m_sOp(Operator::create(value))
{
}

//static
unique_ptr<Operator> ToDouble::create(bsoncxx::types::value value)
{
    return make_unique<ToDouble>(value);
}

bsoncxx::types::value ToDouble::process(bsoncxx::document::view doc)
{
    m_builder.clear();

    // TODO: 1) Implement $convert and use that.
    // TODO: 2) If an object, check whether it is a Decimal or a Date and act accordingly.

    bsoncxx::types::value value = m_sOp->process(doc);

    switch (value.type())
    {
    case bsoncxx::type::k_int32:
        m_builder.append((double)value.get_int32());
        break;

    case bsoncxx::type::k_int64:
        m_builder.append((double)value.get_int64());
        break;

    case bsoncxx::type::k_double:
        m_builder.append(value);
        break;

    case bsoncxx::type::k_utf8:
        {
            errno = 0;
            string s(static_cast<string_view>(value.get_string()));
            const char* z = s.c_str();

            if (*z != 0)
            {
                char* end;
                long l = strtol(z, &end, 10);

                if (errno == 0 && *end == 0)
                {
                    m_builder.append((double)l);
                    break;
                }
            }
        }
        [[fallthrough]];
    default:
        {
            stringstream ss;
            ss << "Failed to optimize pipeline :: caused by :: Unsupported conversion from "
               << bsoncxx::to_string(value.type())
               << " to double in $convert with no onError value";

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
    }

    auto array = m_builder.view();
    m_value = (*array.begin()).get_value();

    return m_value;
}

}

}
