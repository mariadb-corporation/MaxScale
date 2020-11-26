/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/target.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/average.hh>

namespace maxscale
{
/**
 * Query response time average for a backend. Uses median of N samples to filter noise,
 * then uses those medians to calculate the average response time.
 */
class ResponseStat
{
public:
    /*
     * @param num_filter_samples - collect num samples, use median (digital filter)
     * @param sync_duration      - this much time between synchronize to server stats.
     */
    ResponseStat(Target* target, int num_filter_samples, maxbase::Duration sync_duration);
    ~ResponseStat();

    void query_started();
    void query_finished();  // ok to call without a query_started()
    void sync();            // update server EMA if needed
private:
    void sync(bool last_call);
    bool is_valid() const;
    bool sync_time_reached();   // is it time to apply the average to the server?
    void reset();

    Target*                        m_target;
    const int                      m_num_filter_samples;
    const maxbase::Duration        m_sync_duration;
    long                           m_sample_count;
    std::vector<maxbase::Duration> m_samples;   // N sampels from which median is used
    maxbase::CumulativeAverage     m_average;
    maxbase::TimePoint             m_last_start;
    maxbase::TimePoint             m_next_sync;
    bool                           m_synced {true};
};
}
