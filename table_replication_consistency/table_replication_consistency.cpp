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
#include <iostream>
#include "my_pthread.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <algorithm>
#include <sstream>
#include <boost/system/system_error.hpp>
#include "table_replication_consistency.h"
#include "table_replication_listener.h"
#include "listener_exception.h"


/* Global memory */

pthread_t *replication_listener_tid = NULL;
unsigned int n_replication_listeners = 0;

/* Namespaces */

using namespace std;
using namespace mysql;
using namespace mysql::replication_listener;

/***********************************************************************//**
This function will register replication listener for every server
provided and initialize all internal data structures and starts listening
the replication stream.
@return 0 on success, error code at failure. */
int 
tb_replication_consistency_init(
/*============================*/
	replication_listener_t *rpl,              /*!< in: Server
						  definition. */
	int                    n_servers,         /*!< in: Number of servers */
	unsigned int           gateway_server_id) /*!< in: Gateway slave
						  server id. */
{
	boost::uint32_t i;
	int err = 0;
	string errmsg="";

	replication_listener_tid = (pthread_t*)malloc(sizeof(pthread_t) * (n_servers + 1));

	if (replication_listener_tid == NULL) {
		errmsg = string("Table_Replication_Consistency: out of memory");
		goto error_handling;
	}

	for(i=0;i < n_servers; i++) {
		try {
			rpl[i].gateway_slave_server_id = gateway_server_id;
			rpl[i].listener_id = i;

			err = pthread_create(
				&replication_listener_tid[i],
				NULL,
				&(tb_replication_listener_reader),
				(void *) &(rpl[i]));

			if (err) {
				errmsg = string(strerror(err));
				goto error_handling;
			}
		}
		catch(ListenerException const& e)
		{
			errmsg = e.what();
			goto error_handling;
		}
		catch(boost::system::error_code const& e)
		{
			errmsg = e.message();
			goto error_handling;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			errmsg = e.what();
			goto error_handling;
		}
		// Rest of them
		catch(...)
		{
			errmsg = std::string("Unknown exception: ");
			goto error_handling;
		}
	}

	n_replication_listeners = i;
	/* We will try to join the threads at shutdown */
	return (0);

 error_handling:
	n_replication_listeners = i;
rpl[i].error_message = (char *)malloc(errmsg.size()+1);
	strcpy(rpl[i].error_message, errmsg.c_str());
	return (1);
}

/***********************************************************************//**
With this fuction client can request table consistency status for a
single table. As a return client will receive a number of consistency
status structures. Client must allocate memory for consistency result
array and provide the maximum number of values returned. At return
there is information how many results where available.
@return 0 on success, error code at failure. */
int 
tb_replication_consistency_query(
/*=============================*/
	table_consistency_query_t *tb_query, /*!< in: Table consistency
					     query. */ 
	table_consistency_t *tb_consistency, /*!< in: Table consistency
					     status structure.*/
	int *n_servers)                      /*!< inout: Number of
					     servers where to get table
					     consistency status. Out: Number
					     of successfull consistency
					     query results. */
{
	int err = 0;
	boost::uint32_t i = 0;
	std::string errmsg ="";

	// We need to protect C client from exceptions here
	try {
		for(i = 0; i < *n_servers; i++) {
			err = tb_replication_listener_consistency(tb_query->db_dot_table, &tb_consistency[i], i);

			if (err) {
				goto err_exit;
			}
		}
	}
	catch(mysql::ListenerException const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	catch(boost::system::error_code const& e)
	{
		errmsg = e.message();
		goto error_handling;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	// Rest of them
	catch(...)
	{
		errmsg = std::string("Unknown exception: ");
		goto error_handling;
	}

	*n_servers = i;
	return (err);

error_handling:
	tb_consistency[i].error_message = (char *)malloc(errmsg.size()+1);
	strcpy(tb_consistency[i].error_message, errmsg.c_str());

err_exit:
	*n_servers=i-1;
	tb_consistency[i].error_code = err;

	return (1);
}

/***********************************************************************//**
This function will reconnect replication listener to a server
provided.
@return 0 on success, error code at failure. */
int
tb_replication_consistency_reconnect(
/*=================================*/
	replication_listener_t* rpl,     /*!< in: Server definition.*/
	unsigned int gateway_server_id)  /*!< in: Gateway slave
					 server id. */
{
	std::string errmsg ="";
	int err = 0;

	rpl->gateway_slave_server_id = gateway_server_id;

	// We need to protect C client from exceptions here
	try {
		err = tb_replication_listener_reconnect(rpl, &replication_listener_tid[rpl->listener_id]);

		if (err) {
			goto err_exit;
		}
	}
	catch(ListenerException const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	catch(boost::system::error_code const& e)
	{
		errmsg = e.message();
		goto error_handling;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	// Rest of them
	catch(...)
	{
		errmsg = std::string("Unknown exception: ");
		goto error_handling;
	}

	return (err);

error_handling:
	rpl->error_message = (char *)malloc(errmsg.size()+1);
	strcpy(rpl->error_message, errmsg.c_str());

err_exit:
	return (1);
}

/***********************************************************************//**
This function is to shutdown the replication listener and free all
resources on table consistency. This function (TODO) will store
the current status on metadata to MySQL server.
@return 0 on success, error code at failure. */
int
tb_replication_consistency_shutdown(
	char ** error_message)          /*!< out: error message */
/*================================*/
{
	int err = 0;
	boost::uint32_t i = 0;
	std::string errmsg ="";

	// We need to protect C client from exceptions here
	try {
		for(i = 0; i < n_replication_listeners; i++) {
			err = tb_replication_listener_shutdown(i, error_message);

			if (err) {
				goto err_exit;
			}
		}

		// Need to wait until the thread exits
		err = pthread_join(replication_listener_tid[i], (void **)error_message);

		if (err) {
			goto err_exit;
		}
	}
	catch(mysql::ListenerException const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	catch(boost::system::error_code const& e)
	{
		errmsg = e.message();
		goto error_handling;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		errmsg = e.what();
		goto error_handling;
	}
	// Rest of them
	catch(...)
	{
		errmsg = std::string("Unknown exception: ");
		goto error_handling;
	}

	return (err);

error_handling:
	*error_message = (char *)malloc(errmsg.size()+1);
	strcpy(*error_message, errmsg.c_str());

err_exit:
	return (1);

}
