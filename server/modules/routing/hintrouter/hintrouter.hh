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

#include <maxscale/router.hh>
#include "hintroutersession.hh"

class HintRouter : public maxscale::Router<HintRouter, HintRouterSession>
{
public:
    static HintRouter* create(SERVICE* pService, MXS_CONFIG_PARAMETER* params);
    HintRouterSession* newSession(MXS_SESSION *pSession);
    void diagnostics(DCB* pOut);
    json_t* diagnostics_json() const;
    uint64_t getCapabilities() const
    {
        return RCAP_TYPE_NONE;
    }
    HINT_TYPE get_default_action() const
    {
        return m_default_action;
    };
    const string& get_default_server() const
    {
        return m_default_server;
    };
    /* Simple, approximate statistics */
    volatile unsigned int m_routed_to_master;
    volatile unsigned int m_routed_to_slave;
    volatile unsigned int m_routed_to_named;
    volatile unsigned int m_routed_to_all;
private:
    HintRouter(SERVICE* pService, HINT_TYPE default_action, string& default_server,
               int max_slaves);

    HINT_TYPE m_default_action;
    string m_default_server;
    int m_max_slaves;
    volatile int m_total_slave_conns;
private:
    HintRouter(const HintRouter&);
    HintRouter& operator = (const HintRouter&);

    static Dcb connect_to_backend(MXS_SESSION* session, SERVER_REF* sref,
                                  HintRouterSession::BackendMap* all_backends);
};
