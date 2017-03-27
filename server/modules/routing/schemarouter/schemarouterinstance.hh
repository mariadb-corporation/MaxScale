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

#pragma once

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

protected:
    friend class SchemaRouterSession;
    schemarouter_config_t schemarouter_config;  /*< expanded config info from SERVICE */

    ShardManager          shard_manager;        /*< Shard maps hashed by user name */
    SERVICE*              service;              /*< Pointer to service                 */
    SPINLOCK              lock;                 /*< Lock for the instance data         */
    int                   schemarouter_version; /*< version number for router's config */
    ROUTER_STATS          stats;                /*< Statistics for this router         */
    set<string>           ignored_dbs;          /*< List of databases to ignore when the
                                                 * database mapping finds multiple servers
                                                 * with the same database */
    pcre2_code*           ignore_regex;         /*< Databases matching this regex will
                                                 * not cause the session to be terminated
                                                 * if they are found on more than one server. */
    pcre2_match_data*     ignore_match_data;

    SchemaRouter(SERVICE *service, char **options);
};
