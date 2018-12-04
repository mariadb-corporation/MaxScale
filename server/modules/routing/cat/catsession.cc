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
#include "cat.hh"
#include "catsession.hh"

#include <maxscale/protocol/mysql.hh>
#include <maxscale/modutil.hh>

using namespace maxscale;

CatSession::CatSession(MXS_SESSION* session, Cat* router, mxs::SRWBackends backends)
    : RouterSession(session)
    , m_session(session)
    , m_backends(std::move(backends))
    , m_completed(0)
    , m_packet_num(0)
    , m_query(NULL)
{
}

CatSession::~CatSession()
{
}

void CatSession::close()
{
}

bool CatSession::next_backend()
{
    // Skip unused backends
    while (m_current != m_backends.end() && !(*m_current)->in_use())
    {
        m_current++;
    }

    return m_current != m_backends.end();
}

int32_t CatSession::routeQuery(GWBUF* pPacket)
{
    int32_t rval = 0;

    m_completed = 0;
    m_packet_num = 0;
    m_query = pPacket;
    m_current = m_backends.begin();

    if (next_backend())
    {
        // We have a backend, write the query only to this one. It will be
        // propagated onwards in clientReply.
        rval = (*m_current)->write(gwbuf_clone(pPacket));
    }

    return rval;
}

void CatSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    auto& backend = *m_current;
    mxb_assert(backend->dcb() == pDcb);
    bool send = false;

    backend->process_reply(pPacket);

    if (backend->reply_is_complete())
    {
        m_completed++;
        m_current++;

        if (!next_backend())
        {
            send = true;
            gwbuf_free(m_query);
            m_query = NULL;
        }
        else
        {
            (*m_current)->write(gwbuf_clone(m_query));
        }
    }

    if (m_completed == 0)
    {
        send = backend->get_reply_state() != REPLY_STATE_DONE;
    }
    else if (backend->get_reply_state() == REPLY_STATE_RSET_ROWS
             && mxs_mysql_get_command(pPacket) != MYSQL_REPLY_EOF)
    {
        send = true;
    }

    if (send)
    {
        // Increment the packet sequence number and send it to the client
        mxb_assert(modutil_count_packets(pPacket) > 0);
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
    /**
     * The simples thing to do here is to close the connection. Anything else
     * would still require extra processing on the client side and reconnecting
     * will cause things to fix themselves.
     */
    *pSuccess = false;
}
