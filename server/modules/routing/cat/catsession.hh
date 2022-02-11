/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "cat.hh"

#include <maxscale/protocol/mariadb/rwbackend.hh>

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

    bool routeQuery(GWBUF* pPacket) override;

    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* pMessage,
                     mxs::Endpoint* pProblem, const mxs::Reply& pReply) override;
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
