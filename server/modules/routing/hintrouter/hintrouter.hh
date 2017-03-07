#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/router.hh>
#include "hintroutersession.hh"

class HintRouter : public maxscale::Router<HintRouter, HintRouterSession>
{
public:
    static HintRouter* create(SERVICE* pService, char** pzOptions);

    HintRouterSession* newSession(MXS_SESSION *pSession);

    void diagnostics(DCB* pOut);

    uint64_t getCapabilities();

private:
    HintRouter(SERVICE* pService);

private:
    HintRouter(const HintRouter&);
    HintRouter& operator = (const HintRouter&);
};
