/*
Copyright (C) 2013, MariaDB Corporation Ab


This file is distributed as part of the MariaDB Corporation MaxScale. It is free
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

Author: Jan Lindström jan.lindstrom@mariadb.com

Created: 20-06-2013
Updated:

*/
#include <iostream>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <algorithm>
#include <sstream>
#include <boost/system/system_error.hpp>
#include "table_replication_consistency.h"
#include "table_replication_listener.h"
#include "table_replication_metadata.h"
#include "listener_exception.h"
#include "log_manager.h"

/* Global memory */

pthread_t *replication_listener_tid = NULL;
pthread_t *replication_listener_metadata_tid=NULL;
unsigned int n_replication_listeners = 0;

bool listener_shutdown=false;          /* This flag will be true
				       at shutdown */


#ifdef TBR_DEBUG
bool tbr_trace = true;
bool tbr_debug = true;
#else
#ifdef TBR_TRACE
bool tbr_trace = true;
#else
bool tbr_trace = false;
bool tbr_debug = false;
#endif
#endif

/* Namespaces */

using namespace std;
using namespace mysql;
using namespace table_replication_listener;
using namespace table_replication_metadata;

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
	size_t                 n_servers,         /*!< in: Number of servers */
	unsigned int           gateway_server_id, /*!< in: MaxScale slave
						  server id. */
	int                    trace_level)       /*!< in: Trace level */
{
	boost::uint32_t i;
	int err = 0;
	string errmsg="";

	// Allocate memory for thread identifiers
	replication_listener_tid = (pthread_t*)malloc(sizeof(pthread_t) * (n_servers + 1));

	if (replication_listener_tid == NULL) {
		errmsg = string("Fatal: Table_Replication_Consistency: out of memory");
		goto error_handling;
	}

	replication_listener_metadata_tid = (pthread_t*)malloc(sizeof(pthread_t));

	if (replication_listener_metadata_tid == NULL) {
		free(replication_listener_tid);
		errmsg = string("Fatal: Table_Replication_Consistency: out of memory");
		goto error_handling;
	}

	// Set up trace level
	if (trace_level & TBR_TRACE_DEBUG) {
		tbr_debug = true;
	}

	if (trace_level & TBR_TRACE_TRACE) {
		tbr_trace = true;
	}

	// Find out the master server
	for(i=0;i < n_servers; i++) {
		if (rpl[i].is_master) {
			break;
		}
	}

	// If master is found read metadata from MySQL server, if not report error
	if (i < n_servers) {
		char *errm = NULL;
		if(!tb_replication_listener_init(&(rpl[i]), &errm)) {
			errmsg = std::string(errm);
			free(errm);
		}
	} else {
		errmsg = string("Master server is missing from configuration");
		goto error_handling;
	}

	// Start replication stream reader thread for every server in the configuration
	for(i=0;i < n_servers; i++) {
		// We need to try catch all exceptions here because function
		// calling this service could be implemented using C-language
		// thus we need to protect it.
		try {
			rpl[i].gateway_slave_server_id = gateway_server_id;
			rpl[i].listener_id = i;

			// For master we start also metadata updater
			if (rpl[i].is_master) {
				err = pthread_create(
					replication_listener_metadata_tid,
					NULL,
					&(tb_replication_listener_metadata_updater),
					(void *)&(rpl[i]));

				if (err) {
					errmsg = string(strerror(err));
					goto error_handling;
				}
			}

			// Start actual replication listener
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
		// Replication listener will use this exception for
		// error handling.
		catch(ListenerException const& e)
		{
			errmsg = e.what();
			goto error_handling;
		}
		// Boost library exceptions
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

	// Number of threads
	n_replication_listeners = i;
	/* We will try to join the threads at shutdown */
	return (0);

 error_handling:
	n_replication_listeners = i;
	rpl[i].error_message = (char *)malloc(errmsg.size()+1);
	strcpy(rpl[i].error_message, errmsg.c_str());

	// This will log error to log file
	skygw_log_write_flush( LOGFILE_ERROR, (char *)errmsg.c_str());

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
	size_t *n_servers)                   /*!< inout: Number of
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
			err = tb_replication_listener_consistency((const unsigned char *)tb_query->db_dot_table, &tb_consistency[i], i);

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
	// This will log error to log file
	skygw_log_write_flush( LOGFILE_ERROR, (char *)errmsg.c_str());

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
	unsigned int gateway_server_id)  /*!< in: MaxScale slave
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
	// This will log error to log file
	skygw_log_write_flush( LOGFILE_ERROR, (char *)errmsg.c_str());

err_exit:
	return (1);
}

/***********************************************************************//**
This function is to shutdown the replication listener and free all
resources on table consistency. This function will store
the current status on metadata to MySQL server.
@return 0 on success, error code at failure. */
int
tb_replication_consistency_shutdown(
/*================================*/
	char ** error_message)          /*!< out: error message */
{
	int err = 0;
	boost::uint32_t i = 0;
	std::string errmsg ="";

	// We need to protect C client from exceptions here
	try {

		// Wait until all replication listeners are shut down
		for(i = 0; i < n_replication_listeners; i++) {

			err = tb_replication_listener_shutdown(i, error_message);

			if (err) {
				goto err_exit;
			}

			// Need to wait until the thread exits
			err = pthread_join(replication_listener_tid[i], (void **)error_message);

			if (err) {
				goto err_exit;
			}
		}

		listener_shutdown = true;

		// Wait until metadata writer has shut down
		err = pthread_join(*replication_listener_metadata_tid, NULL);

		if (err) {
			goto err_exit;
		}

		// Write metadata to MySQL storage and clean up
		err = tb_replication_listener_done(error_message);

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
	// This will log error to log file
	skygw_log_write_flush( LOGFILE_ERROR, (char *)errmsg.c_str());

err_exit:
	return (1);

}
