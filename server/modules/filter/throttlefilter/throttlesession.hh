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

#include <maxbase/worker.hh>
#include <maxscale/filter.hh>
#include <maxbase/eventcount.hh>

namespace throttle
{

class ThrottleFilter;

class ThrottleSession : public maxscale::FilterSession
{
public:
    ThrottleSession(MXS_SESSION* pSession, ThrottleFilter& filter);
    ThrottleSession(const ThrottleSession&) = delete;
    ThrottleSession& operator=(const ThrottleSession&) = delete;
    ~ThrottleSession();

    int routeQuery(GWBUF* buffer);
private:
    bool delayed_routeQuery(maxbase::Worker::Call::action_t action,
                            GWBUF* buffer);
    int real_routeQuery(GWBUF* buffer, bool is_delayed);
    ThrottleFilter&     m_filter;
    maxbase::EventCount m_query_count;
    maxbase::StopWatch  m_first_sample;
    maxbase::StopWatch  m_last_sample;
    uint32_t            m_delayed_call_id;  // there can be only one in flight

    enum class State {MEASURING,
                      THROTTLING};
    State m_state;
};
}   // throttle
