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

/**
 * @file schemarouter.hh - The schemarouter router module header file
 */

#define MXS_MODULE_NAME "schemarouter"

#include <maxscale/cdefs.h>

#include <set>
#include <string>

#include <maxscale/dcb.h>
#include <maxscale/hashtable.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/pcre2.h>

#include "shard_map.hh"
#include "session_command.hh"

using std::string;
using std::set;

/**
 * Bitmask values for the router session's initialization. These values are used
 * to prevent responses from internal commands being forwarded to the client.
 */
typedef enum init_mask
{
    INIT_READY   = 0x00,
    INIT_MAPPING = 0x01,
    INIT_USE_DB  = 0x02,
    INIT_UNINT   = 0x04,
    INIT_FAILED  = 0x08
} init_mask_t;

typedef enum showdb_response
{
    SHOWDB_FULL_RESPONSE,
    SHOWDB_PARTIAL_RESPONSE,
    SHOWDB_DUPLICATE_DATABASES,
    SHOWDB_FATAL_ERROR
} showdb_response_t;

/**
 * The state of the backend server reference
 */
typedef enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /**< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /**< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_DB_MAPPED        = 0x10
} bref_state_t;

#define BREF_IS_NOT_USED(s)         ((s)->bref_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->bref_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->bref_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->bref_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->bref_state & BREF_CLOSED)
#define BREF_IS_MAPPED(s)           ((s)->bref_mapped)

#define SCHEMA_ERR_DUPLICATEDB 5000
#define SCHEMA_ERRSTR_DUPLICATEDB "DUPDB"
#define SCHEMA_ERR_DBNOTFOUND 1049
#define SCHEMA_ERRSTR_DBNOTFOUND "42000"

struct schemarouter_instance;

/**
 * Route target types
 */
typedef enum
{
    TARGET_UNDEFINED    = (1 << 0),
    TARGET_NAMED_SERVER = (1 << 1),
    TARGET_ALL          = (1 << 2),
    TARGET_ANY          = (1 << 3)
} route_target_t;

/** Helper macros for route target type */
#define TARGET_IS_UNDEFINED(t)    (t == TARGET_UNDEFINED)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_ANY(t)          (t & TARGET_ANY)

/**
 * Reference to BACKEND.
 *
 * Owned by router client session.
 */
typedef struct backend_ref_st
{
    int                n_mapping_eof;
    GWBUF*             map_queue;
    SERVER_REF*        bref_backend;         /*< Backend server */
    DCB*               bref_dcb;             /*< Backend DCB */
    int                bref_state;           /*< State of the backend */
    bool               bref_mapped;          /*< Whether the backend has been mapped */
    bool               last_sescmd_replied;
    int                bref_num_result_wait; /*< Number of not yet received results */
    GWBUF*             bref_pending_cmd;     /*< Pending commands */

    SessionCommandList session_commands;     /**< List of session commands that are
                                              * to be executed on this backend server */
} backend_ref_t;

/**
 * Configuration values
 */
typedef struct schemarouter_config_st
{
    double refresh_min_interval; /**< Minimum required interval between refreshes of databases */
    bool   refresh_databases;    /**< Are databases refreshed when they are not found in the hashtable */
    bool   debug;                /**< Enable verbose debug messages to clients */
} schemarouter_config_t;

/**
 * The statistics for this router instance
 */
typedef struct
{
    int    n_queries;        /*< Number of queries forwarded    */
    int    n_sescmd;         /*< Number of session commands */
    int    longest_sescmd;   /*< Longest chain of stored session commands */
    int    n_hist_exceeded;  /*< Number of sessions that exceeded session
                              * command history limit */
    int    sessions;
    double ses_longest;      /*< Longest session */
    double ses_shortest;     /*< Shortest session */
    double ses_average;      /*< Average session length */
    int    shmap_cache_hit;  /*< Shard map was found from the cache */
    int    shmap_cache_miss; /*< No shard map found from the cache */
} ROUTER_STATS;

/**
 * The per instance data for the router.
 */
typedef struct schemarouter_instance
{
    ShardManager          shard_manager;        /*< Shard maps hashed by user name */
    SERVICE*              service;              /*< Pointer to service                 */
    SPINLOCK              lock;                 /*< Lock for the instance data         */
    schemarouter_config_t schemarouter_config;  /*< expanded config info from SERVICE */
    int                   schemarouter_version; /*< version number for router's config */
    ROUTER_STATS          stats;                /*< Statistics for this router         */
    set<string>           ignored_dbs;          /*< List of databases to ignore when the
                                                 * database mapping finds multiple servers
                                                 * with the same database */
    pcre2_code*           ignore_regex;         /*< Databases matching this regex will
                                                 * not cause the session to be terminated
                                                 * if they are found on more than one server. */
    pcre2_match_data*     ignore_match_data;

} SCHEMAROUTER;

/**
 * The client session structure used within this router.
 */
typedef struct schemarouter_session
{
    bool                   closed;             /*< true when closeSession is called      */
    DCB*                   rses_client_dcb;
    MYSQL_session*         rses_mysql_session; /*< Session client data (username, password, SHA1). */
    backend_ref_t*         rses_backend_ref;   /*< Pointer to backend reference array */
    schemarouter_config_t  rses_config;        /*< Copied config info from router instance */
    int                    rses_nbackends;     /*< Number of backends */
    SCHEMAROUTER          *router;             /*< The router instance */
    Shard                  shardmap;           /**< Database hash containing names of the databases
                                                * mapped to the servers that contain them */
    string                 connect_db;         /*< Database the user was trying to connect to */
    string                 current_db;         /*< Current active database */
    int                    state;              /*< Initialization state bitmask */
    GWBUF*                 queue;              /*< Query that was received before the session was ready */
    ROUTER_STATS           stats;              /*< Statistics for this router         */

    uint64_t               sent_sescmd;        /**< The latest session command being executed */
    uint64_t               replied_sescmd;     /**< The last session command reply that was sent to the client */
} SCHEMAROUTER_SESSION;
