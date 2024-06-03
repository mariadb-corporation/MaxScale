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

mxb::Json Operator::mul(const mxb::Json& lhs, const mxb::Json& rhs)
{
    json::Undefined rv;

    bool lhs_is_numeric = lhs.is_numeric();
    bool rhs_is_numeric = rhs.is_numeric();

    if (lhs_is_numeric && !rhs_is_numeric)
    {
        rv = lhs;
    }
    else if (!lhs_is_numeric && rhs_is_numeric)
    {
        rv = rhs;
    }
    else if (lhs_is_numeric && rhs_is_numeric)
    {
        bool lhs_is_real = lhs.is_real();
        bool rhs_is_real = rhs.is_real();

        if (lhs_is_real || rhs_is_real)
        {
            double l = lhs_is_real ? lhs.get_real() : lhs.get_int();
            double r = rhs_is_real ? rhs.get_real() : rhs.get_int();

            rv = json::Real(l * r);
        }
        else
        {
            rv = json::Integer(lhs.get_int() * rhs.get_int());
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

mxb::Json Accessor::process(const mxb::Json& doc)
{
    m_value = doc;

    auto it = m_fields.begin();
    do
    {
        m_value = m_value.get_object(*it);
        ++it;
    }
    while (m_value && it != m_fields.end());

    return m_value;
}

/**
 * Literal
 */
Literal::Literal(bsoncxx::types::value value)
    : Operator(bson_to_json(value))
{
}

//static
std::unique_ptr<Operator> Literal::create(bsoncxx::types::value value)
{
    return make_unique<Literal>(value);
}

mxb::Json Literal::process(const mxb::Json& doc)
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

mxb::Json First::process(const mxb::Json& doc)
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

mxb::Json Multiply::process(const mxb::Json& doc)
{
    for (auto& sOp : m_sOps)
    {
        m_value = mul(m_value, sOp->process(doc));
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

mxb::Json Sum::process(const mxb::Json& doc)
{
    mxb::Json rv = m_sOp->process(doc);

    switch (rv.type())
    {
    case mxb::Json::Type::INTEGER:
        if (!m_value)
        {
            m_value = rv;
        }
        else
        {
            add_integer(rv.get_int());
        }
        break;

    case mxb::Json::Type::REAL:
        if (!m_value)
        {
            m_value = rv;
        }
        else
        {
            add_real(rv.get_real());
        }
        break;

    default:
        break;
    }

    return m_value;
}

void Sum::add_integer(int64_t value)
{
    switch (m_value.type())
    {
    case mxb::Json::Type::INTEGER:
        m_value.set_int(m_value.get_int() + value);
        break;

    case mxb::Json::Type::REAL:
        m_value = mxb::json::Real(m_value.get_real() + value);
        break;

    default:
        mxb_assert(!true);
    }
}

void Sum::add_real(double value)
{
    switch (m_value.type())
    {
    case mxb::Json::Type::INTEGER:
        m_value = mxb::json::Real(m_value.get_int() + value);
        break;

    case mxb::Json::Type::REAL:
        m_value.set_real(m_value.get_real() + value);
        break;

    default:
        mxb_assert(!true);
    }
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

mxb::Json ToDouble::process(const mxb::Json& doc)
{
    // TODO: 1) Implement $convert and use that.
    // TODO: 2) If an object, check whether it is a Decimal or a Date and act accordingly.

    mxb::Json value = m_sOp->process(doc);

    switch (value.type())
    {
    case mxb::Json::Type::INTEGER:
        m_value = mxb::json::Real(value.get_int());
        break;

    case mxb::Json::Type::REAL:
        m_value = value;
        break;

    case mxb::Json::Type::BOOL:
        m_value = mxb::json::Real(value.get_bool() ? 1 : 0);
        break;

    case mxb::Json::Type::STRING:
        {
            errno = 0;
            auto s = value.get_string();
            const char* z = s.c_str();

            if (*z != 0)
            {
                char* end;
                long l = strtol(z, &end, 10);

                if (errno == 0 && *end == 0)
                {
                    m_value = mxb::json::Real(l);
                    break;
                }
            }
        }
        [[fallthrough]];
    case mxb::Json::Type::OBJECT:
    case mxb::Json::Type::ARRAY:
    case mxb::Json::Type::JSON_NULL:
    case mxb::Json::Type::UNDEFINED:
        {
            stringstream ss;
            ss << "Failed to optimize pipeline :: caused by :: Unsupported conversion from "
               << mxb::json::to_string(value.type())
               << " to double in $convert with no onError value";

            throw SoftError(ss.str(), error::CONVERSION_FAILURE);
        }
    }

    return m_value;
}

}

}
