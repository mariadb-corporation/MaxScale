#ifndef _SHARDROUTER_H
#define _SHARDROUTER_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file shardrouter.h - The sharding router module header file
 *
 * @verbatim
 * Revision History
 *
 * See GitHub https://github.com/mariadb-corporation/MaxScale
 *
 * @endverbatim
 */

#include <dcb.h>
#include <hashtable.h>
#include <mysql_client_server_protocol.h>

struct router_instance;

typedef enum {
	TARGET_UNDEFINED    = 0x00,
	TARGET_MASTER       = 0x01,
	TARGET_SLAVE        = 0x02,
	TARGET_NAMED_SERVER = 0x04,
	TARGET_ALL          = 0x08,
	TARGET_RLAG_MAX     = 0x10,
	TARGET_ANY			= 0x20
} route_target_t;

#define TARGET_IS_UNDEFINED(t)	  (t == TARGET_UNDEFINED)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_ANY(t)          (t & TARGET_ANY)

typedef struct rses_property_st rses_property_t;
typedef struct router_client_session ROUTER_CLIENT_SES;

typedef enum rses_property_type_t {
        RSES_PROP_TYPE_UNDEFINED=-1,
        RSES_PROP_TYPE_SESCMD=0,
        RSES_PROP_TYPE_FIRST = RSES_PROP_TYPE_SESCMD,
        RSES_PROP_TYPE_TMPTABLES,
        RSES_PROP_TYPE_LAST=RSES_PROP_TYPE_TMPTABLES,
	RSES_PROP_TYPE_COUNT=RSES_PROP_TYPE_LAST+1
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
        


#define SUBSVC_IS_MAPPED(s)         (s->state & SUBSVC_MAPPED)
#define SUBSVC_IS_CLOSED(s)         (s->state & SUBSVC_CLOSED)
#define SUBSVC_IS_OK(s)         (s->state & SUBSVC_OK)
#define SUBSVC_IS_WAITING(s)         (s->state & SUBSVC_WAITING_RESULT)

/**
 * Session variable command
 */
typedef struct mysql_sescmd_st {
#if defined(SS_DEBUG)
        skygw_chk_t        my_sescmd_chk_top;
#endif
	rses_property_t*   my_sescmd_prop;       /*< parent property */
        GWBUF*             my_sescmd_buf;        /*< query buffer */
        unsigned char      my_sescmd_packet_type;/*< packet type */
	bool               my_sescmd_is_replied; /*< is cmd replied to client */
#if defined(SS_DEBUG)
        skygw_chk_t        my_sescmd_chk_tail;
#endif
} mysql_sescmd_t;


/**
 * Property structure
 */
struct rses_property_st {
#if defined(SS_DEBUG)
        skygw_chk_t          rses_prop_chk_top;
#endif
        ROUTER_CLIENT_SES*   rses_prop_rsession; /*< parent router session */
        int                  rses_prop_refcount;
        rses_property_type_t rses_prop_type;
        union rses_prop_data {
                mysql_sescmd_t  sescmd;
		HASHTABLE*	temp_tables;
        } rses_prop_data;
        rses_property_t*     rses_prop_next; /*< next property of same type */
#if defined(SS_DEBUG)
        skygw_chk_t          rses_prop_chk_tail;
#endif
};

typedef struct sescmd_cursor_st {
#if defined(SS_DEBUG)
        skygw_chk_t        scmd_cur_chk_top;
#endif
        ROUTER_CLIENT_SES* scmd_cur_rses;         /*< pointer to owning router session */
	rses_property_t**  scmd_cur_ptr_property; /*< address of pointer to owner property */
	mysql_sescmd_t*    scmd_cur_cmd;          /*< pointer to current session command */
	bool               scmd_cur_active;       /*< true if command is being executed */
#if defined(SS_DEBUG)
	skygw_chk_t        scmd_cur_chk_tail;
#endif
} sescmd_cursor_t;

typedef struct shardrouter_config_st {
        int               rw_max_slave_conn_percent;
        int               rw_max_slave_conn_count;
	target_t          rw_use_sql_variables_in;	
} shard_config_t;

typedef enum{
  SUBSVC_ALLOC = 0,
  SUBSVC_OK = 1,
  SUBSVC_CLOSED = (1<<1), /* This is when the service was cleanly closed */
  SUBSVC_FAILED = (1<<2), /* This is when something went wrong */
  SUBSVC_QUERY_ACTIVE = (1<<3),
  SUBSVC_WAITING_RESULT = (1<<4),
  SUBSVC_MAPPED = (1<<5)
}subsvc_state_t;

typedef struct subservice_t{
    SERVICE* service;
    SESSION* session;
    DCB* dcb;
    GWBUF* pending_cmd;
    sescmd_cursor_t* scur;
    int state;
    int n_res_waiting;
    bool mapped;
}SUBSERVICE;

/**
 * Bitmask values for the router session's initialization. These values are used
 * to prevent responses from internal commands being forwarded to the client.
 */
typedef enum shard_init_mask
{
  INIT_READY = 0x0,
  INIT_MAPPING = 0x1,
  INIT_USE_DB = 0x02,
  INIT_UNINT = 0x04

} shard_init_mask_t;

/**
 * The client session structure used within this router.
 */
struct router_client_session {
#if defined(SS_DEBUG)
        skygw_chk_t      rses_chk_top;
#endif
        SPINLOCK         rses_lock;      /*< protects rses_deleted                 */
        int              rses_versno;    /*< even = no active update, else odd. not used 4/14 */
        bool             rses_closed;    /*< true when closeSession is called      */
	    DCB*			 rses_client_dcb;
            DCB* replydcb; /* DCB used to send the client write messages from the router itself */
            DCB* routedcb; /* DCB used to send queued queries to the router */
	    MYSQL_session*   rses_mysql_session;
	/** Properties listed by their type */
	rses_property_t* rses_properties[RSES_PROP_TYPE_COUNT];

        shard_config_t rses_config;    /*< copied config info from router instance */
        bool             rses_autocommit_enabled;
        bool             rses_transaction_active;
	struct router_instance	 *router;	/*< The router instance */
        struct router_client_session* next;
        HASHTABLE*      dbhash;
        SUBSERVICE*     *subservice;
        int             n_subservice;
        bool            hash_init;
        SESSION*        session;
        GWBUF* queue;
        char            connect_db[MYSQL_DATABASE_MAXLEN+1]; /*< Database the user was trying to connect to */
        char            current_db[MYSQL_DATABASE_MAXLEN + 1]; /*< Current active database */
        shard_init_mask_t    init; /*< Initialization state bitmask */
#if defined(SS_DEBUG)
        skygw_chk_t      rses_chk_tail;
#endif
};

/**
 * The statistics for this router instance
 */
typedef struct {
	int		n_sessions;	/*< Number sessions created        */
	int		n_queries;	/*< Number of queries forwarded    */
	int		n_master;	/*< Number of stmts sent to master */
	int		n_slave;	/*< Number of stmts sent to slave  */
	int		n_all;		/*< Number of stmts sent to all    */
} ROUTER_STATS;


/**
 * The per instance data for the router.
 */
typedef struct router_instance {
	SERVICE*                service;     /*< Pointer to owning service                 */
	ROUTER_CLIENT_SES*      connections; /*< List of client connections         */
    SERVICE**                services;      /*< List of services to map for sharding */
    int                     n_services;
    SUBSERVICE*             all_subsvc;
	SPINLOCK                lock;	     /*< Lock for the instance data         */
	shard_config_t        shardrouter_config; /*< expanded config info from SERVICE */
	int                     shardrouter_version;/*< version number for router's config */
        unsigned int	        bitmask;     /*< Bitmask to apply to server->status */
	unsigned int	        bitvalue;    /*< Required value of server->status   */
	ROUTER_STATS            stats;       /*< Statistics for this router         */
        struct router_instance* next;        /*< Next router on the list            */
	bool			available_slaves; /*< The router has some slaves available */
        DCB*                    dumy_backend;
} ROUTER_INSTANCE;

#define BACKEND_TYPE(b) (SERVER_IS_MASTER((b)->backend_server) ? BE_MASTER :    \
        (SERVER_IS_SLAVE((b)->backend_server) ? BE_SLAVE :  BE_UNDEFINED));

bool subsvc_is_valid(SUBSERVICE*);

#endif /*< _SHARDROUTER_H */
