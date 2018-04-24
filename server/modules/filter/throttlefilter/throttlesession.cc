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

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/cppdefs.hh>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>

#include "throttlesession.hh"
#include "throttlefilter.hh"

#include <string>
#include <algorithm>
#include <sstream>

namespace throttle
{
ThrottleSession::ThrottleSession(MXS_SESSION* mxsSession, ThrottleFilter &filter)
    : maxscale::FilterSession(mxsSession),
      m_filter(filter),
      m_query_count("num-queries", filter.config().trigger_duration),
      m_state(State::MEASURING)
{
}

int ThrottleSession::routeQuery(GWBUF *buffer)
{
    // TODO: count everything, or filter something out?
    using namespace std::chrono;

    m_query_count.increment();
    int count = m_query_count.count();
    int secs = duration_cast<seconds>(m_filter.config().trigger_duration).count();
    float qps = count / secs;  // not instantaneous, but over so many seconds

    if (qps >= m_filter.config().max_qps) // trigger
    {
        // sleep a few cycles, keeping qps near max.
        usleep(4 * 1000000 / qps); // to be replaced with delayed calls

        if (m_state == State::MEASURING)
        {
            MXS_INFO("Query throttling STARTED session %ld user %s",
                     m_pSession->ses_id, m_pSession->client_dcb->user);
            m_state = State::THROTTLING;
            m_first_trigger.restart();
        }
        m_last_trigger.restart();
    }
    else if (m_state == State::THROTTLING)
    {
        if (m_last_trigger.lap() > Duration(std::chrono::seconds(2))) // TODO, might be ok though.
        {
            m_state = State::MEASURING;
            MXS_INFO("Query throttling stopped session %ld user %s",
                     m_pSession->ses_id, m_pSession->client_dcb->user);
        }
        else if (m_first_trigger.lap() > m_filter.config().throttle_duration)
        {
            MXS_NOTICE("Session %ld user %s, qps throttling limit reached. Disconnect.",
                       m_pSession->ses_id, m_pSession->client_dcb->user);
            return false; // disconnect
        }
    }

    return mxs::FilterSession::routeQuery(buffer);
}
} // throttle
