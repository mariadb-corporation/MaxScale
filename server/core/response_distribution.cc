/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/assert.h>
#include <maxscale/response_distribution.hh>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace maxscale
{

ResponseDistribution::ResponseDistribution(int range_base)
    : m_range_base(range_base)
{
    mxb_assert(range_base >= 2);

    double lowest_bucket = 1e-6;
    int exponent = round(log(lowest_bucket)) / log(m_range_base);

    for (auto bucket = pow(m_range_base, exponent);
         bucket < 1e6;
         bucket = pow(range_base, ++exponent))
    {
        auto slimit = round(bucket * 1e6) / 1e6;
        if (slimit < 0.9e-6)    // 1e-6 - epsilon
        {
            continue;
        }
        mxb::Duration limit = mxb::from_secs(slimit);
        m_elements.emplace_back(Element {limit, 0, 0s});
    }
}

const std::vector<ResponseDistribution::Element>& ResponseDistribution::get() const
{
    return m_elements;
}

int ResponseDistribution::range_base() const
{
    return m_range_base;
}

ResponseDistribution ResponseDistribution::with_stats_reset() const
{
    ResponseDistribution ret(*this);
    for (auto& element : ret.m_elements)
    {
        element.count = 0;
        element.total = 0s;
    }

    return ret;
}

ResponseDistribution& ResponseDistribution::operator+(const ResponseDistribution& rhs)
{
    mxb_assert(m_elements.size() == rhs.m_elements.size());

    for (size_t i = 0; i < m_elements.size(); ++i)
    {
        m_elements[i].count += rhs.m_elements[i].count;
        m_elements[i].total += rhs.m_elements[i].total;
    }

    return *this;
}

ResponseDistribution& ResponseDistribution::operator+=(const ResponseDistribution& rhs)
{
    this-> operator+(rhs);
    return *this;
}
}
