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

#include "maxscale/mock/backend.hh"
#include <maxscale/protocol/mysql.h>
#include <iostream>

using namespace std;

namespace maxscale
{

namespace mock
{

//
// Backend
//

Backend::Backend()
{
}

Backend::~Backend()
{
}

//
// BufferBackend
//
BufferBackend::BufferBackend()
{
}

BufferBackend::~BufferBackend()
{
}

bool BufferBackend::respond(RouterSession* pSession)
{
    ss_dassert(!idle(pSession));

    bool rv = false;

    if (!idle(pSession))
    {
        Responses& responses = m_session_responses[pSession];
        ss_dassert(!responses.empty());

        GWBUF* pResponse = responses.front();
        responses.pop_front();

        pSession->clientReply(pResponse);

        rv = !responses.empty();
    }

    return rv;
}

bool BufferBackend::idle(const RouterSession* pSession) const
{
    bool rv = true;

    SessionResponses::const_iterator i = m_session_responses.find(pSession);

    if (i != m_session_responses.end())
    {
        const Responses& responses = i->second;
        rv = responses.empty();
    }

    return rv;
}

void BufferBackend::enqueue_response(RouterSession* pSession, GWBUF* pResponse)
{
    Responses& responses = m_session_responses[pSession];

    responses.push_back(pResponse);
}

//
// OkBackend
//

OkBackend::OkBackend()
{
}

void OkBackend::handle_statement(RouterSession* pSession, GWBUF* pStatement)
{
    /* Note: sequence id is always 01 (4th byte) */
    const static uint8_t ok[MYSQL_OK_PACKET_MIN_LEN] =
        { 07, 00, 00, 01, 00, 00, 00, 02, 00, 00, 00 };

    GWBUF* pResponse = gwbuf_alloc_and_load(sizeof(ok), &ok);
    ss_dassert(pResponse);

    enqueue_response(pSession, pResponse);

    gwbuf_free(pStatement);
}

} // mock

} // maxscale
