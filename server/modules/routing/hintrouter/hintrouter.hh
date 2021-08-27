/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "hintrouterdefs.hh"

#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include "hintroutersession.hh"

class HintRouter : public mxs::Router
{
public:

    struct Config : public mxs::config::Configuration
    {
        Config(const char* name);

        HINT_TYPE   default_action;
        std::string default_server;
        int64_t     max_slaves;
    };

    static HintRouter* create(SERVICE* pService);
    HintRouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;
    json_t*            diagnostics() const override;
    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_NONE;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    HINT_TYPE get_default_action() const
    {
        return m_config.default_action;
    }
    const string& get_default_server() const
    {
        return m_config.default_server;
    }
    /* Simple, approximate statistics */
    volatile unsigned int m_routed_to_master;
    volatile unsigned int m_routed_to_slave;
    volatile unsigned int m_routed_to_named;
    volatile unsigned int m_routed_to_all;
private:
    HintRouter(SERVICE* pService);

    volatile int m_total_slave_conns;
    Config       m_config;
private:
    HintRouter(const HintRouter&);
    HintRouter& operator=(const HintRouter&);

    static bool connect_to_backend(MXS_SESSION* session,
                                   mxs::Endpoint* sref,
                                   HintRouterSession::BackendMap* all_backends);
};
