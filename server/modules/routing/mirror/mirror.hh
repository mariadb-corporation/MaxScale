/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "common.hh"

#include <maxscale/router.hh>
#include <maxscale/backend.hh>
#include <maxbase/shared_mutex.hh>

#include "exporter.hh"
#include "config.hh"

class MirrorSession;

class Mirror : public mxs::Router
{
public:
    Mirror(const Mirror&) = delete;
    Mirror& operator=(const Mirror&) = delete;

    ~Mirror() = default;
    static Mirror*      create(SERVICE* pService, mxs::ConfigParameters* params);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints);
    json_t*             diagnostics() const;
    uint64_t            getCapabilities() const;

    bool configure(mxs::ConfigParameters* params)
    {
        return true;
    }

    void ship(json_t* obj);

    mxs::Target* get_main() const
    {
        return m_config.main;
    }

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    bool post_configure();

private:
    Mirror(SERVICE* pService)
        : m_config(pService->name(), this)
        , m_service(pService)
    {
    }

    Config                    m_config;
    std::unique_ptr<Exporter> m_exporter;
    mxb::shared_mutex         m_rw_lock;
    SERVICE*                  m_service;
};
