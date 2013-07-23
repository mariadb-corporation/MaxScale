/*
Copyright (C) 2013, SkySQL Ab


This file is distributed as part of the SkySQL Gateway. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Author: Jan Lindström jan.lindstrom@skysql.com

Created: 20-06-2013
Updated:

*/

#ifndef TABLE_REPLICATION_LISTENER_H
#define TABLE_REPLICATION_LISTENER_H

#include <boost/cstdint.hpp>
#include <skygw_debug.h>

namespace mysql
{

namespace table_replication_listener
{

/***********************************************************************//**
This is the function that is executed by replication listeners.
At startup it will try to connect the server and start listening
the actual replication stream.
@return Pointer to error message. */
void *tb_replication_listener_reader(
/*=================================*/
        void    *arg);                   /*!< in: Replication listener
					 definition. */

/***********************************************************************//**
With this fuction client can request table consistency status for a
single table. As a return client will receive a number of consistency
status structures. Client must allocate memory for consistency result
array and provide the maximum number of values returned. At return
there is information how many results where available.
@return 0 on success, error code at failure. */
int
tb_replication_listener_consistency(
/*================================*/
        const unsigned char *db_dot_table,   /*!< in: Fully qualified table
					     name. */
	table_consistency_t *tb_consistency, /*!< out: Consistency values. */
	boost::uint32_t     server_no);      /*!< in: Server */

/***********************************************************************//**
This function will reconnect replication listener to a server
provided.
@return 0 on success, error code at failure. */
int
tb_replication_listener_reconnect(
/*==============================*/
        replication_listener_t* rpl,       /*!< in: Server definition.*/
	pthread_t*              tid);      /*!< in: Thread id */

/***********************************************************************//**
This function is to shutdown the replication listener and free all
resources on table consistency. This function (TODO) will store
the current status on metadata to MySQL server.
@return 0 on success, error code at failure. */
int
tb_replication_listener_shutdown(
/*=============================*/
        boost::uint32_t server_id,       /*!< in: server id */
        char           **error_message); /*!< out: error message */


} // table_replication_listener

} // mysql


#endif
