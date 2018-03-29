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

#include "cat.hh"
#include "catsession.hh"
#include "maxscale/protocol/mysql.h"
#include "maxscale/modutil.h"

using namespace maxscale;

CatSession::CatSession(MXS_SESSION* session, Cat* router, SRWBackendList& backends):
    RouterSession(session),
    m_session(session),
    m_backends(backends),
    m_completed(0)
{
}

CatSession::~CatSession()
{
}

void CatSession::close()
{
}

void CatSession::skip_unused()
{
    // Skip unused backends
    while (m_current != m_backends.end() && !(*m_current)->in_use())
    {
        m_current++;
    }
}

int32_t CatSession::routeQuery(GWBUF* pPacket)
{
    int32_t rval = 0;

    m_completed = 0;
    m_packet_num = 0;
    m_query = pPacket;
    m_current = m_backends.begin();

    // If the first backend is not in use, find one that is
    skip_unused();

    if (m_current != m_backends.end())
    {
        // We have a backend, write the query only to this one. It will be
        // propagated onwards in clientReply.
        rval = (*m_current)->write(gwbuf_clone(pPacket));
        (*m_current)->set_reply_state(REPLY_STATE_START);
    }

    return rval;
}

void CatSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    auto backend = *m_current;
    ss_dassert(backend->dcb() == pDcb);
    bool send = false;
    bool propagate = true;

    if (m_completed == 0 && backend->get_reply_state() == REPLY_STATE_START &&
        !mxs_mysql_is_result_set(pPacket))
    {
        propagate = false;
    }

    if (backend->reply_is_complete(pPacket))
    {
        backend->ack_write();
        m_completed++;
        m_current++;
        skip_unused();

        if (m_current == m_backends.end())
        {
            uint8_t eof_packet[] = {0x5, 0x0, 0x0, 0x0, 0xfe, 0x0, 0x0, 0x2, 0x0};
            gwbuf_free(pPacket);
            pPacket = gwbuf_alloc_and_load(sizeof(eof_packet), eof_packet);
            send = true;
            gwbuf_free(m_query);
            m_query = NULL;
        }
        else if (propagate)
        {
            (*m_current)->write(gwbuf_clone(m_query));
        }
        else
        {
            send = true;
            gwbuf_free(m_query);
            m_query = NULL;
        }
    }

    if (m_completed == 0)
    {
        send = backend->get_reply_state() != REPLY_STATE_DONE;
    }
    else if (backend->get_reply_state() == REPLY_STATE_RSET_ROWS &&
             mxs_mysql_get_command(pPacket) != MYSQL_REPLY_EOF)
    {
        send = true;
    }

    if (send)
    {
        GWBUF_DATA(pPacket)[3] = m_packet_num++;
        MXS_SESSION_ROUTE_REPLY(pDcb->session, pPacket);
    }
    else
    {
        gwbuf_free(pPacket);
    }
}

void CatSession::handleError(GWBUF* pMessage, DCB* pProblem, mxs_error_action_t action, bool* pSuccess)
{
    *pSuccess = false;
}
