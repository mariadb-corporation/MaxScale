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

#include "nosqlaccumulationoperator.hh"
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

namespace accumulation
{

/**
 * Operator
 */
Operator::~Operator()
{
}

const bsoncxx::types::bson_value::value& Operator::finish()
{
    return value();
}

/**
 * Avg
 */
void Avg::accumulate(bsoncxx::document::view doc)
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
}

/**
 * First
 */
void First::accumulate(bsoncxx::document::view doc)
{
    if (!ready())
    {
        m_value = m_sOp->process(doc);

        set_ready();
    }
}

/**
 * Last
 */
void Last::accumulate(bsoncxx::document::view doc)
{
    // TODO: The position of the doc should be passed, no point in
    // TODO: processing and assigning at every stage.
    m_value = m_sOp->process(doc);
}

/**
 * Max
 */
void Max::accumulate(bsoncxx::document::view doc)
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
}

/**
 * Min
 */
void Min::accumulate(bsoncxx::document::view doc)
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
}

/**
 * Push
 */
void Push::accumulate(bsoncxx::document::view doc)
{
    m_builder.append(m_sOp->process(doc));
}

const Operator::BsonValue& Push::finish()
{
    if (nobson::is_null(m_value))
    {
        m_value = m_builder.extract().view();
    }
    else
    {
        ArrayBuilder builder;

        // First copy existing elements.
        bsoncxx::array::view array = m_value.view().get_array();
        for (const auto& element : array)
        {
            builder.append(element.get_value());
        }

        // Then append new elements.
        array = builder.extract().view();
        for (const auto& element : array)
        {
            builder.append(element.get_value());
        }

        m_value = builder.extract().view();
    }

    return m_value;
}

/**
 * Sum
 */
void Sum::accumulate(bsoncxx::document::view doc)
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
}

}

}
