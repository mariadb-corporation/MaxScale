#pragma once
#ifndef _SCHEMAROUTER_H
#define _SCHEMAROUTER_H
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

/**
 * @file schemarouter.h - The schemarouter router module header file
 *
 * @verbatim
 * Revision History
 *
 * See GitHub https://github.com/mariadb-corporation/MaxScale
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "schemarouter"

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/hashtable.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/pcre2.h>

MXS_BEGIN_DECLS

/**
 * Bitmask values for the router session's initialization. These values are used
 * to prevent responses from internal commands being forwarded to the client.
 */
typedef enum init_mask
{
    INIT_READY = 0x0,
    INIT_MAPPING = 0x1,
    INIT_USE_DB = 0x02,
    INIT_UNINT = 0x04,
    INIT_FAILED = 0x08
} init_mask_t;

typedef enum showdb_response
{
    SHOWDB_FULL_RESPONSE,
    SHOWDB_PARTIAL_RESPONSE,
    SHOWDB_DUPLICATE_DATABASES,
    SHOWDB_FATAL_ERROR
} showdb_response_t;

enum shard_map_state
{
    SHMAP_UNINIT, /*< No databases have been added to this shard map */
    SHMAP_READY, /*< All available databases have been added */
    SHMAP_STALE /*< The shard map has old data or has not been updated recently */
};

/**
 * A map of the shards tied to a single user.
 */
typedef struct shard_map
{
    HASHTABLE *hash; /*< A hashtable of database names and the servers which
                       * have these databases. */
    SPINLOCK lock;
    time_t last_updated;
    enum shard_map_state state; /*< State of the shard map */
} shard_map_t;

/**
 * The state of the backend server reference
 */
typedef enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /*< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /*< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_DB_MAPPED           = 0x10
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
/**
 * The type of the backend server
 */
typedef enum backend_type_t
{
    BE_UNDEFINED = -1,
    BE_MASTER,
    BE_JOINED = BE_MASTER,
    BE_SLAVE,
    BE_COUNT
} backend_type_t;

struct router_instance;

/**
 * Route target types
 */
typedef enum
{
    TARGET_UNDEFINED    = 0x00,
    TARGET_MASTER       = 0x01,
    TARGET_SLAVE        = 0x02,
    TARGET_NAMED_SERVER = 0x04,
    TARGET_ALL          = 0x08,
    TARGET_RLAG_MAX     = 0x10,
    TARGET_ANY          = 0x20
} route_target_t;

#define TARGET_IS_UNDEFINED(t)    (t == TARGET_UNDEFINED)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_ANY(t)          (t & TARGET_ANY)

typedef struct rses_property_st rses_property_t;
typedef struct router_client_session ROUTER_CLIENT_SES;

/**
 * Router session properties
 */
typedef enum rses_property_type_t
{
    RSES_PROP_TYPE_UNDEFINED = -1,
    RSES_PROP_TYPE_SESCMD = 0,
    RSES_PROP_TYPE_FIRST = RSES_PROP_TYPE_SESCMD,
    RSES_PROP_TYPE_TMPTABLES,
    RSES_PROP_TYPE_LAST = RSES_PROP_TYPE_TMPTABLES,
    RSES_PROP_TYPE_COUNT = RSES_PROP_TYPE_LAST + 1
} rses_property_type_t;

/** default values for rwsplit configuration parameters */
#define CONFIG_MAX_SLAVE_CONN 1
#define CONFIG_MAX_SLAVE_RLAG -1 /*< not used */
#define CONFIG_SQL_VARIABLES_IN TYPE_ALL

#define GET_SELECT_CRITERIA(s)                                                                  \
        (strncmp(s,"LEAST_GLOBAL_CONNECTIONS", strlen("LEAST_GLOBAL_CONNECTIONS")) == 0 ?       \
        LEAST_GLOBAL_CONNECTIONS : (                                                            \
        strncmp(s,"LEAST_BEHIND_MASTER", strlen("LEAST_BEHIND_MASTER")) == 0 ?                  \
        LEAST_BEHIND_MASTER : (                                                                 \
        strncmp(s,"LEAST_ROUTER_CONNECTIONS", strlen("LEAST_ROUTER_CONNECTIONS")) == 0 ?        \
        LEAST_ROUTER_CONNECTIONS : (                                                            \
        strncmp(s,"LEAST_CURRENT_OPERATIONS", strlen("LEAST_CURRENT_OPERATIONS")) == 0 ?        \
        LEAST_CURRENT_OPERATIONS : UNDEFINED_CRITERIA))))

/**
 * Session variable command
 */
typedef struct mysql_sescmd_st
{
#if defined(SS_DEBUG)
    skygw_chk_t        my_sescmd_chk_top;
#endif
    rses_property_t*   my_sescmd_prop;       /*< Parent property */
    GWBUF*             my_sescmd_buf;        /*< Query buffer */
    unsigned char      my_sescmd_packet_type;/*< Packet type */
    bool               my_sescmd_is_replied; /*< Is cmd replied to client */
    int      position; /*< Position of this command */
#if defined(SS_DEBUG)
    skygw_chk_t        my_sescmd_chk_tail;
#endif
} mysql_sescmd_t;


/**
 * Property structure
 */
struct rses_property_st
{
#if defined(SS_DEBUG)
    skygw_chk_t          rses_prop_chk_top;
#endif
    ROUTER_CLIENT_SES*   rses_prop_rsession; /*< Parent router session */
    int                  rses_prop_refcount; /*< Reference count*/
    rses_property_type_t rses_prop_type; /*< Property type */
    union rses_prop_data
    {
        mysql_sescmd_t  sescmd; /*< Session commands */
        HASHTABLE*  temp_tables; /*< Hashtable of table names */
    } rses_prop_data;
    rses_property_t*     rses_prop_next; /*< Next property of same type */
#if defined(SS_DEBUG)
    skygw_chk_t          rses_prop_chk_tail;
#endif
};

typedef struct sescmd_cursor_st
{
#if defined(SS_DEBUG)
    skygw_chk_t        scmd_cur_chk_top;
#endif
    ROUTER_CLIENT_SES* scmd_cur_rses;         /*< pointer to owning router session */
    rses_property_t**  scmd_cur_ptr_property; /*< address of pointer to owner property */
    mysql_sescmd_t*    scmd_cur_cmd;          /*< pointer to current session command */
    bool               scmd_cur_active;       /*< true if command is being executed */
    int      position; /*< Position of this cursor */
#if defined(SS_DEBUG)
    skygw_chk_t        scmd_cur_chk_tail;
#endif
} sescmd_cursor_t;

/**
 * Internal structure used to define the set of backend servers we are routing
 * connections to. This provides the storage for routing module specific data
 * that is required for each of the backend servers.
 *
 * Owned by router_instance, referenced by each routing session.
 */
typedef struct backend_st
{
#if defined(SS_DEBUG)
    skygw_chk_t     be_chk_top;
#endif
    SERVER*         backend_server;      /*< The server itself */
    int             backend_conn_count;  /*< Number of connections to
                          *  the server
                          */
    bool            be_valid;        /*< Valid when belongs to the
                          *  router's configuration
                          */
    int     weight;          /*< Desired weighting on the
                          *  load. Expressed in .1%
                          * increments
                          */
    struct stats
    {
        int queries;
    } stats;
#if defined(SS_DEBUG)
    skygw_chk_t     be_chk_tail;
#endif
} BACKEND;


/**
 * Reference to BACKEND.
 *
 * Owned by router client session.
 */
typedef struct backend_ref_st
{
#if defined(SS_DEBUG)
    skygw_chk_t     bref_chk_top;
#endif
    int             n_mapping_eof;
    GWBUF*          map_queue;
    SERVER_REF*     bref_backend; /*< Backend server */
    DCB*            bref_dcb; /*< Backend DCB */
    bref_state_t    bref_state; /*< State of the backend */
    bool            bref_mapped; /*< Whether the backend has been mapped */
    bool            last_sescmd_replied;
    int             bref_num_result_wait; /*< Number of not yet received results */
    sescmd_cursor_t bref_sescmd_cur; /*< Session command cursor */
    GWBUF*          bref_pending_cmd; /*< For stmt which can't be routed due active sescmd execution */
#if defined(SS_DEBUG)
    skygw_chk_t     bref_chk_tail;
#endif
} backend_ref_t;

/**
 * Configuration values
 */
typedef struct schemarouter_config_st
{
    int               rw_max_slave_conn_percent;
    int               rw_max_slave_conn_count;
    mxs_target_t      rw_use_sql_variables_in;
    int max_sescmd_hist;
    bool disable_sescmd_hist;
    time_t last_refresh; /*< Last time the database list was refreshed */
    double refresh_min_interval; /*< Minimum required interval between refreshes of databases */
    bool refresh_databases; /*< Are databases refreshed when they are not found in the hashtable */
    bool debug; /*< Enable verbose debug messages to clients */
} schemarouter_config_t;

/**
 * The statistics for this router instance
 */
typedef struct
{
    int     n_queries;  /*< Number of queries forwarded    */
    int             n_sescmd;       /*< Number of session commands */
    int             longest_sescmd; /*< Longest chain of stored session commands */
    int             n_hist_exceeded;/*< Number of sessions that exceeded session
                                         * command history limit */
    int sessions;
    double          ses_longest;      /*< Longest session */
    double          ses_shortest; /*< Shortest session */
    double          ses_average; /*< Average session length */
    int             shmap_cache_hit; /*< Shard map was found from the cache */
    int             shmap_cache_miss;/*< No shard map found from the cache */
} ROUTER_STATS;

/**
 * The client session structure used within this router.
 */
struct router_client_session
{
#if defined(SS_DEBUG)
    skygw_chk_t      rses_chk_top;
#endif
    SPINLOCK         rses_lock;      /*< protects rses_deleted                 */
    int              rses_versno;    /*< even = no active update, else odd. not used 4/14 */
    bool             rses_closed;    /*< true when closeSession is called      */
    DCB*             rses_client_dcb;
    MYSQL_session*   rses_mysql_session; /*< Session client data (username, password, SHA1). */
    /** Properties listed by their type */
    rses_property_t* rses_properties[RSES_PROP_TYPE_COUNT]; /*< Session properties */
    backend_ref_t*   rses_master_ref; /*< Router session master reference */
    backend_ref_t*   rses_backend_ref; /*< Pointer to backend reference array */
    schemarouter_config_t rses_config;    /*< Copied config info from router instance */
    int              rses_nbackends; /*< Number of backends */
    bool             rses_autocommit_enabled; /*< Is autocommit enabled */
    bool             rses_transaction_active; /*< Is a transaction active */
    struct router_instance   *router;   /*< The router instance */
    struct router_client_session* next; /*< List of router sessions */
    shard_map_t*
    shardmap; /*< Database hash containing names of the databases mapped to the servers that contain them */
    char            connect_db[MYSQL_DATABASE_MAXLEN + 1]; /*< Database the user was trying to connect to */
    char            current_db[MYSQL_DATABASE_MAXLEN + 1]; /*< Current active database */
    init_mask_t    init; /*< Initialization state bitmask */
    GWBUF*          queue; /*< Query that was received before the session was ready */
    DCB*            dcb_route; /*< Internal DCB used to trigger re-routing of buffers */
    DCB*            dcb_reply; /*< Internal DCB used to send replies to the client */
    ROUTER_STATS    stats;     /*< Statistics for this router         */
    int             n_sescmd;
    int             pos_generator;
#if defined(SS_DEBUG)
    skygw_chk_t      rses_chk_tail;
#endif
};


/**
 * The per instance data for the router.
 */
typedef struct router_instance
{
    HASHTABLE*              shard_maps;  /*< Shard maps hashed by user name */
    SERVICE*                service;     /*< Pointer to service                 */
    ROUTER_CLIENT_SES*      connections; /*< List of client connections         */
    SPINLOCK                lock;        /*< Lock for the instance data         */
    schemarouter_config_t        schemarouter_config; /*< expanded config info from SERVICE */
    int                     schemarouter_version;/*< version number for router's config */
    unsigned int            bitmask;     /*< Bitmask to apply to server->status */
    unsigned int            bitvalue;    /*< Required value of server->status   */
    ROUTER_STATS            stats;       /*< Statistics for this router         */
    struct router_instance* next;        /*< Next router on the list            */
    bool                    available_slaves; /*< The router has some slaves available */
    HASHTABLE*              ignored_dbs; /*< List of databases to ignore when the
                                          * database mapping finds multiple servers
                                          * with the same database */
    pcre2_code*             ignore_regex; /*< Databases matching this regex will
                                           * not cause the session to be terminated
                                           * if they are found on more than one server. */
    pcre2_match_data*       ignore_match_data;
    SERVER*                 preferred_server; /**< Server to prefer in conflict situations */

} ROUTER_INSTANCE;

#define BACKEND_TYPE(b) (SERVER_IS_MASTER((b)->backend_server) ? BE_MASTER :    \
        (SERVER_IS_SLAVE((b)->backend_server) ? BE_SLAVE :  BE_UNDEFINED));

MXS_END_DECLS

#endif /*< _SCHEMAROUTER_H */
