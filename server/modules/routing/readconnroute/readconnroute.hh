/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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

class RCR;

/**
 * The client session structure used within this router.
 */
class RCRSession : public mxs::RouterSession
{
public:
    RCRSession(RCR* inst, MXS_SESSION* session, mxs::Endpoint* backend,
               const Endpoints& endpoints, uint32_t bitmask, uint32_t bitvalue);
    ~RCRSession() = default;

    /**
     * Route data from client to the backend.
     *
     * @param queue Buffer containing the data to route
     *
     * @return Returns 1 on success and 0 on error
     */
    int routeQuery(GWBUF* queue);

    void clientReply(GWBUF* pPacket, mxs::Endpoint* pBackend, const mxs::Reply* pReply)
    {
        RouterSession::clientReply(pPacket, pBackend, pReply);
    }

    bool handleError(GWBUF* pMessage, mxs::Endpoint* pProblem)
    {
        return false;
    }

private:
    RCR*           m_instance;  /**< Router instance */
    uint32_t       m_bitmask;   /**< Bitmask to apply to server->status */
    uint32_t       m_bitvalue;  /**< Session specific required value of server->status */
    mxs::Endpoint* m_backend;
    Endpoints      m_endpoints;

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
    static RCR* create(SERVICE* service, MXS_CONFIG_PARAMETER* params);

    /**
     * Create a new session for this router instance
     *
     * @param session The session object
     *
     * @return Router session or nullptr on error
     */
    RCRSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);

    /**
     * Display router diagnostics
     *
     * @param dcb       DCB to send diagnostics to
     */
    void diagnostics(DCB* pDcb);

    /**
     * Get router diagnostics in JSON
     *
     * @return JSON data representing the router instance
     */
    json_t* diagnostics_json() const;

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
    bool configure(MXS_CONFIG_PARAMETER* params);

private:
    RCR(SERVICE* service);

    uint64_t m_bitmask_and_bitvalue = 0;    /**< Lower 32-bits for bitmask and upper for bitvalue */
};
