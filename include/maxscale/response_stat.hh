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
#pragma once

#include <maxscale/ccdefs.hh>
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
    ResponseStat(int num_filter_samples = 9,
                 maxbase::Duration sync_duration = std::chrono::milliseconds(250));

    void              query_started();
    void              query_ended();    // ok to call without a query_started
    bool              make_valid();     // make valid even if there are only filter_samples
    bool              is_valid() const;
    int               num_samples() const;
    maxbase::Duration average() const;
    bool              sync_time_reached();  // is it time to apply the average to the server?
    void              reset();

private:
    const int                      m_num_filter_samples;
    const maxbase::Duration        m_sync_duration;
    int                            m_sample_count;
    std::vector<maxbase::Duration> m_samples;   // N sampels from which median is used
    maxbase::CumulativeAverage     m_average;
    maxbase::TimePoint             m_last_start;
    maxbase::TimePoint             m_next_sync;
};
}
