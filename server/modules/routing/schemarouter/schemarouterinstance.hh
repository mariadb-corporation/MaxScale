/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "schemarouter.hh"

#include <mutex>
#include <set>
#include <string>

#include <maxscale/router.hh>
#include <maxscale/pcre2.hh>

#include "schemaroutersession.hh"

namespace schemarouter
{

class SchemaRouterSession;

/**
 * The per instance data for the router.
 */
class SchemaRouter : public Router
{
public:
    static SchemaRouter* create(SERVICE* pService);
    mxs::RouterSession*  newSession(MXS_SESSION* pSession, const Endpoints& endpoints) override;
    json_t*              diagnostics() const override;
    uint64_t             getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

private:
    friend class SchemaRouterSession;

    /** Internal functions */
    SchemaRouter(SERVICE* service);

    /** Member variables */
    Config       m_config;
    ShardManager m_shard_manager;   /*< Shard maps hashed by user name */
    SERVICE*     m_service;         /*< Pointer to service */
    std::mutex   m_lock;            /*< Lock for the instance data */
    Stats        m_stats;           /*< Statistics for this router */
};
}
