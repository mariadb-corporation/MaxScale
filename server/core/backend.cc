/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/backend.hh>

#include <sstream>

#include <maxscale/protocol/mysql.h>
#include <maxscale/debug.h>

using namespace maxscale;

Backend::Backend(SERVER_REF *ref):
    m_closed(false),
    m_backend(ref),
    m_dcb(NULL),
    m_state(0)
{
    std::stringstream ss;
    ss << "[" << server()->name << "]:" << server()->port;
    m_uri = ss.str();
}

Backend::~Backend()
{
    ss_dassert(m_closed || !in_use());

    if (in_use())
    {
        close();
    }
}

void Backend::close(close_type type)
{
    ss_dassert(m_dcb->n_close == 0);

    if (!m_closed)
    {
        m_closed = true;

        if (in_use())
        {
            CHK_DCB(m_dcb);

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

            /** decrease server current connection counters */
            atomic_add(&m_backend->connections, -1);
        }
    }
    else
    {
        ss_dassert(false);
    }
}

bool Backend::execute_session_command()
{
    if (is_closed() || !session_command_count())
    {
        return false;
    }

    CHK_DCB(m_dcb);

    SessionCommandList::iterator iter = m_session_commands.begin();
    SessionCommand& sescmd = *(*iter);
    GWBUF *buffer = sescmd.deep_copy_buffer();
    bool rval = false;

    switch (sescmd.get_command())
    {
    case MXS_COM_QUIT:
    case MXS_COM_STMT_CLOSE:
        /** These commands do not generate responses */
        rval = write(buffer, NO_RESPONSE);
        complete_session_command();
        break;

    case MXS_COM_CHANGE_USER:
        /** This makes it possible to handle replies correctly */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rval = auth(buffer);
        break;

    case MXS_COM_QUERY:
    default:
        /**
         * Mark session command buffer, it triggers writing
         * MySQL command to protocol
         */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rval = write(buffer);
        break;
    }

    return rval;
}

void Backend::append_session_command(GWBUF* buffer, uint64_t sequence)
{
    m_session_commands.push_back(SSessionCommand(new SessionCommand(buffer, sequence)));
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
    ss_dassert(session_command_count() > 0);
    return m_session_commands.front();
}

void Backend::clear_state(backend_state state)
{
    if ((state & WAITING_RESULT) && (m_state & WAITING_RESULT))
    {
        ss_debug(int prev2 = )atomic_add(&m_backend->server->stats.n_current_ops, -1);
        ss_dassert(prev2 > 0);
    }

    m_state &= ~state;
}

void Backend::set_state(backend_state state)
{
    if ((state & WAITING_RESULT) && (m_state & WAITING_RESULT) == 0)
    {
        ss_debug(int prev2 = )atomic_add(&m_backend->server->stats.n_current_ops, 1);
        ss_dassert(prev2 >= 0);
    }

    m_state |= state;
}

bool Backend::connect(MXS_SESSION* session)
{
    bool rval = false;

    if ((m_dcb = dcb_connect(m_backend->server, session, m_backend->server->protocol)))
    {
        m_closed = false;
        m_state = IN_USE;
        atomic_add(&m_backend->connections, 1);
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
    bool rval = m_dcb->func.write(m_dcb, buffer) != 0;

    if (rval && type == EXPECT_RESPONSE)
    {
        set_state(WAITING_RESULT);
    }

    return rval;
}

bool Backend::auth(GWBUF* buffer)
{
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
    ss_dassert(is_waiting_result());
    clear_state(WAITING_RESULT);
}

void Backend::store_command(GWBUF* buffer)
{
    m_pending_cmd.reset(buffer);
}

bool Backend::write_stored_command()
{
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
