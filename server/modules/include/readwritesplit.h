#ifndef _RWSPLITROUTER_H
#define _RWSPLITROUTER_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file router.h - The read write split router module heder file
 *
 * @verbatim
 * Revision History
 *
 * See GitHub https://github.com/skysql/MaxScale
 *
 * @endverbatim
 */

#include <dcb.h>

/**
 * Internal structure used to define the set of backend servers we are routing
 * connections to. This provides the storage for routing module specific data
 * that is required for each of the backend servers.
 */
typedef struct backend {
        SERVER* backend_server;	     /*< The server itself                   */
        int     backend_conn_count;  /*< Number of connections to the server */
} BACKEND;

typedef struct rses_property_st rses_property_t;
typedef struct router_client_session ROUTER_CLIENT_SES;

typedef enum rses_property_type_t {
        RSES_PROP_TYPE_UNDEFINED=0,
        RSES_PROP_TYPE_SESCMD,
        RSES_PROP_TYPE_FIRST = RSES_PROP_TYPE_SESCMD,
	RSES_PROP_TYPE_LAST=RSES_PROP_TYPE_SESCMD,
	RSES_PROP_TYPE_COUNT=RSES_PROP_TYPE_LAST+1
} rses_property_type_t;

typedef enum backend_type_t {
        BE_UNDEFINED=-1, 
        BE_MASTER, 
        BE_JOINED = BE_MASTER,
        BE_SLAVE, 
        BE_COUNT
} backend_type_t;

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
		void*           placeholder; /*< to be removed due new type */
        } rses_prop_data;
        rses_property_t*     rses_prop_next; /*< next property of same type */
#if defined(SS_DEBUG)
        skygw_chk_t          rses_prop_chk_tail;
#endif
};

typedef struct sescmd_cursor_st {
        ROUTER_CLIENT_SES* scmd_cur_rses;         /*< pointer to owning router session */
	rses_property_t**  scmd_cur_ptr_property; /*< address of pointer to owner property */
	mysql_sescmd_t*    scmd_cur_cmd;          /*< pointer to current session command */
	bool               scmd_cur_active;       /*< true if command is being executed */
	backend_type_t     scmd_cur_be_type;      /*< BE_MASTER or BE_SLAVE */
} sescmd_cursor_t;

/**
 * The client session structure used within this router.
 */
struct router_client_session {
#if defined(SS_DEBUG)
        skygw_chk_t      rses_chk_top;
#endif
        SPINLOCK         rses_lock;      /*< protects rses_deleted                 */
        int              rses_versno;    /*< even = no active update, else odd     */
        bool             rses_closed;    /*< true when closeSession is called      */
	/** Properties listed by their type */
	rses_property_t* rses_properties[RSES_PROP_TYPE_COUNT];
	BACKEND*         rses_backend[BE_COUNT];/*< Backends used by client session  */
	DCB*             rses_dcb[BE_COUNT];
	/*< cursor is pointer and status variable to current session command */
	sescmd_cursor_t  rses_cursor[BE_COUNT];
        int              rses_capabilities; /*< input type, for example */
        struct router_client_session* next;
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
	SERVICE*                service;     /*< Pointer to service                 */
	ROUTER_CLIENT_SES*      connections; /*< List of client connections         */
	SPINLOCK                lock;	     /*< Lock for the instance data         */
	BACKEND**               servers;     /*< Backend servers                    */
	BACKEND*                master;      /*< NULL or pointer                    */
        unsigned int	        bitmask;     /*< Bitmask to apply to server->status */
	unsigned int	        bitvalue;    /*< Required value of server->status   */
	ROUTER_STATS            stats;       /*< Statistics for this router         */
        struct router_instance* next;        /*< Next router on the list            */
} ROUTER_INSTANCE;

#endif /*< _RWSPLITROUTER_H */
