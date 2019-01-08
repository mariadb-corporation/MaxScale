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

/**
 * @file readconnection.hh - The read connection balancing query module header file
 */

#define MXS_MODULE_NAME "readconnroute"

#include <maxscale/ccdefs.hh>
#include <maxscale/dcb.hh>
#include <maxscale/service.hh>
#include <maxscale/router.hh>

class ReadConn;

/**
 * The client session structure used within this router.
 */
class ReadConnSession : public mxs::RouterSession
{
public:
    ReadConnSession(ReadConn* inst, MXS_SESSION* session, SERVER_REF* backend, DCB* dcb,
                    uint32_t bitmask, uint32_t bitvalue);
    ~ReadConnSession();

    int routeQuery(GWBUF*);

    void close();
    void clientReply(GWBUF* pPacket, DCB* pBackend);
    void handleError(GWBUF* pMessage,
                     DCB*   pProblem,
                     mxs_error_action_t action,
                     bool* pSuccess);

    ReadConn*   instance;
    SERVER_REF* backend;    /*< Backend used by the client session */
    DCB*        backend_dcb;/*< DCB Connection to the backend      */
    DCB*        client_dcb; /**< Client DCB */
    uint32_t    bitmask;    /*< Bitmask to apply to server->status */
    uint32_t    bitvalue;   /*< Session specific required value of server->status */
};

/**
 * The statistics for this router instance
 */
struct Stats
{
    int n_sessions = 0;     /*< Number sessions created     */
    int n_queries = 0;      /*< Number of queries forwarded */
};

/**
 * The per instance data for the router.
 */
class ReadConn : public mxs::Router<ReadConn, ReadConnSession>
{
public:
    static ReadConn* create(SERVICE* service, MXS_CONFIG_PARAMETER* params);

    ReadConnSession* newSession(MXS_SESSION* pSession);
    void             diagnostics(DCB* pDcb);
    json_t*          diagnostics_json() const;
    uint64_t         getCapabilities();
    bool             configure(MXS_CONFIG_PARAMETER* params);
    bool             connection_is_valid(ReadConnSession* router_cli_ses);

    uint64_t bitmask_and_bitvalue = 0;  /*< Lower 32-bits for bitmask and upper for bitvalue */
    Stats    stats;                     /*< Statistics for this router               */

private:
    ReadConn(SERVICE* service);
};
