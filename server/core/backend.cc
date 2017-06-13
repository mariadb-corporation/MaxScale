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
#include <maxscale/protocol/mysql.h>
#include <maxscale/debug.h>

using namespace maxscale;

Backend::Backend(SERVER_REF *ref):
    m_closed(false),
    m_backend(ref),
    m_dcb(NULL),
    m_num_result_wait(0),
    m_state(0)
{
}

Backend::~Backend()
{
    ss_dassert(m_closed);

    if (!m_closed)
    {
        close();
    }
}

void Backend::close()
{
    if (!m_closed)
    {
        m_closed = true;

        if (in_use())
        {
            CHK_DCB(m_dcb);

            /** Clean operation counter in bref and in SERVER */
            while (is_waiting_result())
            {
                clear_state(BREF_WAITING_RESULT);
            }
            clear_state(BREF_IN_USE);
            set_state(BREF_CLOSED);

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

    int rc = 0;

    SessionCommandList::iterator iter = m_session_commands.begin();
    SessionCommand& sescmd = *(*iter);
    GWBUF *buffer = sescmd.copy_buffer().release();

    switch (sescmd.get_command())
    {
    case MYSQL_COM_CHANGE_USER:
        /** This makes it possible to handle replies correctly */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rc = m_dcb->func.auth(m_dcb, NULL, m_dcb->session, buffer);
        break;

    case MYSQL_COM_QUERY:
    default:
        /**
         * Mark session command buffer, it triggers writing
         * MySQL command to protocol
         */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rc = m_dcb->func.write(m_dcb, buffer);
        break;
    }

    return rc == 1;
}

void Backend::add_session_command(GWBUF* buffer, uint64_t sequence)
{
    m_session_commands.push_back(SSessionCommand(new SessionCommand(buffer, sequence)));
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

void Backend::clear_state(enum bref_state state)
{
    if (state != BREF_WAITING_RESULT)
    {
        m_state &= ~state;
    }
    else
    {
        /** Decrease global operation count */
        ss_debug(int prev2 = )atomic_add(&m_backend->server->stats.n_current_ops, -1);
        ss_dassert(prev2 > 0);
    }
}

void Backend::set_state(enum bref_state state)
{
    if (state != BREF_WAITING_RESULT)
    {
        m_state |= state;
    }
    else
    {
        /** Increase global operation count */
        ss_debug(int prev2 = )atomic_add(&m_backend->server->stats.n_current_ops, 1);
        ss_dassert(prev2 >= 0);
    }
}

SERVER_REF* Backend::backend() const
{
    return m_backend;
}

bool Backend::connect(MXS_SESSION* session)
{
    bool rval = false;

    if ((m_dcb = dcb_connect(m_backend->server, session, m_backend->server->protocol)))
    {
        m_state = BREF_IN_USE;
        atomic_add(&m_backend->connections, 1);
        rval = true;
    }

    return rval;
}

DCB* Backend::dcb() const
{
    return m_dcb;
}

bool Backend::write(GWBUF* buffer)
{
    return m_dcb->func.write(m_dcb, buffer) != 0;
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

bool Backend::in_use() const
{
    return m_state & BREF_IN_USE;
}

bool Backend::is_waiting_result() const
{
    return m_num_result_wait > 0;
}

bool Backend::is_query_active() const
{
    return m_state & BREF_QUERY_ACTIVE;
}

bool Backend::is_closed() const
{
    return m_state & BREF_CLOSED;
}
