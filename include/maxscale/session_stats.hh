/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>


#include <unordered_map>

#include <maxscale/server.hh>
#include <maxbase/average.hh>
#include <maxbase/stopwatch.hh>

namespace maxscale
{

/** SessionStats is a class holding statistics associated with a session */
class SessionStats
{

public:
    struct CurrentStats
    {
        maxbase::Duration ave_session_dur;
        double            ave_session_active_pct;
        int64_t           ave_session_selects;
        int64_t           total_queries;
        int64_t           total_read_queries;
        int64_t           total_write_queries;
    };

    void update(maxbase::Duration sess_duration,
                maxbase::Duration active_duration,
                int64_t num_selects);

    void inc_total()
    {
        ++m_total;
    }

    void inc_read()
    {
        ++m_read;
    }

    void inc_write()
    {
        ++m_write;
    }

    int64_t total() const
    {
        return m_total;
    }

    int64_t read() const
    {
        return m_read;
    }

    int64_t write() const
    {
        return m_write;
    }

    CurrentStats current_stats() const;

    SessionStats& operator+=(const SessionStats& rhs);

private:
    int64_t m_total = 0;
    int64_t m_read = 0;
    int64_t m_write = 0;

    maxbase::CumulativeAverage m_ave_session_dur;
    maxbase::CumulativeAverage m_ave_active_dur;
    maxbase::CumulativeAverage m_num_ave_session_selects;
};

using TargetSessionStats = std::unordered_map<mxs::Target*, SessionStats>;
}
