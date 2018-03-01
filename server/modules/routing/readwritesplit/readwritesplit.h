#pragma once
#ifndef _RWSPLITROUTER_H
#define _RWSPLITROUTER_H
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
 * @file router.h - The read write split router module heder file
 *
 * @verbatim
 * Revision History
 *
 * See GitHub https://github.com/mariadb-corporation/MaxScale
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "readwritesplit"

#include <maxscale/cdefs.h>

#include <math.h>

#include <maxscale/dcb.h>
#include <maxscale/hashtable.h>
#include <maxscale/router.h>
#include <maxscale/service.h>

MXS_BEGIN_DECLS

typedef enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /*< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /*< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_FATAL_FAILURE    = 0x10 /*< Backend references that should be dropped */
} bref_state_t;

#define BREF_IS_NOT_USED(s)         ((s)->bref_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->bref_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->bref_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->bref_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->bref_state & BREF_CLOSED)
#define BREF_HAS_FAILED(s)          ((s)->bref_state & BREF_FATAL_FAILURE)

typedef enum backend_type_t
{
    BE_UNDEFINED = -1,
    BE_MASTER,
    BE_JOINED = BE_MASTER,
    BE_SLAVE,
    BE_COUNT
} backend_type_t;

struct router_instance;

typedef enum
{
    TARGET_UNDEFINED    = 0x00,
    TARGET_MASTER       = 0x01,
    TARGET_SLAVE        = 0x02,
    TARGET_NAMED_SERVER = 0x04,
    TARGET_ALL          = 0x08,
    TARGET_RLAG_MAX     = 0x10
} route_target_t;

#define TARGET_IS_MASTER(t)       (t & TARGET_MASTER)
#define TARGET_IS_SLAVE(t)        (t & TARGET_SLAVE)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_RLAG_MAX(t)     (t & TARGET_RLAG_MAX)

typedef struct rses_property_st rses_property_t;
typedef struct router_client_session ROUTER_CLIENT_SES;

typedef enum rses_property_type_t
{
    RSES_PROP_TYPE_UNDEFINED = -1,
    RSES_PROP_TYPE_SESCMD    = 0,
    RSES_PROP_TYPE_FIRST     = RSES_PROP_TYPE_SESCMD,
    RSES_PROP_TYPE_TMPTABLES,
    RSES_PROP_TYPE_LAST      = RSES_PROP_TYPE_TMPTABLES,
    RSES_PROP_TYPE_COUNT     = RSES_PROP_TYPE_LAST + 1
} rses_property_type_t;

/**
 * This criteria is used when backends are chosen for a router session's use.
 * Backend servers are sorted to ascending order according to the criteria
 * and top N are chosen.
 */
typedef enum select_criteria
{
    UNDEFINED_CRITERIA = 0,
    LEAST_GLOBAL_CONNECTIONS,   /*< all connections established by MaxScale */
    LEAST_ROUTER_CONNECTIONS,   /*< connections established by this router */
    LEAST_BEHIND_MASTER,
    LEAST_CURRENT_OPERATIONS,
    DEFAULT_CRITERIA   = LEAST_CURRENT_OPERATIONS,
    LAST_CRITERIA               /*< not used except for an index */
} select_criteria_t;

static inline const char* select_criteria_to_str(select_criteria_t type)
{
    switch (type)
    {
    case LEAST_GLOBAL_CONNECTIONS:
        return "LEAST_GLOBAL_CONNECTIONS";

    case LEAST_ROUTER_CONNECTIONS:
        return "LEAST_ROUTER_CONNECTIONS";

    case LEAST_BEHIND_MASTER:
        return "LEAST_BEHIND_MASTER";

    case LEAST_CURRENT_OPERATIONS:
        return "LEAST_CURRENT_OPERATIONS";

    default:
        return "UNDEFINED_CRITERIA";
    }
}

/**
 * Controls how master failure is handled
 */
enum failure_mode
{
    RW_FAIL_INSTANTLY, /**< Close the connection as soon as the master is lost */
    RW_FAIL_ON_WRITE, /**< Close the connection when the first write is received */
    RW_ERROR_ON_WRITE /**< Don't close the connection but send an error for writes */
};

static inline const char* failure_mode_to_str(enum failure_mode type)
{
    switch (type)
    {
    case RW_FAIL_INSTANTLY:
        return "fail_instantly";

    case RW_FAIL_ON_WRITE:
        return "fail_on_write";

    case RW_ERROR_ON_WRITE:
        return "error_on_write";

    default:
        ss_dassert(false);
        return "UNDEFINED_MODE";
    }
}

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
    rses_property_t*   my_sescmd_prop;       /*< parent property */
    GWBUF*             my_sescmd_buf;        /*< query buffer */
    unsigned char      my_sescmd_packet_type; /*< packet type */
    bool               my_sescmd_is_replied; /*< is cmd replied to client */
    unsigned char      reply_cmd; /*< The reply command. One of OK, ERR, RESULTSET or
                                   *  LOCAL_INFILE. Slave servers are compared to this
                                   *  when they return session command replies.*/
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
    ROUTER_CLIENT_SES*   rses_prop_rsession; /*< parent router session */
    int                  rses_prop_refcount;
    rses_property_type_t rses_prop_type;

    union rses_prop_data
    {
        mysql_sescmd_t   sescmd;
        HASHTABLE*       temp_tables;
    } rses_prop_data;
    rses_property_t*     rses_prop_next; /*< next property of same type */
#if defined(SS_DEBUG)
    skygw_chk_t          rses_prop_chk_tail;
#endif
} ;

typedef struct sescmd_cursor_st
{
#if defined(SS_DEBUG)
    skygw_chk_t        scmd_cur_chk_top;
#endif
    ROUTER_CLIENT_SES* scmd_cur_rses;         /*< pointer to owning router session */
    rses_property_t**  scmd_cur_ptr_property; /*< address of pointer to owner property */
    mysql_sescmd_t*    scmd_cur_cmd;          /*< pointer to current session command */
    bool               scmd_cur_active;       /*< true if command is being executed */
    int                position; /*< Position of this cursor */
#if defined(SS_DEBUG)
    skygw_chk_t        scmd_cur_chk_tail;
#endif
} sescmd_cursor_t;

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
    SERVER_REF*     ref;
    DCB*            bref_dcb;
    bref_state_t    bref_state;
    int             bref_num_result_wait;
    sescmd_cursor_t bref_sescmd_cur;
    GWBUF*          bref_pending_cmd; /**< For stmt which can't be routed due active sescmd execution */
    unsigned char   reply_cmd;  /**< The reply the backend server sent to a session command.
                                 * Used to detect slaves that fail to execute session command. */
#if defined(SS_DEBUG)
    skygw_chk_t     bref_chk_tail;
#endif
    int closed_at; /** DEBUG: Line number where this backend reference was closed */
} backend_ref_t;

typedef struct rwsplit_config_st
{
    int               rw_max_slave_conn_percent; /**< Maximum percentage of slaves
                                                  * to use for each connection*/
    int               max_slave_connections; /**< Maximum number of slaves for each connection*/
    select_criteria_t slave_selection_criteria; /**< The slave selection criteria */
    int               max_slave_replication_lag; /**< Maximum replication lag */
    mxs_target_t      use_sql_variables_in; /**< Whether to send user variables
                                                * to master or all nodes */
    int               max_sescmd_history; /**< Maximum amount of session commands to store */
    bool              disable_sescmd_history; /**< Disable session command history */
    bool              master_accept_reads; /**< Use master for reads */
    bool              strict_multi_stmt; /**< Force non-multistatement queries to be routed
                                             * to the master after a multistatement query. */
    bool              strict_sp_calls; /**< Lock session to master after an SP call */
    enum failure_mode master_failure_mode; /**< Master server failure handling mode.
                                               * @see enum failure_mode */
    bool              retry_failed_reads; /**< Retry failed reads on other servers */
} rwsplit_config_t;

#if defined(PREP_STMT_CACHING)

typedef struct prep_stmt_st
{
#if defined(SS_DEBUG)
    skygw_chk_t       pstmt_chk_top;
#endif

    union id
    {
        int   seq;
        char* name;
    } pstmt_id;
    prep_stmt_state_t pstmt_state;
    prep_stmt_type_t  pstmt_type;
#if defined(SS_DEBUG)
    skygw_chk_t       pstmt_chk_tail;
#endif
} prep_stmt_t;

#endif /*< PREP_STMT_CACHING */

/**
 * The client session structure used within this router.
 */
struct router_client_session
{
#if defined(SS_DEBUG)
    skygw_chk_t      rses_chk_top;
#endif
    bool             rses_closed;    /*< true when closeSession is called */
    rses_property_t* rses_properties[RSES_PROP_TYPE_COUNT]; /*< Properties listed by their type */
    backend_ref_t*   rses_master_ref;
    backend_ref_t*   rses_backend_ref; /*< Pointer to backend reference array */
    rwsplit_config_t rses_config;    /*< copied config info from router instance */
    int              rses_nbackends;
    int              rses_nsescmd;  /*< Number of executed session commands */
    bool             rses_load_active; /*< If LOAD DATA LOCAL INFILE is being currently executed */
    bool             have_tmp_tables;
    uint64_t         rses_load_data_sent; /*< How much data has been sent */
    DCB*             client_dcb;
    int              pos_generator;
    backend_ref_t    *forced_node; /*< Current server where all queries should be sent */
#if defined(PREP_STMT_CACHING)
    HASHTABLE*       rses_prep_stmt[2];
#endif
    struct router_instance *router;   /*< The router instance */
    struct router_client_session *next;
#if defined(SS_DEBUG)
    skygw_chk_t      rses_chk_tail;
#endif
} ;

/**
 * The statistics for this router instance
 */
typedef struct
{
    uint64_t n_sessions; /*< Number sessions created */
    uint64_t n_queries;  /*< Number of queries forwarded */
    uint64_t n_master;   /*< Number of stmts sent to master */
    uint64_t n_slave;    /*< Number of stmts sent to slave */
    uint64_t n_all;      /*< Number of stmts sent to all */
} ROUTER_STATS;

/**
 * The per instance data for the router.
 */
typedef struct router_instance
{
    SERVICE*                service;     /*< Pointer to service */
    rwsplit_config_t        rwsplit_config; /*< expanded config info from SERVICE */
    int                     rwsplit_version; /*< version number for router's config */
    ROUTER_STATS            stats;       /*< Statistics for this router */
    bool                    available_slaves; /*< The router has some slaves avialable */
} ROUTER_INSTANCE;

#define BACKEND_TYPE(b) (SERVER_IS_MASTER((b)->backend_server) ? BE_MASTER :    \
        (SERVER_IS_SLAVE((b)->backend_server) ? BE_SLAVE :  BE_UNDEFINED));

MXS_END_DECLS

#endif /*< _RWSPLITROUTER_H */
