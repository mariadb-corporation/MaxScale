/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file readconnection.hh - The read connection balancing query module header file
 */

#define MXS_MODULE_NAME "readconnroute"

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>
#include <maxscale/session_stats.hh>
#include <maxscale/workerlocal.hh>

class RCR;

/**
 * The client session structure used within this router.
 */
class RCRSession : public mxs::RouterSession
{
public:
    RCRSession(RCR* inst, MXS_SESSION* session, mxs::Endpoint* backend,
               const Endpoints& endpoints, uint32_t bitmask, uint32_t bitvalue);
    ~RCRSession();

    /**
     * Route data from client to the backend.
     *
     * @param queue Buffer containing the data to route
     *
     * @return Returns 1 on success and 0 on error
     */
    int routeQuery(GWBUF* queue);

    void clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& pReply);

    bool handleError(mxs::ErrorType type, GWBUF* pMessage, mxs::Endpoint* pProblem, const mxs::Reply& pReply)
    {
        MXS_INFO("Server '%s' failed", pProblem->target()->name());
        return false;
    }

private:
    RCR*           m_instance;  /**< Router instance */
    uint32_t       m_bitmask;   /**< Bitmask to apply to server->status */
    uint32_t       m_bitvalue;  /**< Session specific required value of server->status */
    mxs::Endpoint* m_backend;
    Endpoints      m_endpoints;

    maxscale::SessionStats& m_session_stats;
    maxbase::StopWatch      m_session_timer;
    maxbase::IntervalTimer  m_query_timer;
    int64_t                 m_session_queries = 0;

    bool connection_is_valid() const;
};

/**
 * The per instance data for the router.
 */
class RCR : public mxs::Router<RCR, RCRSession>
{
public:
    /**
     * Create a new RadConn instance
     *
     * @param service The service this router is being create for
     * @param params  List of parameters for this service
     *
     * @return The new instance or nullptr on error
     */
    static RCR* create(SERVICE* service, mxs::ConfigParameters* params);

    /**
     * Create a new session for this router instance
     *
     * @param session The session object
     *
     * @return Router session or nullptr on error
     */
    RCRSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);

    /**
     * Get router diagnostics in JSON
     *
     * @return JSON data representing the router instance
     */
    json_t* diagnostics() const;

    /**
     * Get router capability bits
     *
     * @return The router capability bits
     */
    uint64_t getCapabilities();

    /**
     * Reconfigure the router instance
     *
     * @param params New configuration parameters
     *
     * @return True if router reconfiguration was successful
     */
    bool configure(mxs::ConfigParameters* params);

    /**
     * @brief session_stats
     *
     * @return a reference to the SessionStats of the Target (of the calling thread).
     */
    maxscale::SessionStats& session_stats(maxscale::Target* pTarget);

    /**
     * @brief Combine stats for all servers across all threads
     *
     * @return reference to the TargetSessionStats of this thread.
     */
    maxscale::TargetSessionStats combined_target_stats() const;

private:
    RCR(SERVICE* service);

    uint64_t m_bitmask_and_bitvalue = 0;    /**< Lower 32-bits for bitmask and upper for bitvalue */

    mxs::WorkerGlobal<maxscale::TargetSessionStats> m_target_stats;
};
