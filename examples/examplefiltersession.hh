/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

class ExampleFilter;

/*
 * Defines session-specific data for this filter. An object of this class is created when a client connects
 * and deleted on disconnect. The object is only accessed from one thread because sessions are locked to
 * a thread when created.
 */
class ExampleFilterSession : public maxscale::FilterSession
{
    // Prevent copy-constructor and assignment operator usage
    ExampleFilterSession(const ExampleFilterSession&);
    ExampleFilterSession& operator=(const ExampleFilterSession&);

public:
    ~ExampleFilterSession();

    // Called when a client session has been closed. Destructor will be called right after.
    void close();

    /**
     * Called by ExampleFilter::newSession() to create the session.
     *
     * @param pSession pSession The generic MaxScale session object
     * @param pFilter The shared filter object
     * @return A new session or NULL on failure
     */
    static ExampleFilterSession* create(MXS_SESSION* pSession, SERVICE* pService, ExampleFilter& pFilter);

    /**
     * Handle a query from the client. This is called when the client sends a query and the query has not
     * been blocked by any previous component in the query processing chain. The filter should do its own
     * processing and then send the query to the next component. If the query comes in multiple packets,
     * this is called for each packet.
     *
     * @param pPacket Packet containing the query, or at least a part of it
     * @return 0 on success. This typically depends on the later stages of the query processing chain.
     */
    int routeQuery(GWBUF* pPacket);


    /**
     * Handle a reply from server. The reply typically contains a resultset or a response to a command.
     * The filter should do its own processing and then send the query to the next component.
     * If the reply comes in multiple packets, this is called for each packet. The processing chain for
     * replies is the same as for queries, just walked in the opposite direction.
     *
     * @param pPacket Packet containing results
     * @return 0 on success. This typically depends on the later stages of the reply processing chain.
     */
    int clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    // Used in the create function
    ExampleFilterSession(MXS_SESSION* pSession, SERVICE* pService, ExampleFilter& filter);

    ExampleFilter& m_filter;    /**< Shared filter data */

    uint64_t m_session_id {0};  /**< Session id */
    int      m_queries {0};     /**< How many queries has this session seen */
    int      m_replies {0};     /**< How many replies has this session seen */
};
