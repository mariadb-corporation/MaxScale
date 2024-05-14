/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>

#include <atomic>

namespace maxbase
{
namespace stats
{

// Tracks the maximum and average value of a statistic
template<class T>
class Value
{
public:
    using value_type = T;

    template<class Value>
    void track(Value val)
    {
        double alpha = 0.04;        // Same as the alpha for the response time average
        double old_avg = m_avg.load(std::memory_order_relaxed);

        while (!m_avg.compare_exchange_weak(
            old_avg, old_avg * (1.0 - alpha) + val * alpha, std::memory_order_relaxed))
        {
        }

        auto old_max = m_max.load(std::memory_order_relaxed);

        while (val > old_max
                // Updates old_max if it's not equal to the expected value.
               && !m_max.compare_exchange_weak(old_max, val, std::memory_order_acq_rel))
        {
        }
    }

    value_type max() const
    {
        return m_max.load(std::memory_order_relaxed);
    }

    value_type avg() const
    {
        return m_avg.load(std::memory_order_relaxed);
    }

private:
    std::atomic<value_type> m_max {0};
    std::atomic<value_type> m_avg {0};
};
}
}
