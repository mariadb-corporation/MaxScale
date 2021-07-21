/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/local_client.hh>
#include <maxscale/routingworker.hh>

LocalClient::~LocalClient()
{
    if (m_down && m_down->is_open())
    {
        m_down->close();
    }
}

bool LocalClient::queue_query(GWBUF* buffer)
{
    bool rval = false;

    if (m_down->is_open())
    {
        rval = m_down->routeQuery(buffer);
    }
    else
    {
        gwbuf_free(buffer);
    }


    return rval;
}

LocalClient* LocalClient::create(MXS_SESSION* session, mxs::Target* target)
{
    LocalClient* relay = nullptr;
    auto state = session->state();

    if (state == MXS_SESSION::State::STARTED || state == MXS_SESSION::State::CREATED)
    {
        relay = new LocalClient;

        if (!(relay->m_down = target->get_connection(relay, session)))
        {
            delete relay;
            relay = nullptr;
        }
    }

    return relay;
}

bool LocalClient::connect()
{
    return m_down->connect();
}

int32_t LocalClient::routeQuery(GWBUF* buffer)
{
    mxb_assert(!true);
    return 0;
}

int32_t LocalClient::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    gwbuf_free(buffer);
    return 0;
}

bool LocalClient::handleError(mxs::ErrorType type, GWBUF* error, mxs::Endpoint* down, const mxs::Reply& reply)
{
    if (m_down->is_open())
    {
        m_down->close();
    }

    return true;
}
