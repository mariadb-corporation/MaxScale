#ifndef _READCONNECTION_H
#define _READCONNECTION_H
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
 * @file readconnection.h - The read connection balancing query module heder file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 * 27/06/14	Mark Riddoch	Addition of server weight percentage
 *
 * @endverbatim
 */
#include <dcb.h>

/**
 * Internal structure used to define the set of backend servers we are routing
 * connections to. This provides the storage for routing module specific data
 * that is required for each of the backend servers.
 */
typedef struct backend
{
    SERVER *server; /*< The server itself */
    int current_connection_count; /*< Number of connections to the server */
    int weight; /*< Desired routing weight */
} BACKEND;

/**
 * The client session structure used within this router.
 */
typedef struct router_client_session
{
#if defined(SS_DEBUG)
    skygw_chk_t rses_chk_top;
#endif
    SPINLOCK rses_lock; /*< protects rses_deleted              */
    int rses_versno; /*< even = no active update, else odd  */
    bool rses_closed; /*< true when closeSession is called   */
    BACKEND *backend; /*< Backend used by the client session */
    DCB *backend_dcb; /*< DCB Connection to the backend      */
    struct router_client_session *next;
    int rses_capabilities; /*< input type, for example */
#if defined(SS_DEBUG)
    skygw_chk_t rses_chk_tail;
#endif
} ROUTER_CLIENT_SES;

/**
 * The statistics for this router instance
 */
typedef struct
{
    int n_sessions; /*< Number sessions created     */
    int n_queries; /*< Number of queries forwarded */
} ROUTER_STATS;

/**
 * The per instance data for the router.
 */
typedef struct router_instance
{
    SERVICE *service; /*< Pointer to the service using this router */
    ROUTER_CLIENT_SES *connections; /*< Link list of all the client connections  */
    SPINLOCK lock; /*< Spinlock for the instance data           */
    BACKEND **servers; /*< List of backend servers                  */
    unsigned int bitmask; /*< Bitmask to apply to server->status       */
    unsigned int bitvalue; /*< Required value of server->status         */
    ROUTER_STATS stats; /*< Statistics for this router               */
    struct router_instance
    *next;
} ROUTER_INSTANCE;
#endif
