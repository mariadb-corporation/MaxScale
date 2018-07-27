/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "response_stat.hh"

#include <algorithm>

namespace maxscale
{
ResponseStat::ResponseStat(int ignore_first_n, int num_filter_samples,
                           maxbase::Duration sync_duration) :
    m_ignore_first_n{ignore_first_n},
    m_num_filter_samples {num_filter_samples},
    m_sync_duration{sync_duration},
    m_sample_count{0},
    m_samples(num_filter_samples),
    m_last_start{maxbase::TimePoint()},
    m_next_sync{maxbase::Clock::now() + sync_duration}
{}


void ResponseStat::query_started()
{
    if (m_ignore_first_n)
    {
        --m_ignore_first_n;
        return;
    }
    m_last_start = maxbase::Clock::now();
}

void ResponseStat::query_ended()
{
    if (m_last_start == maxbase::TimePoint())
    {
        // m_last_start is defaulted. Ignore, avoids extra logic in call sites.
        return;
    }
    m_samples[m_sample_count] = maxbase::Clock::now() - m_last_start;

    if (++m_sample_count == m_num_filter_samples)
    {
        std::sort(begin(m_samples), end(m_samples));
        maxbase::Duration new_sample = m_samples[m_num_filter_samples / 2];
        m_average.add(std::chrono::duration<double>(new_sample).count());
        m_sample_count = 0;
    }
    m_last_start = maxbase::TimePoint();
}

bool ResponseStat::is_valid() const
{
    return m_average.num_samples();
}

int ResponseStat::num_samples() const
{
    return m_average.num_samples();
}

maxbase::Duration ResponseStat::average() const
{
    return maxbase::Duration(m_average.average());
}

bool ResponseStat::sync_time_reached(int num_synch_medians)
{
    auto now = maxbase::Clock::now();
    bool reached = m_next_sync < now || m_average.num_samples() >= num_synch_medians;
    if (reached)
    {
        m_next_sync = now + m_sync_duration;
    }
    return reached;
}

void ResponseStat::reset()
{
    m_sample_count = 0;
    m_average.reset();
    m_next_sync = maxbase::Clock::now() + m_sync_duration;
}

}

