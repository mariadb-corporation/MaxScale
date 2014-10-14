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

#include "binlog_api.h"
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <algorithm>
#include "listener_exception.h"
#include "table_replication_consistency.h"
#include "table_replication_listener.h"
#include "table_replication_parser.h"
#include "table_replication_metadata.h"
#include "log_manager.h"
#include "skygw_debug.h"

using mysql::Binary_log;
using mysql::system::create_transport;
using namespace std;
using namespace mysql;
using namespace system;
using namespace table_replication_parser;
using namespace table_replication_metadata;

namespace mysql {

namespace table_replication_listener {


/* STL multimap containing the consistency information. Multimap is used
because same table can be found from several servers. */
multimap<std::string, tbr_metadata_t*> table_consistency_map;

boost::mutex table_consistency_mutex;    /* This mutex is used to protect
					 above data structure from
					 multiple threads */

/* We use this map to store constructed binary log connections */
map<int, Binary_log*> table_replication_listeners;

boost::mutex table_replication_mutex;    /* This mutex is used to protect
					 above data structure from
					 multiple threads */

/* We use this map to store table consistency server metadata */
map<boost::uint32_t, tbr_server_t*> table_replication_servers;

boost::mutex table_servers_mutex;        /* This mutex is used to proted
					 above data structure from multiple
					 threads */

replication_listener_t *master;          /* Master server definition */

/* Master connect info */
char *master_user=NULL;
char *master_passwd=NULL;
char *master_host=NULL;
unsigned long master_port=3307;

/***********************************************************************//**
Internal function to extract user, passwd, hostname and port from
replication listener url. */
static void
tbrl_extract_master_connect_info()
/*==============================*/
{
	char *body = master->server_url;
	size_t len = strlen(master->server_url);

	/* Find the user name, which is mandatory */
	const char *user = body + 8;

	const char *user_end= strpbrk(user, ":@");

	assert(user_end - user >= 1);          // There has to be a username

	/* Find the password, which can be empty */
	assert(*user_end == ':' || *user_end == '@');
	const char *const pass = user_end + 1;        // Skip the ':' (or '@')
	const char *pass_end = pass;
	if (*user_end == ':')
	{
		pass_end = strchr(pass, '@');
	}
	assert(pass_end - pass >= 0);               // Password can be empty

	/* Find the host name, which is mandatory */
	// Skip the '@', if there is one
	const char *host = *pass_end == '@' ? pass_end + 1 : pass_end;
	const char *host_end = strchr(host, ':');
	if (host == host_end) {
		/* If no ':' was found there is no port, so the host end at the end
		* of the string */
		if (host_end == 0)
			host_end = body + len;
	}
	assert(host_end - host >= 1);              // There has to be a host

	/* Find the port number */
	unsigned long portno = 3307;
	if (*host_end == ':')
		portno = strtoul(host_end + 1, NULL, 10);

	std::string u(user, user_end - user);
	std::string p(pass, pass_end - pass);
	std::string h(host, host_end - host);

	master_user = (char *)malloc(u.length()+1);
	master_passwd = (char *)malloc(p.length()+1);
	master_host = (char *)malloc(h.length()+1);
	strcpy(master_user, u.c_str());
	strcpy(master_passwd, p.c_str());
	strcpy(master_host, h.c_str());
	master_port = portno;
}

/***********************************************************************//**
Internal function to update table consistency information based
on log event header, table name and if GTID is known the gtid.*/
static void
tbrl_update_consistency(
/*====================*/
	Log_event_header *lheader,  /*!< in: Log event header */
	string database_dot_table,  /*!< in: db.table name */
	bool gtid_known,            /*!< in: is GTID known */
	Gtid& gtid)                 /*!< in: gtid */
{
	bool not_found = true;
	tbr_metadata_t *tc=NULL;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_consistency_mutex);

	multimap<std::string, tbr_metadata_t*>::iterator key = table_consistency_map.find(database_dot_table);

	if( key == table_consistency_map.end()) {
		not_found = true;
	} else {
		// Loop through the consistency values
		for(multimap<std::string, tbr_metadata_t*>::iterator i = key;
		    i != table_consistency_map.end(); ++i) {
			tc = (*i).second;
			if (tc->server_id == lheader->server_id) {
				not_found = false;
				break;
			}

			// If the next table name is not anymore the same,
			// we can safely exit from the loop, names are ordered
			if (strcpy((char *)tc->db_table, (char *)database_dot_table.c_str()) != 0) {
				not_found = true;
				break;
			}
		}
	}

	if(not_found) {
		// Consistency for this table and server not found, insert a record
		tc = (tbr_metadata_t*) malloc(sizeof(tbr_metadata_t));
		tc->db_table = (unsigned char *)malloc(database_dot_table.size()+1);
		strcpy((char *)tc->db_table, (char *)database_dot_table.c_str());
		tc->server_id = lheader->server_id;
		tc->binlog_pos = lheader->next_position;
		tc->gtid_known =  gtid_known;
		tc->gtid_len = gtid.get_gtid_length();
		tc->gtid = (unsigned char *)malloc(tc->gtid_len);
		memcpy(tc->gtid, gtid.get_gtid(), tc->gtid_len);

		table_consistency_map.insert(pair<std::string, tbr_metadata_t*>(database_dot_table, tc));
	} else {
		// Consistency for this table and server found, update the
		// consistency values
		tc->binlog_pos = lheader->next_position;
		free(tc->gtid);
		tc->gtid_len = gtid.get_gtid_length();
		tc->gtid = (unsigned char *)malloc(tc->gtid_len);
		memcpy(tc->gtid, gtid.get_gtid(), tc->gtid_len);
		tc->gtid_known = gtid_known;
	}

	if (tbr_trace) {
		// This will log error to log file
		skygw_log_write_flush( LOGFILE_TRACE,
			(char *)"TRC Trace: Current state for table %s in server %d binlog_pos %lu GTID '%s'",
			tc->db_table, tc->server_id, tc->binlog_pos, gtid.get_string().c_str());
	}

}

/***********************************************************************//**
Internal function to update table replication consistency server status
based on log event header and gtid if known*/
static void
tbrl_update_server_status(
/*======================*/
	Log_event_header *lheader,  /*!< in: Log event header */
	bool gtid_known,            /*!< in: is GTID known */
	Gtid& gtid)                 /*!< in: gtid */
{
	bool not_found = true;
	tbr_server_t *ts=NULL;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_servers_mutex);

	// Try to find out the servre metadata
	map<uint32_t, tbr_server_t*>::iterator key = table_replication_servers.find(lheader->server_id);

	if( key == table_replication_servers.end()) {
		not_found = true;
	} else {
		ts = (*key).second;
		not_found = false;
	}

	if(not_found) {
		// Consistency for this server not found, insert a record
		ts = (tbr_server_t*) malloc(sizeof(tbr_server_t));
		ts->server_id = lheader->server_id;
		ts->binlog_pos = lheader->next_position;
		ts->gtid_known =  gtid_known;
		ts->gtid_len = gtid.get_gtid_length();
		ts->gtid = (unsigned char *)malloc(ts->gtid_len);
		memcpy(ts->gtid, gtid.get_gtid(), ts->gtid_len);

		table_replication_servers.insert(pair<boost::uint32_t, tbr_server_t*>(lheader->server_id, ts));
	} else {
		// Consistency for this server found, update the consistency values
		ts->binlog_pos = lheader->next_position;
		free(ts->gtid);
		ts->gtid_len = gtid.get_gtid_length();
		ts->gtid = (unsigned char *)malloc(ts->gtid_len);
		memcpy(ts->gtid, gtid.get_gtid(), ts->gtid_len);
		ts->gtid_known = gtid_known;
	}

	if (tbr_trace) {
		// This will log error to log file
		skygw_log_write_flush( LOGFILE_TRACE,
			(char *)"TRC Trace: Current state for server %d binlog_pos %lu GTID '%s'",
			ts->server_id, ts->binlog_pos, gtid.get_string().c_str());
	}
}

/***********************************************************************//**
Internal function to iterate through server metadata to find out if
we should continue from existing binlog position or gtid position.*/
static bool
tbrl_get_startup_pos(
/*=================*/
	boost::uint32_t server_id,
	boost::uint64_t *binlog_pos,
	Gtid *gtid,
	bool *gtid_known,
	bool *use_binlog_pos)
{
	*use_binlog_pos = true;
	*gtid_known = false;
	*binlog_pos = 0;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_servers_mutex);

	map<boost::uint32_t, tbr_server_t*>::iterator key = table_replication_servers.find(server_id);

	if (key != table_replication_servers.end()) {
		// Found
		tbr_server_t *mserver = (*key).second;

		// For MariaDB we know how to start from GTID position if
		// that is specified, in MYSQL we use always binlog pos

		if (mserver->server_type == TRC_SERVER_TYPE_MARIADB) {
			if (mserver->gtid_known) {
				boost::uint32_t domain;
				boost::uint32_t server;
				boost::uint64_t sno;
				sscanf((const char *)mserver->gtid, "%u-%u-%lu", &domain, &server, &sno);
				*gtid_known = true;
				*gtid = Gtid(domain, server, sno);
			} else {
				*binlog_pos = mserver->binlog_pos;
				*use_binlog_pos = true;
			}
		} else {
			*binlog_pos = mserver->binlog_pos;
			*use_binlog_pos = true;
		}

		return true;
	}

	return false;
}

/***********************************************************************//**
This is the function that is executed by replication listeners.
At startup it will try to connect the server and start listening
the actual replication stream. Stream is listened and events
are handled until a shutdown message is send from the user.
@return Pointer to error message. */
void* tb_replication_listener_reader(
/*=================================*/
	void * arg)                   /*!< in: Replication listener
					 definition. */

{
	replication_listener_t *rlt = (replication_listener_t*)arg;
	char *uri = rlt->server_url;
	map<int, string> tid2tname;
	map<int, string>::iterator tb_it;
	pthread_t id = pthread_self();
	string database_dot_table;
	const char* server_type;
	Gtid gtid;
	bool gtid_known = false;
	boost::uint64_t binlog_pos = 0;
	bool use_binlog_pos = true;

	try {
		Binary_log binlog(create_transport(uri), uri);

		// If the external user has provided the position where to
		// continue we will use that. If no position is given,
		// we try to use position from metadata tables. If all this
		// is not available, we start from the begining of the binlog.
		if (rlt->use_binlog_pos) {
			binlog_pos = rlt->binlog_pos;
		} else if (rlt->use_mariadb_gtid) {
			boost::uint32_t domain;
			boost::uint32_t server;
			boost::uint64_t sno;
			sscanf((const char *)rlt->gtid, "%u-%u-%lu", &domain, &server, &sno);
			gtid = Gtid(domain, server, sno);
			use_binlog_pos = false;
		} else if (rlt->use_mysql_gtid) {
			gtid = Gtid(rlt->gtid);
			use_binlog_pos = false;
		} else {
			// At startup we need to iterate through servers and see if
			// we need to continue from last position
			if(!tbrl_get_startup_pos(rlt->listener_id, &binlog_pos, &gtid, &gtid_known, &use_binlog_pos)) {
				binlog_pos = 0;
				use_binlog_pos = true;
			}
		}

		// Connect to server
		if (use_binlog_pos) {
			binlog.connect(binlog_pos);
		} else {
			binlog.connect(gtid);
		}

		{
			// Need to be protected by mutex to avoid concurrency problems
			boost::mutex::scoped_lock lock(table_replication_mutex);
			table_replication_listeners[rlt->listener_id] = &binlog;
		}

		// Set up the master
		if (rlt->is_master) {
			master = rlt;
		}

		server_type = binlog.get_mysql_server_type_str();

		if (tbr_trace) {
			string trace_msg = "Server " + string(uri) + string(server_type);
			skygw_log_write_flush( LOGFILE_TRACE, (char *)trace_msg.c_str());
		}

		Binary_log_event *event;

		// While we have events
		while (true) {
			Log_event_header *lheader;

			// Wait for the next event
			int result = binlog.wait_for_next_event(&event);

			if (result == ERR_EOF)
				break;

			lheader = event->header();

			// Insert or update current server status
			tbrl_update_server_status(lheader, gtid_known, gtid);

			switch(event->get_event_type()) {

			case QUERY_EVENT: {
				Query_event *qevent = dynamic_cast<Query_event *>(event);
				int n_tables = 0;

				// This is overkill but we really do not know how
				// many names there are at this state
				char **db_names = (char **) malloc(qevent->query.size()+1 * sizeof(char *));
				char **table_names = (char **) malloc(qevent->query.size()+1 * sizeof(char *));

				// Try to parse db.table names from the SQL-clause
				if (tbr_parser_table_names(db_names, table_names, &n_tables, qevent->query.c_str())) {
					// Success, set up the consistency
					// information for every table
					for(int k=0;k < n_tables; k++) {
						// Update the consistency
						// information

						if(db_names[k][0]=='\0') {
							database_dot_table = qevent->db_name;
						} else {
							database_dot_table = string(db_names[k]);
						}
						database_dot_table.append(".");
						database_dot_table.append(string(table_names[k]));

						tbrl_update_consistency(lheader, database_dot_table, gtid_known, gtid);

						free(db_names[k]);
						free(table_names[k]);
					}
					free(db_names);
					free(table_names);
				} else {
					for(int k=0; k < n_tables; k++) {
						free(db_names[k]);
						free(table_names[k]);
					}
					free(db_names);
					free(table_names);
				}

				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Thread %ld Server %d Binlog_pos %lu event %d"
						" : %s Query %s DB %s gtid '%s'",
						id,
						lheader->server_id,
						lheader->next_position,
						event->get_event_type(),
						get_event_type_str(event->get_event_type()),
						qevent->query.c_str(),
						qevent->db_name.c_str(),
						gtid.get_string().c_str());
				}
				break;
			}

			/*
			Event is global transaction identifier. We need to store
			value of this and handle actual state later.
			*/
			case GTID_EVENT_MARIADB:
			case GTID_EVENT_MYSQL: {
				Gtid_event *gevent = dynamic_cast<Gtid_event *>(event);

				if (binlog.get_mysql_server_type() == MYSQL_SERVER_TYPE_MARIADB) {
					gtid_known = true;
					gtid = Gtid(gevent->domain_id, gevent->server_id, gevent->sequence_number);
				} else {
					gtid_known = true;
					gtid = Gtid(gevent->m_mysql_gtid);
				}

				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Thread %ld Server %d Binlog_pos %lu event %d"
						" : %s gtid '%s'",
						id,
						lheader->server_id,
						lheader->next_position,
						event->get_event_type(),
						get_event_type_str(event->get_event_type()),
						gtid.get_string().c_str());
				}

				break;
			}

			// With this event we know to which database and table the
			// following events will be targeted.
			case TABLE_MAP_EVENT: {
				Table_map_event *table_map_event= dynamic_cast<Table_map_event*>(event);
				database_dot_table= table_map_event->db_name;
				database_dot_table.append(".");
				database_dot_table.append(table_map_event->table_name);
				tid2tname[table_map_event->table_id]= database_dot_table;

				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Thread %ld Server %d Binlog_pos %lu event %d"
						" : %s dbtable '%s' id %d",
						id,
						lheader->server_id,
						lheader->next_position,
						event->get_event_type(),
						get_event_type_str(event->get_event_type()),
						database_dot_table.c_str(),
						table_map_event->table_id);
				}

				break;
			}

		        /* This is row based replication event containing INSERT,
			UPDATE or DELETE clause broken to rows */
			case WRITE_ROWS_EVENT:
			case UPDATE_ROWS_EVENT:
			case DELETE_ROWS_EVENT: {
				Row_event *revent = dynamic_cast<Row_event*>(event);
				tb_it= tid2tname.begin();
				tb_it= tid2tname.find(revent->table_id);

				// DB.table name found
				if (tb_it != tid2tname.end())
				{
					database_dot_table= tb_it->second;
				}

				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Thread %ld Server %d Binlog_pos %lu event %d"
						" : %s dbtable '%s' id %d",
						id,
						lheader->server_id,
						lheader->next_position,
						revent->get_event_type(),
						get_event_type_str(revent->get_event_type()),
						database_dot_table.c_str(),
						revent->table_id);
				}


				// Update the consistency information
				tbrl_update_consistency(lheader, database_dot_table, gtid_known, gtid);

				break;

			}
			// Default event handler, do nothing
			default:
				break;
	          } // switch
		} // while
	} // try
	catch(ListenerException e)
	{
		string err = std::string("Listener exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		// Re-Throw this one.
		throw;
	}
	catch(boost::system::error_code e)
	{
		string err = std::string("Listener system exception: ")+ e.message();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		// Re-Throw this one.
		throw;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		string err = std::string("Listener other exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		// Re-Throw this one.
		throw;
	}
	// Rest of them
	catch(...)
	{
		string err = std::string("Unknown exception: ");
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		// Re-Throw this one.
		// It was not handled so you want to make sure it is handled correctly by
		// the OS. So just allow the exception to keep propagating.
		throw;
	}

	if (tbr_trace) {
		string trace_msg = string("Listener for server ") + string(uri) + string(server_type) + string(" shutting down");
		skygw_log_write_flush( LOGFILE_TRACE, (char *)trace_msg.c_str());
	}

	// Thread execution will end here
	pthread_exit(NULL);
	return NULL;
}

/***********************************************************************//**
This function is to shutdown the replication listener and free all
resources on table consistency. This function will store
the current status on metadata to MySQL server.
@return 0 on success, error code at failure. */
int
tb_replication_listener_shutdown(
/*=============================*/
        boost::uint32_t server_id,       /*!< in: server id */
	char            **error_message) /*!< out: error message */
{
	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_replication_mutex);
	map<int, Binary_log*>::iterator b_it;

	listener_shutdown = true;

	b_it = table_replication_listeners.find(server_id);

	if ( b_it != table_replication_listeners.end()) {
		Binary_log *binlog = (*b_it).second;

		if (tbr_debug) {
			skygw_log_write_flush( LOGFILE_TRACE,
				(char *)"TRC Debug: Shutting down replication listener for server %s",
				binlog->get_url().c_str());
		}

		try {
			binlog->shutdown();
		}
		catch(ListenerException e)
		{
			string err = std::string("Listener exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		catch(boost::system::error_code e)
		{
			string err = std::string("Listener system exception: ")+ e.message();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			string err = std::string("Listener other exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		// Rest of them
		catch(...)
		{
			string err = std::string("Unknown exception: ");
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			// It was not handled so you want to make sure it is handled correctly by
			// the OS. So just allow the exception to keep propagating.
			throw;
		}

		return (0);
	} else {
		std::string err = std::string("Replication listener for server_id = ") + to_string(server_id) + std::string(" not active ");
		*error_message = (char *)malloc(err.size()+1);
		strcpy(*error_message, err.c_str());

		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());

		return (1);
	}
}

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
	boost::uint32_t     server_no)       /*!< in: Server */
{
	bool found = false;
	boost::uint32_t cur_server = 0;
	tbr_metadata_t *tc=NULL;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_consistency_mutex);

	// Loop through the consistency values
	for(multimap<std::string, tbr_metadata_t*>::iterator i = table_consistency_map.find((char *)db_dot_table);
	    i != table_consistency_map.end(); ++i, ++cur_server) {
		if (cur_server == server_no) {
			tc = (*i).second;
			memcpy(tb_consistency, tc, sizeof(tbr_metadata_t));
			found = true;
			break;
		}
	}

	if (found) {
		if (tbr_trace) {
			// This will log error to log file
			skygw_log_write_flush( LOGFILE_TRACE,
				(char *)"TRC Trace: Current state for table %s in server %d binlog_pos %lu GTID '%s'",
				tc->db_table, tc->server_id, tc->binlog_pos, tc->gtid);
		}
		return (1);
	} else {
		return (0);
	}

}
/***********************************************************************//**
This function will reconnect replication listener to a server
provided.
@return 0 on success, error code at failure. */
int
tb_replication_listener_reconnect(
/*==============================*/
        replication_listener_t* rpl,  /*!< in: Server definition.*/
	pthread_t*              tid)  /*!< in: Thread id */
{
	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_replication_mutex);
	map<int, Binary_log*>::iterator b_it;
	bool found = false;
	int err = 0;
	Binary_log *binlog = NULL;
        char *error_message = NULL;
	std::string errmsg = "";

	for(b_it = table_replication_listeners.begin(); b_it != table_replication_listeners.end(); ++b_it) {
		binlog = (*b_it).second;
		std::string url = binlog->get_url();

		// Found correct listener?
		if (url.compare(std::string(rpl->server_url)) == 0) {
			found = true;
			break;
		}
	}

	if (found) {

		if (tbr_debug) {
			skygw_log_write_flush( LOGFILE_TRACE,
				(char *)"TRC Debug: Reconnecting to server %s",
				binlog->get_url().c_str());
		}

		try {
			// Shutdown the current listener thread
			binlog->shutdown();

			// Wait until thread has exited
			err = pthread_join(*tid, (void **)&error_message);

			if (err) {
				if (error_message = NULL) {
					error_message = strerror(err);
				}

				goto err_exit;
			}

			// Start a new replication listener
			err = pthread_create(
				tid,
				NULL,
				&(tb_replication_listener_reader),
				(void *)rpl);

			if (err) {
				error_message = strerror(err);
				goto err_exit;
			}

		}
		catch(ListenerException e)
		{
			string err = std::string("Listener exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		catch(boost::system::error_code e)
		{
			string err = std::string("Listener system exception: ")+ e.message();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			string err = std::string("Listener other exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			throw;
		}
		// Rest of them
		catch(...)
		{
			string err = std::string("Unknown exception: ");
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			// Re-Throw this one.
			// It was not handled so you want to make sure it is handled correctly by
			// the OS. So just allow the exception to keep propagating.
			throw;
		}


	} else {
		errmsg = std::string("Replication listener not found");
		error_message = (char *)errmsg.c_str();
	}

	return (0);

err_exit:
	if (error_message) {
		rpl->error_message = (char *)malloc(strlen(error_message +1));
		strcpy(rpl->error_message, error_message);
		skygw_log_write_flush( LOGFILE_ERROR, error_message);
	}

	return (1);
}

/***********************************************************************//**
This internal function is executed on its own thread and it will write
table consistency information to the master database in every n seconds
based on configuration.
*/
void
*tb_replication_listener_metadata_updater(
/*======================================*/
	void *arg)   /*!< in: Master definition */
{
	master = (replication_listener_t*)arg;
	tbr_metadata_t **tm=NULL;
	tbr_server_t **ts=NULL;
	bool err = false;

	// Set up master connect info
	tbrl_extract_master_connect_info();

	while(listener_shutdown == false) {
		sleep(10); // Sleep ~10 seconds

		try {
			size_t nelems;

			// This scope for scoped mutexing
			{
				// Need to be protected by mutex to avoid concurrency problems
				boost::mutex::scoped_lock lock(table_consistency_mutex);

				nelems = table_consistency_map.size();
				size_t k =0;

				tm = (tbr_metadata_t**)calloc(nelems, sizeof(tbr_metadata_t*));

				if (!tm) {
					skygw_log_write_flush( LOGFILE_ERROR, (char *)"Error: TRM: Out of memory");
					goto my_exit;

				}

				for(multimap<std::string, tbr_metadata_t*>::iterator i = table_consistency_map.begin();
				    i != table_consistency_map.end(); ++i,++k) {

					tm[k] = ((*i).second);
				}
			}


			// Insert or update metadata information
			if (!tbrm_write_consistency_metadata(
				(const char *)master_host,
				(const char *)master_user,
				(const char *)master_passwd,
				master_port,
				tm,
				nelems)) {
				goto my_exit;
			}

			free(tm);
			tm = NULL;

			// This scope for scoped mutexing
			{
				// Need to be protected by mutex to avoid
				// concurrency problems
				boost::mutex::scoped_lock lock(table_servers_mutex);

				nelems = table_replication_servers.size();
				size_t k =0;

				ts = (tbr_server_t**)calloc(nelems, sizeof(tbr_server_t*));

				if (!ts) {
					skygw_log_write_flush( LOGFILE_ERROR, (char *)"Error: TRM: Out of memory");
					goto my_exit;
				}

				for(map<boost::uint32_t, tbr_server_t*>::iterator i = table_replication_servers.begin();
				    i != table_replication_servers.end(); ++i,++k) {

					ts[k] = ((*i).second);

				}
			}

			// Insert or update metadata information
			if (!tbrm_write_server_metadata(
				(const char *)master_host,
				(const char *)master_user,
				(const char *)master_passwd,
				master_port,
				ts,
				nelems)) {
					goto my_exit;
			}

			free(ts);
			ts = NULL;
		}
		catch(ListenerException e)
		{
			string err = std::string("Listener exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			goto my_exit;
		}
		catch(boost::system::error_code e)
		{
			string err = std::string("Listener system exception: ")+ e.message();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			goto my_exit;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			string err = std::string("Listener other exception: ")+ e.what();
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			goto my_exit;
		}
		// Rest of them
		catch(...)
		{
			string err = std::string("Unknown exception: ");
			skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
			goto my_exit;
		}
	}

my_exit:

	if (tm) {
		free(tm);
	}

	if (ts) {
		free(ts);
	}

	if (tbr_trace) {
		skygw_log_write_flush( LOGFILE_TRACE, (char *)"Shutting down the metadata updater thread");
	}

	pthread_exit(NULL);
	return NULL;
}

/***********************************************************************//**
Read current state of the metadata from the MySQL server or create
necessary metadata and initialize listener metadata.
@return true on success, false on failure
*/
bool
tb_replication_listener_init(
/*=========================*/
	replication_listener_t* rpl, /*! in: Master server definition */
	char **error_message)        /*!< out: error message */
{
	tbr_metadata_t *tm = NULL;
	tbr_server_t *ts=NULL;
	size_t tm_rows = 0;
	std::string dbtable;
	std::string err;

	master = rpl;
	// Set up master connect info
	tbrl_extract_master_connect_info();


	try {
		if (!tbrm_read_consistency_metadata((const char *)master_host,
				(const char *)master_user,
				(const char *)master_passwd,
				(unsigned int)master_port,
				&tm,
				&tm_rows)) {
			err = std::string("Error: reading table consistency metadata failed");
			goto error_exit;
		}

		for(size_t i=0;i < tm_rows; i++) {
			tbr_metadata_t *t = &(tm[i]);
			dbtable = std::string((char *)t->db_table);

			table_consistency_map.insert(pair<std::string, tbr_metadata_t*>(dbtable, t));
		}

		if (!tbrm_read_server_metadata(
				(const char *)master_host,
				(const char *)master_user,
				(const char *)master_passwd,
				(unsigned int)master_port,
				&ts,
				&tm_rows)) {
			err = std::string("Error: reading table servers metadata failed");
			goto error_exit;
		}

		for(size_t i=0;i < tm_rows; i++) {
			tbr_server_t *t = &(ts[i]);

			table_replication_servers.insert(pair<uint32_t, tbr_server_t*>(t->server_id, t));
		}

	}
	catch(ListenerException e)
	{
		err = std::string("Listener exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	catch(boost::system::error_code e)
	{
		err = std::string("Listener system exception: ")+ e.message();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		err = std::string("Listener other exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	// Rest of them
	catch(...)
	{
		err = std::string("Unknown exception: ");
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}

	return true;

error_exit:
	*error_message = (char *)malloc(err.length()+1);
	strcpy(*error_message, err.c_str());
	return false;
}

/***********************************************************************//**
Write current state of the metadata to the MySQL server and
clean up the data structures.
@return 0 on success, error code at failure. */
int
tb_replication_listener_done(
/*==========================*/
	char **error_message)  /*!< out: error message */
{
	size_t nelems = table_consistency_map.size();
	size_t nelems2 = table_replication_servers.size();
	size_t k =0;
	tbr_metadata_t **tm=NULL;
	tbr_server_t **ts=NULL;
	bool err = false;

	tm = (tbr_metadata_t**)calloc(nelems, sizeof(tbr_metadata_t*));
	ts = (tbr_server_t **)calloc(nelems2, sizeof(tbr_server_t*));

	if (tm == NULL || ts == NULL) {
		skygw_log_write_flush( LOGFILE_ERROR, (char *)"TRM: Out of memory");
		goto error_exit;
	}

	try {
		k = 0;
		for(multimap<std::string, tbr_metadata_t*>::iterator i = table_consistency_map.begin();
		    i != table_consistency_map.end(); ++i,++k) {
			tm[k] = ((*i).second);
		}

		// Insert or update table consistency metadata information
		if (!tbrm_write_consistency_metadata(
			(const char *)master_host,
			(const char *)master_user,
			(const char *)master_passwd,
			(unsigned int)master_port,
			tm,
			nelems)) {
			goto error_exit;
		}

		// Clean up memory allocation for multimap items
		for(multimap<std::string, tbr_metadata_t*>::iterator i = table_consistency_map.begin();
		    i != table_consistency_map.end(); ++i) {
			tbr_metadata_t *trm = ((*i).second);

			free(trm->db_table);
			free(trm->gtid);

			table_consistency_map.erase(i);
			free(trm);
		}

		k=0;
		for(map<uint32_t, tbr_server_t*>::iterator i = table_replication_servers.begin();
		    i != table_replication_servers.end(); ++i,++k) {
			ts[k] = ((*i).second);
		}

		// Insert or update table server metadata information
		if (!tbrm_write_server_metadata(
			(const char *)master_host,
			(const char *)master_user,
			(const char *)master_passwd,
			(unsigned int)master_port,
			ts,
			nelems2)) {
			goto error_exit;
		}

		// Clean up memory allocation for multimap items
		for(map<uint32_t, tbr_server_t*>::iterator j = table_replication_servers.begin();
		    j != table_replication_servers.end(); ++j) {
			tbr_server_t *trs = ((*j).second);

			free(trs->gtid);

			table_replication_servers.erase(j);
			free(trs);
		}

		// Clean up binlog listeners
		table_replication_listeners.erase(table_replication_listeners.begin(), table_replication_listeners.end());
	}
	catch(ListenerException e)
	{
		string err = std::string("Listener exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	catch(boost::system::error_code e)
	{
		string err = std::string("Listener system exception: ")+ e.message();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		string err = std::string("Listener other exception: ")+ e.what();
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}
	// Rest of them
	catch(...)
	{
		string err = std::string("Unknown exception: ");
		skygw_log_write_flush( LOGFILE_ERROR, (char *)err.c_str());
		goto error_exit;
	}

	if (tbr_trace) {
		skygw_log_write_flush( LOGFILE_TRACE, (char *)"Shutting down the listeners");
		goto error_exit;
	}

	free(tm);
	free(ts);

	return err;

error_exit:
	if (tm) {
		free(tm);
	}
	if (ts) {
		free(ts);
	}

	return err;
}

} // namespace table_replication_listener

} // namespace mysql
