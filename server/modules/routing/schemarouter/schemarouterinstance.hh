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

#include "schemarouter.hh"

#include <set>
#include <string>

#include <maxscale/router.hh>
#include <maxscale/pcre2.h>

#include "schemaroutersession.hh"

using std::string;
using std::set;

class SchemaRouterSession;

/**
 * The per instance data for the router.
 */
class SchemaRouter: public mxs::Router<SchemaRouter, SchemaRouterSession>
{
public:
    ~SchemaRouter();
    static SchemaRouter* create(SERVICE* pService, char** pzOptions);
    SchemaRouterSession* newSession(MXS_SESSION* pSession);
    void diagnostics(DCB* pDcb);
    uint64_t getCapabilities();

private:
    friend class SchemaRouterSession;

    /** Internal functions */
    SchemaRouter(SERVICE *service, char **options);

    /** Member variables */
    schemarouter_config_t m_config;        /*< expanded config info from SERVICE */
    ShardManager          m_shard_manager; /*< Shard maps hashed by user name */
    SERVICE*              m_service;       /*< Pointer to service */
    SPINLOCK              m_lock;          /*< Lock for the instance data */
    ROUTER_STATS          m_stats;         /*< Statistics for this router */
    set<string>           m_ignored_dbs;   /*< List of databases to ignore when the
                                            * database mapping finds multiple servers
                                            * with the same database */
    pcre2_code*           m_ignore_regex;  /*< Databases matching this regex will
                                            * not cause the session to be terminated
                                            * if they are found on more than one server. */
    pcre2_match_data*     m_ignore_match_data;
};
