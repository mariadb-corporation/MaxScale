/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/filter.hh>
#include "eventcount.hh"

namespace throttle
{

class ThrottleFilter;

class ThrottleSession : public maxscale::FilterSession
{
public:
    ThrottleSession(MXS_SESSION* pSession, ThrottleFilter& filter);
    ThrottleSession(const ThrottleSession&) = delete;
    ThrottleSession& operator = (const ThrottleSession&)  = delete;

    int routeQuery(GWBUF* buffer);
private:
    ThrottleFilter& m_filter;
    EventCount      m_query_count;
    StopWatch       m_first_trigger;
    StopWatch       m_last_trigger;
    StopWatch       remove_me;

    enum class State {MEASURING, THROTTLING};
    State m_state;
};
} // throttle
