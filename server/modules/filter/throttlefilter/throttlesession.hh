/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
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
    ThrottleSession(MXS_SESSION* pSession, SERVICE* pService, ThrottleFilter& filter);
    ThrottleSession(const ThrottleSession&) = delete;
    ThrottleSession& operator=(const ThrottleSession&) = delete;
    ~ThrottleSession();

    bool routeQuery(GWBUF* buffer) override;
private:
    bool delayed_routeQuery(maxbase::Worker::Call::action_t action,
                            GWBUF* buffer);
    int real_routeQuery(GWBUF* buffer, bool is_delayed);

    // Configuration
    int64_t                   m_max_qps;
    std::chrono::milliseconds m_sampling_duration;
    std::chrono::milliseconds m_throttling_duration;
    std::chrono::milliseconds m_continuous_duration;

    maxbase::EventCount m_query_count;
    maxbase::StopWatch  m_first_sample;
    maxbase::StopWatch  m_last_sample;
    uint32_t            m_delayed_call_id;  // there can be only one in flight

    enum class State {MEASURING,
                      THROTTLING};
    State m_state;
};
}   // throttle
