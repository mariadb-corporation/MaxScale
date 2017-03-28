/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.hh"

#include <maxscale/protocol/mysql.h>

using namespace schemarouter;

Backend::Backend(SERVER_REF *ref):
    m_backend(ref),
    m_dcb(NULL),
    m_map_queue(NULL),
    m_mapped(false),
    m_num_mapping_eof(0),
    m_num_result_wait(0),
    m_pending_cmd(NULL),
    m_state(0)
{
}

Backend::~Backend()
{
    gwbuf_free(m_map_queue);
    gwbuf_free(m_pending_cmd);
}


bool Backend::execute_sescmd()
{
    if (BREF_IS_CLOSED(this))
    {
        return false;
    }

    CHK_DCB(m_dcb);

    int rc = 0;

    /** Return if there are no pending ses commands */
    if (m_session_commands.size() == 0)
    {
        MXS_INFO("Cursor had no pending session commands.");
        return false;
    }

    SessionCommandList::iterator iter = m_session_commands.begin();
    GWBUF *buffer = iter->copy_buffer().release();

    switch (iter->get_command())
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
