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

/** This could arguably be a utility, but is written specifically for rwsplit
 *  so it stays here at least for now.
 */
namespace maxscale
{
/**
 * Query response statistics. Uses median of N samples to filter noise, then
 * uses those medians to calculate the average response time.
 * The class makes an average of the durations between calls to query_started()
 * and query_ended(). Once the stats are good, sync_time_reached(int max) returns true,
 * based on the average containing at least max samples (or medians), or the time
 * sync_duration (constructor arg) has passed since the last reset().
 */
class ResponseStat
{
public:
    /*
     * @param num_filter_samples - collect num samples, use median
     * @param num_synch_medians  - this many medians before the average should be synced, or
     * @param sync_duration      - this much time between syncs.
     */
    ResponseStat(int num_filter_samples = 5,
                 int num_synch_medians = 500,
                 maxbase::Duration sync_duration = std::chrono::seconds(5));

    void              query_started();
    void              query_ended();// ok to call without a query_started
    bool              make_valid(); // make valid even if there are too few filter_samples
    bool              is_valid() const;
    int               num_samples() const;
    maxbase::Duration average() const;
    bool              sync_time_reached();  // is it time to apply the average?
    void              reset();
private:
    const int                      m_num_filter_samples;
    const int                      m_num_synch_medians;
    const maxbase::Duration        m_sync_duration;
    int                            m_sample_count;
    std::vector<maxbase::Duration> m_samples;   // N sampels from which median is used
    maxbase::CumulativeAverage     m_average;
    maxbase::TimePoint             m_last_start;
    maxbase::TimePoint             m_next_sync;
};
}
