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
#include "throttlesession.hh"
#include "eventcount.hh"
#include "stopwatch.hh"
#include <iostream>
#include <thread>
#include <memory>

namespace throttle
{

struct ThrottleConfig
{

    int         max_qps;           // if this many queries per second is exceeded..
    Duration    trigger_duration;  // .. in this time window, then cap qps to max_qps ..
    Duration    throttle_duration; // .. for this long before disconnect.
    // Example: max 100qps and trigger 5s. As soon as more than 500 queries happen in less than
    // 5s the action is triggered. So, 501 queries in one second is a trigger, but 400qps
    // for one second is acceptable as long as the qps stayed low for the previous 4 seconds,
    // and stays low for the next 4 seconds. In other words, a short burst>max is not a trigger.
    // Further, say max_throttle_duration is 60s. If the qps is continously capped (throttled)
    // for 60s, the session is disconnected.

    // TODO: this should probably depend on overall activity. If this is to protect the
    // database, multiple sessions gone haywire will still cause problems. It would be quite
    // east to add a counter into the filter to measure overall qps. On the other hand, if
    // a single session is active, it should be allowed to run at whatever the absolute
    // allowable speed is.
};

class ThrottleFilter : public maxscale::Filter<ThrottleFilter, ThrottleSession>
{
public:
    static ThrottleFilter* create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* pParams);
    ThrottleFilter(const ThrottleFilter&) = delete;
    ThrottleFilter& operator = (const ThrottleFilter&) = delete;

    ThrottleSession* newSession(MXS_SESSION* mxsSession);

    void diagnostics(DCB* pDcb);
    json_t* diagnostics_json() const;
    uint64_t getCapabilities();
    const ThrottleConfig& config() const;
    void sessionClose(ThrottleSession* session);
private:
    ThrottleFilter(const ThrottleConfig& config);

    ThrottleConfig m_config;
};
} // throttle
