/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include "cat.hh"

#include <maxscale/protocol/rwbackend.hh>

class Cat;

/**
 * The client session structure used within this router.
 */
class CatSession : public mxs::RouterSession
{
    CatSession(const CatSession&) = delete;
    CatSession& operator=(const CatSession&) = delete;
public:

    CatSession(MXS_SESSION* session, Cat* router, mxs::SRWBackends backends);

    /**
     * The RouterSession instance will be deleted when a client session
     * has terminated. Will be called only after @c close() has been called.
     */
    ~CatSession();

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called when a packet being is routed to the backend. The router should
     * forward the packet to the appropriate server(s).
     *
     * @param pPacket A client packet.
     */
    int32_t routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `MXS_SESSION_ROUTE_REPLY`.
     *
     * @param pPacket  A client packet.
     * @param pBackend The backend the packet is coming from.
     */
    void clientReply(GWBUF* pPacket, DCB* pBackend);

    /**
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param action    The context.
     * @param pSuccess  On output, if false, the session will be terminated.
     */
    void handleError(GWBUF* pMessage,
                     DCB*   pProblem,
                     mxs_error_action_t action,
                     bool* pSuccess);
private:

    MXS_SESSION*               m_session;
    mxs::SRWBackends           m_backends;
    uint64_t                   m_completed;
    uint8_t                    m_packet_num;
    mxs::SRWBackends::iterator m_current;
    GWBUF*                     m_query;

    /**
     * Iterate to next backend
     *
     * @return True if m_current points to a valid backend that is in use
     */
    bool next_backend();
};
