/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#define MXS_MODULE_NAME "smartrouter"

/**
 * @file Smart Router. Routes queries to the best router for the type of query.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/router.hh>

class SmartRouterSession;

/** class Smartrouter. Only defines the mxs::Router<> functions needed for all routers.
 */
class SmartRouter : public mxs::Router<SmartRouter, SmartRouterSession>
{
public:
    static SmartRouter* create(SERVICE* pService, MXS_CONFIG_PARAMETER* pParams);

    SmartRouterSession* newSession(MXS_SESSION* pSession);

    void     diagnostics(DCB* pDcb);
    json_t*  diagnostics_json() const;
    uint64_t getCapabilities();
    bool     configure(MXS_CONFIG_PARAMETER* pParams);

    SERVICE* service() const;
private:
    SmartRouter(SERVICE* service);
};
