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

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/cppdefs.hh>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>

#include "throttlesession.hh"
#include "throttlefilter.hh"

#include <string>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace throttle
{
ThrottleSession::ThrottleSession(MXS_SESSION* mxsSession, ThrottleFilter &filter)
    : maxscale::FilterSession(mxsSession),
      m_filter(filter),
      m_query_count("num-queries", filter.config().sampling_duration),
      m_delayed_call_id(0),
      m_state(State::MEASURING)
{
}

ThrottleSession::~ThrottleSession()
{
    if (m_delayed_call_id)
    {
        maxscale::Worker* worker = maxscale::Worker::get_current();
        ss_dassert(worker);
        worker->cancel_delayed_call(m_delayed_call_id);
    }
}

int ThrottleSession::real_routeQuery(GWBUF *buffer, bool is_delayed)
{
    using namespace std::chrono;

    int count  = m_query_count.count();
    // not in g++ 4.4: duration<float>(x).count(), so
    long micro = duration_cast<microseconds>(m_filter.config().sampling_duration).count();
    float secs = micro / 1000000.0;
    float qps  = count / secs;  // not instantaneous, but over so many seconds

    if (!is_delayed && qps >= m_filter.config().max_qps) // trigger
    {
        // delay the current routeQuery for at least one cycle at stated max speed.
        int32_t delay = 1 + std::ceil(1000.0 / m_filter.config().max_qps);
        maxscale::Worker* worker = maxscale::Worker::get_current();
        ss_dassert(worker);
        m_delayed_call_id = worker->delayed_call(delay, &ThrottleSession::delayed_routeQuery,
                                                 this, buffer);
        if (m_state == State::MEASURING)
        {
            MXS_INFO("Query throttling STARTED session %ld user %s",
                     m_pSession->ses_id, m_pSession->client_dcb->user);
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
        if (m_last_sample.lap() > m_filter.config().continuous_duration)
        {
            m_state = State::MEASURING;
            MXS_INFO("Query throttling stopped session %ld user %s",
                     m_pSession->ses_id, m_pSession->client_dcb->user);
        }
        else if (m_first_sample.lap() > m_filter.config().throttling_duration)
        {
            MXS_NOTICE("Query throttling Session %ld user %s, throttling limit reached. Disconnect.",
                       m_pSession->ses_id, m_pSession->client_dcb->user);
            return false; // disconnect
        }
    }

    m_query_count.increment();

    return mxs::FilterSession::routeQuery(buffer);
}

bool ThrottleSession::delayed_routeQuery(maxscale::Worker::Call::action_t action, GWBUF *buffer)
{
    m_delayed_call_id = 0;
    switch (action)
    {
    case maxscale::Worker::Call::EXECUTE:
        if (!real_routeQuery(buffer, true))
        {
            poll_fake_hangup_event(m_pSession->client_dcb);
        }
        break;

    case maxscale::Worker::Call::CANCEL:
        gwbuf_free(buffer);
        break;
    }

    return false;
}

int ThrottleSession::routeQuery(GWBUF *buffer)
{
    return real_routeQuery(buffer, false);
}

} // throttle
