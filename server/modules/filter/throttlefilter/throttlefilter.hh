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

#include <maxscale/filter.hh>
#include "throttlesession.hh"
#include <maxbase/eventcount.hh>
#include <maxbase/stopwatch.hh>
#include <iostream>
#include <thread>
#include <memory>

namespace throttle
{

struct ThrottleConfig
{

    int         max_qps;             // if this many queries per second is exceeded..
    maxbase::Duration sampling_duration;   // .. in this time window, then cap qps to max_qps ..
    maxbase::Duration throttling_duration; // .. for this long before disconnect.
    maxbase::Duration continuous_duration;  // What time window is considered continuous meddling.

    // Example: max 100qps and sampling 5s. As soon as more than 500 queries are made in less
    // then any 5s period throttling is triggered (because 501 > 100qps * 5 s). But also note
    // that qps can stay at 200qps for 2.5s before throttling starts.

    // Once throttling has started a countdown for the throttling_duration is started. Throttling
    // is stopped if the qps stays below max_qps for continuous_duration. If throttling continues
    // for more than throttling_duration, the session is disconnected.

    // TODO: this should probably depend on overall activity. If this is to protect the
    // database, multiple sessions gone haywire will still cause problems. It would be quite
    // easy to add a counter into the filter to measure overall qps. On the other hand, if
    // a single session is active, it should be allowed to run at whatever the absolute
    // allowable speed is.
};

class ThrottleFilter : public maxscale::Filter<ThrottleFilter, ThrottleSession>
{
public:
    static ThrottleFilter* create(const char* zName, MXS_CONFIG_PARAMETER* pParams);
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
