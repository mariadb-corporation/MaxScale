/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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


#include <map>

#include <maxscale/server.hh>
#include <maxbase/average.hh>
#include <maxbase/stopwatch.hh>

namespace maxscale
{

/** ServerStats is a class to hold server statistics associated with a router */
class ServerStats
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

    void start_session();
    void end_session(maxbase::Duration sess_duration, maxbase::Duration active_duration, int64_t num_selects);

    CurrentStats current_stats() const;

    ServerStats& operator+=(const ServerStats& rhs);

    // These are exactly what they were in struct ServerStats, in readwritesplit.hh.
    // (well, but I changed uint64_t to int64_t).
    // TODO make these private, and add functions to modify them.
    int64_t total = 0;
    int64_t read = 0;
    int64_t write = 0;

private:
    maxbase::CumulativeAverage m_ave_session_dur;
    maxbase::CumulativeAverage m_ave_active_dur;
    maxbase::CumulativeAverage m_num_ave_session_selects;
};

using SrvStatMap = std::map<SERVER*, ServerStats>;
}
