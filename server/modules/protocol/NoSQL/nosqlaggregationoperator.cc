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

namespace nosql
{

namespace aggregation
{

namespace
{

map<string, Operator::Creator, less<>> operators =
{
    { First::NAME, First::create },
    { Sum::NAME, Sum::create },
};

}


/**
 * Operator
 */
Operator::~Operator()
{
}

//static
std::unique_ptr<Operator> Operator::unsupported(bsoncxx::document::element element)
{
    stringstream ss;
    ss << "Unsupported operator '" << element.key() << "'";

    throw SoftError(ss.str(), error::INTERNAL_ERROR);

    return unique_ptr<Operator>();
}

//static
unique_ptr<Operator> Operator::create(bsoncxx::document::element element)
{
    unique_ptr<Operator> sOp;

    switch (element.type())
    {
    case bsoncxx::type::k_utf8:
        {
            string_view s = element.get_utf8();

            if (!s.empty() && s.front() == '$')
            {
                sOp = Accessor::create(element);
            }
            else
            {
                sOp = Literal::create(element);
            }
        }
        break;

    case bsoncxx::type::k_document:
        {
            bsoncxx::document::view doc = element.get_document();

            auto it = doc.begin();

            if (it == doc.end())
            {
                sOp = Literal::create(element);
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

                sOp = jt->second(op);
            }
        }
        break;

        // TODO: bsoncxx::type::k_array will need specific handling.

    default:
        sOp = Literal::create(element);
    }

    return sOp;
}

/**
 * Accessor
 */
Accessor::Accessor(bsoncxx::document::element element)
{
    mxb_assert(element.type() == bsoncxx::type::k_utf8);

    string_view field = element.get_utf8();

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
unique_ptr<Operator> Accessor::create(bsoncxx::document::element element)
{
    return make_unique<Accessor>(element);
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
Literal::Literal(bsoncxx::document::element element)
    : Operator(element_to_json(element))
{
}

//static
std::unique_ptr<Operator> Literal::create(bsoncxx::document::element element)
{
    return make_unique<Literal>(element);
}

mxb::Json Literal::process(const mxb::Json& doc)
{
    return m_value;
}

/**
 * First
 */
First::First(bsoncxx::document::element element)
    : m_field(element)
{
}

//static
unique_ptr<Operator> First::create(bsoncxx::document::element element)
{
    return make_unique<First>(element);
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
 * Sum
 */
Sum::Sum(bsoncxx::document::element element)
    : m_sOp(Operator::create(element))
{
}

//static
unique_ptr<Operator> Sum::create(bsoncxx::document::element element)
{
    return make_unique<Sum>(element);
}

mxb::Json Sum::process(const mxb::Json& doc)
{
    mxb::Json rv = m_sOp->process(doc);

    if (!m_value)
    {
        m_value = rv;
    }
    else
    {
        // TODO: Sum the value!
        m_value = rv;
    }

    return m_value;
}

}

}

