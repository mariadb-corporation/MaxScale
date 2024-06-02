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
unique_ptr<Operator> Operator::get(bsoncxx::document::element element)
{
    auto it = operators.find(element.key());

    if (it == operators.end())
    {
        stringstream ss;
        ss << "Unrecognized expression '" << element.key() << "'";

        throw SoftError(ss.str(), error::INVALID_PIPELINE_OPERATOR);
    }

    return it->second(element);
}

/**
 * Field
 */
Field::Field(bsoncxx::document::element element)
{
    if (element.type() != bsoncxx::type::k_utf8)
    {
        stringstream ss;
        ss << "Value of '" << element.key() << "' must be a string";

        throw SoftError(ss.str(), nosql::error::TYPE_MISMATCH);
    }

    string_view field = element.get_utf8();

    if (field.empty() || field.front() != '$')
    {
        m_kind = Kind::LITERAL;
        m_value = mxb::json::String(field);
    }
    else
    {
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
}

//static
unique_ptr<Operator> Field::create(bsoncxx::document::element element)
{
    return make_unique<Field>(element);
}

mxb::Json Field::process(const mxb::Json& doc)
{
    if (m_kind != Kind::LITERAL)
    {
        auto it = m_fields.begin();
        do
        {
            m_value = doc.get_object(*it);
            ++it;
        }
        while (m_value && it != m_fields.end());
    }

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
{
    mxb_assert(!true);
}

//static
unique_ptr<Operator> Sum::create(bsoncxx::document::element element)
{
    return make_unique<Sum>(element);
}

mxb::Json Sum::process(const mxb::Json& doc)
{
    mxb_assert(!true);
    return mxb::Json();
}

}

}
