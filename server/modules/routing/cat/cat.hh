/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/router.hh>
#include <maxscale/config2.hh>

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
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints);
    json_t*             diagnostics() const;
    uint64_t            getCapabilities() const;

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

private:
    friend class CatSession;

    Cat(const std::string& name);

    mxs::config::Configuration m_config;
};
