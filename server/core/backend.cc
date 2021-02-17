/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/backend.hh>

#include <sstream>

#include <maxbase/atomic.hh>

#include <maxbase/alloc.h>
#include <maxscale/protocol/mariadb/mysql.hh>

using namespace maxscale;

Backend::Backend(mxs::Endpoint* b)
    : m_backend(b)
{
    m_backend->set_userdata(this);
}

Backend::~Backend()
{
    mxb_assert(m_closed || !in_use());

    if (in_use())
    {
        close();
    }
}

void Backend::close(close_type type)
{
    if (!m_closed)
    {
        m_closed = true;
        m_closed_at = time(NULL);

        if (in_use())
        {
            /** Clean operation counter in bref and in SERVER */
            while (!m_responses.empty())
            {
                ack_write();
            }

            clear_state(IN_USE);

            if (type == CLOSE_FATAL)
            {
                set_state(FATAL_FAILURE);
            }

            m_backend->close();
        }
    }
    else
    {
        mxb_assert(false);
    }
}

void Backend::clear_state(backend_state state)
{
    m_state &= ~state;
}

void Backend::set_state(backend_state state)
{
    m_state |= state;
}

bool Backend::connect()
{
    mxb_assert(!in_use());
    bool rval = false;

    if (m_backend->connect())
    {
        m_closed = false;
        m_closed_at = 0;
        m_opened_at = time(NULL);
        m_state = IN_USE;
        m_close_reason.clear();
        rval = true;
    }
    else
    {
        m_state = FATAL_FAILURE;
    }

    return rval;
}

bool Backend::write(GWBUF* buffer, response_type type)
{
    mxb_assert(in_use());
    bool rval = m_backend->routeQuery(buffer);

    if (rval && type != NO_RESPONSE)
    {
        m_responses.push_back(type);

        MXB_AT_DEBUG(int prev2 = ) mxb::atomic::add(&m_backend->target()->stats().n_current_ops,
                                                    1, mxb::atomic::RELAXED);
        mxb_assert(prev2 >= 0);
    }

    return rval;
}

void Backend::ack_write()
{
    mxb_assert(!m_responses.empty());
    m_responses.pop_front();

    MXB_AT_DEBUG(int prev2 = ) mxb::atomic::add(&m_backend->target()->stats().n_current_ops,
                                                -1, mxb::atomic::RELAXED);
    mxb_assert(prev2 > 0);
}

const maxbase::StopWatch& Backend::session_timer() const
{
    return m_session_timer;
}

const maxbase::IntervalTimer& Backend::select_timer() const
{
    return m_select_timer;
}

void Backend::select_started()
{
    m_select_timer.start_interval();
}

void Backend::select_finished()
{
    m_select_timer.end_interval();
    ++m_num_selects;
}

int64_t Backend::num_selects() const
{
    return m_num_selects;
}

void Backend::set_close_reason(const std::string& reason)
{
    m_close_reason = reason;
}

std::string Backend::get_verbose_status() const
{
    std::stringstream ss;
    char closed_at[30] = "not closed";
    char opened_at[30] = "not opened";

    if (m_closed_at)
    {
        mxb_assert(m_closed);
        ctime_r(&m_closed_at, closed_at);
        char* nl = strrchr(closed_at, '\n');
        mxb_assert(nl);
        *nl = '\0';
    }

    if (m_opened_at)
    {
        ctime_r(&m_opened_at, opened_at);
        char* nl = strrchr(opened_at, '\n');
        mxb_assert(nl);
        *nl = '\0';
    }

    ss << "name: [" << name() << "] "
       << "status: [" << m_backend->target()->status_string() << "] "
       << "state: [" << to_string((backend_state)m_state) << "] "
       << "last opened at: [" << opened_at << "] "
       << "last closed at: [" << closed_at << "] "
       << "last close reason: [" << m_close_reason << "] ";

    return ss.str();
}

std::string Backend::to_string(backend_state state)
{
    std::string rval;

    if (state == 0)
    {
        rval = "NOT_IN_USE";
    }
    else
    {
        if (state & IN_USE)
        {
            rval += "IN_USE";
        }

        if (state & FATAL_FAILURE)
        {
            rval += rval.empty() ? "" : "|";
            rval += "FATAL_FAILURE";
        }
    }

    return rval;
}
