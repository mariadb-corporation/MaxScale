/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/backend.hh>

#include <sstream>

#include <maxbase/atomic.hh>

#include <maxscale/alloc.h>
#include <maxscale/protocol/mysql.hh>

using namespace maxscale;

Backend::Backend(SERVER_REF* ref)
    : m_closed(false)
    , m_closed_at(0)
    , m_opened_at(0)
    , m_backend(ref)
    , m_dcb(NULL)
    , m_state(0)
{
    std::stringstream ss;
    ss << "[" << server()->address << "]:" << server()->port;
    m_uri = ss.str();
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
    mxb_assert(m_dcb->n_close == 0);

    if (!m_closed)
    {
        m_closed = true;
        m_closed_at = time(NULL);

        if (in_use())
        {
            /** Clean operation counter in bref and in SERVER */
            if (is_waiting_result())
            {
                clear_state(WAITING_RESULT);
            }
            clear_state(IN_USE);

            if (type == CLOSE_FATAL)
            {
                set_state(FATAL_FAILURE);
            }

            dcb_close(m_dcb);
            m_dcb = NULL;

            /** decrease server current connection counters */
            mxb::atomic::add(&m_backend->connections, -1, mxb::atomic::RELAXED);
        }
    }
    else
    {
        mxb_assert(false);
    }
}

bool Backend::execute_session_command()
{
    if (is_closed() || !has_session_commands())
    {
        return false;
    }

    SSessionCommand& sescmd = m_session_commands.front();
    GWBUF* buffer = sescmd->deep_copy_buffer();
    bool rval = false;

    switch (sescmd->get_command())
    {
    case MXS_COM_QUIT:
    case MXS_COM_STMT_CLOSE:
    case MXS_COM_STMT_SEND_LONG_DATA:
        /** These commands do not generate responses */
        rval = write(buffer, NO_RESPONSE);
        complete_session_command();
        mxb_assert(!is_waiting_result());
        break;

    case MXS_COM_CHANGE_USER:
        rval = auth(buffer);
        break;

    case MXS_COM_QUERY:
    default:
        // We want the complete response in one packet
        gwbuf_set_type(buffer, GWBUF_TYPE_COLLECT_RESULT);
        rval = write(buffer, EXPECT_RESPONSE);
        mxb_assert(is_waiting_result());
        break;
    }

    return rval;
}

void Backend::append_session_command(GWBUF* buffer, uint64_t sequence)
{
    append_session_command(SSessionCommand(new SessionCommand(buffer, sequence)));
}

void Backend::append_session_command(const SSessionCommand& sescmd)
{
    m_session_commands.push_back(sescmd);
}

void Backend::append_session_command(const SessionCommandList& sescmdlist)
{
    m_session_commands.insert(m_session_commands.end(), sescmdlist.begin(), sescmdlist.end());
}

uint64_t Backend::complete_session_command()
{
    uint64_t rval = m_session_commands.front()->get_position();
    m_session_commands.pop_front();
    return rval;
}

size_t Backend::session_command_count() const
{
    return m_session_commands.size();
}

const SSessionCommand& Backend::next_session_command() const
{
    mxb_assert(has_session_commands());
    return m_session_commands.front();
}

void Backend::clear_state(backend_state state)
{
    if ((state & WAITING_RESULT) && (m_state & WAITING_RESULT))
    {
        MXB_AT_DEBUG(int prev2 = ) mxb::atomic::add(&m_backend->server->stats.n_current_ops,
                                                    -1,
                                                    mxb::atomic::RELAXED);
        mxb_assert(prev2 > 0);
    }

    m_state &= ~state;
}

void Backend::set_state(backend_state state)
{
    if ((state & WAITING_RESULT) && (m_state & WAITING_RESULT) == 0)
    {
        MXB_AT_DEBUG(int prev2 = ) mxb::atomic::add(&m_backend->server->stats.n_current_ops,
                                                    1,
                                                    mxb::atomic::RELAXED);
        mxb_assert(prev2 >= 0);
    }

    m_state |= state;
}

bool Backend::connect(MXS_SESSION* session, SessionCommandList* sescmd)
{
    mxb_assert(!in_use());
    bool rval = false;

    if ((m_dcb = dcb_connect(m_backend->server, session, m_backend->server->protocol().c_str())))
    {
        m_closed = false;
        m_closed_at = 0;
        m_opened_at = time(NULL);
        m_state = IN_USE;
        mxb::atomic::add(&m_backend->connections, 1, mxb::atomic::RELAXED);
        rval = true;

        if (sescmd && sescmd->size())
        {
            append_session_command(*sescmd);

            if (!execute_session_command())
            {
                rval = false;
            }
        }
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
    bool rval = m_dcb->func.write(m_dcb, buffer) != 0;

    if (rval && type == EXPECT_RESPONSE)
    {
        set_state(WAITING_RESULT);
    }

    return rval;
}

bool Backend::auth(GWBUF* buffer)
{
    mxb_assert(in_use());
    bool rval = false;

    if (m_dcb->func.auth(m_dcb, NULL, m_dcb->session, buffer) == 1)
    {
        set_state(WAITING_RESULT);
        rval = true;
    }

    return rval;
}

void Backend::ack_write()
{
    mxb_assert(is_waiting_result());
    clear_state(WAITING_RESULT);
}

void Backend::store_command(GWBUF* buffer)
{
    m_pending_cmd.reset(buffer);
}

bool Backend::write_stored_command()
{
    mxb_assert(in_use());
    bool rval = false;

    if (m_pending_cmd.length())
    {
        rval = write(m_pending_cmd.release());

        if (!rval)
        {
            MXS_ERROR("Routing of pending query failed.");
        }
    }

    return rval;
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

void Backend::select_ended()
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
       << "status: [" << m_backend->server->status_string() << "] "
       << "state: [" << to_string((backend_state)m_state) << "] "
       << "last opened at: [" << opened_at << "] "
       << "last closed at: [" << closed_at << "] "
       << "last close reason: [" << m_close_reason << "] "
       << "num sescmd: [" << m_session_commands.size() << "]";

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

        if (state & WAITING_RESULT)
        {
            rval += rval.empty() ? "" : "|";
            rval += "WAITING_RESULT";
        }

        if (state & FATAL_FAILURE)
        {
            rval += rval.empty() ? "" : "|";
            rval += "FATAL_FAILURE";
        }
    }

    return rval;
}
