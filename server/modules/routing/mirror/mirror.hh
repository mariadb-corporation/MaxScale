/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "common.hh"

#include <maxscale/router.hh>
#include <maxscale/backend.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxbase/shared_mutex.hh>
#include <maxscale/service.hh>

#include "exporter.hh"
#include "config.hh"

class MirrorSession;

class Mirror : public mxs::Router
{
public:
    Mirror(const Mirror&) = delete;
    Mirror& operator=(const Mirror&) = delete;

    ~Mirror() = default;
    static Mirror*      create(SERVICE* pService);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;
    json_t*             diagnostics() const override;
    uint64_t            getCapabilities() const override;

    void ship(json_t* obj);

    mxs::Target* get_main() const
    {
        return m_config.main;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

    const Config& config() const
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
