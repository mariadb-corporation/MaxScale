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
#include <sstream>

using namespace std;

namespace nosql
{

namespace aggregation
{

/**
 * Operator
 */
Operator::~Operator()
{
}

/**
 * Operator:::Field
 */
Operator::Field::Field(string_view field)
{
    if (field.empty() || field.front() != '$')
    {
        m_kind = Kind::LITERAL;
        m_fields.emplace_back(string(field));
    }
    else
    {
        size_t from = 1; // Skip the '$'
        auto to = field.find_first_of('.');

        do
        {
            if (to == string_view::npos)
            {
                m_fields.emplace_back(string(field.substr(from)));
            }
            else
            {
                m_fields.emplace_back(string(field.substr(from, to - from)));
                from = to + 1;
            }
        }
        while (to != string_view::npos);
    }
}

mxb::Json Operator::Field::access(const mxb::Json& doc)
{
    mxb::Json rv;

    if (m_kind == Kind::LITERAL)
    {
        rv = std::move(mxb::json::String(m_fields.front()));
    }
    else
    {
        auto it = m_fields.begin();
        do
        {
            rv = doc.get_object(*it);
            ++it;
        }
        while (rv && it != m_fields.end());
    }

    return rv;
}

/**
 * First
 */
namespace
{

string_view element_to_string_view(bsoncxx::document::element field)
{
    if (field.type() != bsoncxx::type::k_utf8)
    {
        stringstream ss;
        ss << "Value of '" << field.key() << "' must be a string";

        throw SoftError(ss.str(), nosql::error::TYPE_MISMATCH);
    }

    return field.get_utf8();
}

}

First::First(bsoncxx::document::element field)
    : m_field(element_to_string_view(field))
{
}

void First::process(const mxb::Json& doc)
{
    if (!ready())
    {
        m_value = m_field.access(doc);

        set_ready();
    }
}

void First::update(mxb::Json& doc, const std::string& field)
{
    MXB_AT_DEBUG(bool rv=) doc.set_object(field.c_str(), m_value);
    mxb_assert(rv);
}

}
}
