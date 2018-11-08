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

#include <maxscale/session_stats.hh>
#include <iostream>

void maxscale::ServerStats::start_session()
{
    // nop for now
}

void maxscale::ServerStats::end_session(maxbase::Duration sess_duration,
                                        maxbase::Duration active_duration,
                                        int64_t num_selects)
{
    m_ave_session_dur.add(sess_duration.secs());
    m_ave_active_dur.add(active_duration.secs());
    m_num_ave_session_selects.add(num_selects);
}

maxscale::ServerStats& maxscale::ServerStats::operator+=(const maxscale::ServerStats& rhs)
{
    total += rhs.total;
    read += rhs.read;
    write += rhs.write;
    m_ave_session_dur += rhs.m_ave_session_dur;
    m_ave_active_dur += rhs.m_ave_active_dur;
    m_num_ave_session_selects += rhs.m_num_ave_session_selects;

    return *this;
}

maxscale::ServerStats::CurrentStats maxscale::ServerStats::current_stats() const
{
    double sess_secs = m_ave_session_dur.average();
    double active_secs = m_ave_active_dur.average();
    double active = 100 * active_secs / sess_secs;

    return {maxbase::Duration(sess_secs),
            active,
            static_cast<int64_t>(m_num_ave_session_selects.average()),
            total,
            read,
            write};
}
