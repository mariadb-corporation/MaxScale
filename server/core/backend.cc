/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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
    , m_target(b->target())
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
        m_state = IN_USE;
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
        m_target->stats().add_current_op();
    }

    return rval;
}

void Backend::ack_write()
{
    mxb_assert(!m_responses.empty());
    m_responses.erase(m_responses.begin());
    m_target->stats().remove_current_op();
}

const maxbase::EpollIntervalTimer& Backend::select_timer() const
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
