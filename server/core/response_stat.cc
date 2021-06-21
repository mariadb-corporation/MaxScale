/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/response_stat.hh>

#include <algorithm>
#include <maxbase/worker.hh>
#include <maxbase/stopwatch.hh>

namespace maxscale
{
ResponseStat::ResponseStat(Target* target, int num_filter_samples,
                           maxbase::Duration sync_duration)
    : m_target(target)
    , m_num_filter_samples {num_filter_samples}
    , m_sync_duration{sync_duration}
    , m_sample_count{0}
    , m_samples(num_filter_samples)
    , m_last_start{maxbase::TimePoint()}
    , m_next_sync{maxbase::Clock::now(maxbase::NowType::EPollTick) + sync_duration}
{
}

ResponseStat::~ResponseStat()
{
    sync(true);
}


void ResponseStat::query_started()
{
    m_last_start = maxbase::Clock::now(maxbase::NowType::EPollTick);
}

void ResponseStat::query_finished()
{
    if (m_last_start == maxbase::TimePoint())
    {
        // m_last_start is defaulted. Ignore, avoids extra logic at call sites.
        return;
    }

    m_samples[m_sample_count] = maxbase::Clock::now(maxbase::NowType::EPollTick) - m_last_start;

    if (++m_sample_count == m_num_filter_samples)
    {
        std::sort(m_samples.begin(), m_samples.end());
        maxbase::Duration new_sample = m_samples[m_num_filter_samples / 2];
        m_average.add(mxb::to_secs(new_sample));
        m_sample_count = 0;
    }

    m_last_start = maxbase::TimePoint();
}

void ResponseStat::sync()
{
    sync(false);
}

void ResponseStat::sync(bool last_call)
{
    bool sync_reached = sync_time_reached();

    if (sync_reached || last_call)
    {
        if (is_valid())
        {
            m_target->response_time_add(m_average.average(), m_average.num_samples());
            m_synced = true;
            reset();
        }
        else if (sync_reached || !m_synced)
        {
            m_synced = true;
            m_target->response_time_add(m_target->ping() / 1000000.0, 1);
            reset();
        }
    }
}

bool ResponseStat::is_valid() const
{
    return m_average.num_samples();
}

bool ResponseStat::sync_time_reached()
{
    auto now = maxbase::Clock::now(maxbase::NowType::EPollTick);
    bool reached = m_next_sync < now;

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
    m_next_sync = maxbase::Clock::now(maxbase::NowType::EPollTick) + m_sync_duration;
}
}
