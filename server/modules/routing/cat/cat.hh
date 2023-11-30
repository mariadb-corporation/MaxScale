/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXB_MODULE_NAME "cat"

#include <maxscale/router.hh>
#include <maxscale/config2.hh>
#include <maxscale/protocol/mariadb/module_names.hh>

class CatSession;

/**
 * The per instance data for the router.
 */
class Cat : public mxs::Router
{
public:
    Cat(const Cat&) = delete;
    Cat& operator=(const Cat&) = delete;

    static Cat*         create(SERVICE* pService);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;
    json_t*             diagnostics() const override;
    uint64_t            getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

private:
    friend class CatSession;

    Cat(const std::string& name);

    mxs::config::Configuration m_config;
};
