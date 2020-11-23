/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/router.hh>

class CatSession;

/**
 * The per instance data for the router.
 */
class Cat : public mxs::Router
{
    Cat(const Cat&) = delete;
    Cat& operator=(const Cat&) = delete;
public:
    ~Cat();
    static Cat*         create(SERVICE* pService, mxs::ConfigParameters* params);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints);
    json_t*             diagnostics() const;
    uint64_t            getCapabilities() const;

    bool configure(mxs::ConfigParameters* params)
    {
        return false;
    }

    mxs::config::Configuration* getConfiguration()
    {
        return nullptr;
    }

private:
    friend class CatSession;

    /** Internal functions */
    Cat() = default;
};
