/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/backend.hh>

#include <sstream>

using namespace maxscale;

Backend::Backend(mxs::Endpoint* b)
    : m_backend(b)
{
    m_backend->set_userdata(this);
}

Backend::~Backend()
{
    if (in_use())
    {
        close();
    }
}

void Backend::close(close_type type)
{
    mxb_assert(in_use());

    /** Clean operation counter in bref and in SERVER */
    while (!m_responses.empty())
    {
        ack_write();
    }

    m_state = type == CLOSE_FATAL ? FATAL_FAILURE : CLOSED;

    m_backend->close();
}

bool Backend::connect()
{
    mxb_assert(!in_use());
    bool rval = false;

    if (m_backend->connect())
    {
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

bool Backend::write(GWBUF&& buffer, response_type type)
{
    mxb_assert(in_use());
    bool rval = m_backend->routeQuery(std::move(buffer));

    if (rval && type != NO_RESPONSE)
    {
        m_responses.push_back(type);
        m_backend->target()->stats().add_current_op();
    }

    return rval;
}

void Backend::ack_write()
{
    mxb_assert(!m_responses.empty());
    m_responses.pop_front();
    m_backend->target()->stats().remove_current_op();
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

// static
const char* Backend::to_string(backend_state state)
{
    switch (state)
    {
    case CLOSED:
        return "CLOSED";

    case IN_USE:
        return "IN_USE";

    case FATAL_FAILURE:
        return "FATAL_FAILURE";
    }

    mxb_assert(!true);
    return "UNKNOWN";
}
