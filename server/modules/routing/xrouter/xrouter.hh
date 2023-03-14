/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXB_MODULE_NAME "xrouter"

#include <maxscale/router.hh>
#include <maxscale/config2.hh>
#include <maxscale/protocol/postgresql/module_names.hh>

class XRouterSession;

class XRouter final : public mxs::Router
{
public:
    static constexpr uint64_t CAPS = RCAP_TYPE_QUERY_CLASSIFICATION | RCAP_TYPE_SESCMD_HISTORY
        | RCAP_TYPE_TRANSACTION_TRACKING;

    static XRouter*     create(SERVICE* pService);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;
    json_t*             diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return CAPS;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_POSTGRESQL_PROTOCOL_NAME};
    }

private:
    XRouter(const std::string& name);

    mxs::config::Configuration m_config;
};
