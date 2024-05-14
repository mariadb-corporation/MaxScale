/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "throttlefilter"

#include <maxscale/ccdefs.hh>
#include <maxscale/session.hh>

#include "throttlesession.hh"
#include "throttlefilter.hh"

#include <string>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace throttle
{
ThrottleSession::ThrottleSession(MXS_SESSION* mxsSession, SERVICE* service, ThrottleFilter& filter)
    : maxscale::FilterSession(mxsSession, service)
    , m_max_qps(filter.config().max_qps.get())
    , m_sampling_duration(filter.config().sampling_duration.get())
    , m_throttling_duration(filter.config().throttling_duration.get())
    , m_continuous_duration(filter.config().continuous_duration.get())
    , m_query_count("num-queries", m_sampling_duration)
    , m_delayed_call_id(0)
    , m_state(State::MEASURING)
{
}

ThrottleSession::~ThrottleSession()
{
    if (m_delayed_call_id)
    {
        m_pSession->cancel_dcall(m_delayed_call_id);
    }
}

int ThrottleSession::real_routeQuery(GWBUF&& buffer, bool is_delayed)
{
    using namespace std::chrono;

    int count = m_query_count.count();
    // not in g++ 4.4: duration<float>(x).count(), so
    long micro = duration_cast<microseconds>(m_sampling_duration).count();
    float secs = micro / 1000000.0;
    float qps = count / secs;   // not instantaneous, but over so many seconds

    if (!is_delayed && qps >= m_max_qps)    // trigger
    {
        // delay the current routeQuery for at least one cycle at stated max speed.
        int32_t delay = 1 + std::ceil(1000.0 / m_max_qps);
        maxbase::Worker* worker = maxbase::Worker::get_current();
        mxb_assert(worker);

        auto cb = [this](GWBUF&& buf){
            return real_routeQuery(std::move(buf), true);
        };

        m_pSession->delay_routing(this, std::move(buffer), std::chrono::milliseconds(delay), cb);

        if (m_state == State::MEASURING)
        {
            MXB_INFO("Query throttling STARTED session %ld user %s",
                     m_pSession->id(),
                     m_pSession->user().c_str());
            m_state = State::THROTTLING;
            m_first_sample.restart();
        }

        m_last_sample.restart();

        // Filter pipeline ok thus far, will continue after the delay
        // from this point in the pipeline.
        return true;
    }
    else if (m_state == State::THROTTLING)
    {
        if (m_last_sample.split() > m_continuous_duration)
        {
            m_state = State::MEASURING;
            MXB_INFO("Query throttling stopped session %ld user %s",
                     m_pSession->id(),
                     m_pSession->user().c_str());
        }
        else if (m_first_sample.split() > m_throttling_duration)
        {
            MXB_NOTICE("Query throttling Session %ld user %s, throttling limit reached. Disconnect.",
                       m_pSession->id(),
                       m_pSession->user().c_str());
            return false;   // disconnect
        }
    }

    m_query_count.increment();

    return mxs::FilterSession::routeQuery(std::move(buffer));
}

bool ThrottleSession::routeQuery(GWBUF&& buffer)
{
    return real_routeQuery(std::move(buffer), false);
}
}   // throttle
