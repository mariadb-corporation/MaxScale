/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "cat.hh"
#include "catsession.hh"

#include <maxscale/protocol/mariadb/mysql.hh>

using namespace maxscale;

CatSession::CatSession(MXS_SESSION* session, Cat* router, mxs::RWBackends backends)
    : RouterSession(session)
    , m_backends(std::move(backends))
    , m_completed(0)
    , m_packet_num(0)
{
}

bool CatSession::next_backend()
{
    // Skip unused backends
    while (m_current != m_backends.end() && !m_current->in_use())
    {
        m_current++;
    }

    return m_current != m_backends.end();
}

bool CatSession::routeQuery(GWBUF&& packet)
{
    int32_t rval = 0;

    m_completed = 0;
    m_packet_num = 0;
    m_query = std::move(packet);
    m_current = m_backends.begin();

    if (next_backend())
    {
        // We have a backend, write the query only to this one. It will be
        // propagated onwards in clientReply.
        rval = m_current->write(m_query.shallow_clone());
    }

    return rval;
}

bool CatSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_current->backend() == down.endpoint());
    bool send = false;

    if (reply.is_complete())
    {
        m_completed++;
        m_current++;

        if (!next_backend())
        {
            send = true;
            m_query.clear();
        }
        else
        {
            m_current->write(m_query.shallow_clone());
        }
    }

    if (m_completed == 0)
    {
        send = reply.state() != mxs::ReplyState::DONE;
    }
    else if (reply.state() == mxs::ReplyState::RSET_ROWS
             && mxs_mysql_get_command(packet) != MYSQL_REPLY_EOF)
    {
        send = true;
    }

    int32_t rc = 1;

    if (send)
    {
        // Increment the packet sequence number and send it to the client
        packet.data()[3] = m_packet_num++;
        rc = RouterSession::clientReply(std::move(packet), down, reply);
    }

    return rc;
}
