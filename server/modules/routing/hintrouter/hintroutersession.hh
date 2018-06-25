#pragma once
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

#include "hintrouterdefs.hh"

#include <deque>
#include <tr1/unordered_map>
#include <vector>
#include <string>

#include <maxscale/router.hh>
#include "dcb.hh"

using std::string;

class HintRouter;

class HintRouterSession : public maxscale::RouterSession
{
public:
    typedef std::tr1::unordered_map<string, Dcb> BackendMap; // All backends, indexed by name
    typedef std::vector<Dcb> BackendArray;
    typedef std::vector<SERVER_REF*> RefArray;
    typedef BackendMap::value_type MapElement;
    typedef BackendArray::size_type size_type;

    HintRouterSession(MXS_SESSION*    pSession,
                      HintRouter*     pRouter,
                      const BackendMap& backends
                     );

    ~HintRouterSession();

    void close();

    int32_t routeQuery(GWBUF* pPacket);

    void clientReply(GWBUF* pPacket, DCB* pBackend);

    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);

private:
    HintRouterSession(const HintRouterSession&); // denied
    HintRouterSession& operator = (const HintRouterSession&); // denied
private:
    bool route_by_hint(GWBUF* pPacket, HINT* current_hint, bool ignore_errors);
    bool route_to_slave(GWBUF* pPacket, bool print_errors);
    void update_connections();

    HintRouter* m_router;
    BackendMap m_backends; // all connections
    Dcb m_master; // connection to master
    BackendArray m_slaves; // connections to slaves
    size_type m_n_routed_to_slave; // packets routed to a single slave, used for rr
    size_type m_surplus_replies; // how many replies should be ignored
};
