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

#include "binlog_api.h"
#include "my_pthread.h"
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

using mysql::Binary_log;
using mysql::system::create_transport;
using namespace std;
using namespace mysql;
using namespace system;
using namespace table_replication_parser;

namespace mysql {

namespace table_replication_listener {

/* Table Consistency data structure */
typedef struct {
	char* database_dot_table;        /* Fully qualified db.table name,
					 primary key. */
	boost::uint32_t server_id;       /* Server id */
	char* gtid;                      /* Global transaction id */
	boost::uint64_t binlog_pos;      /* Binlog position */
	bool gtid_known;                 /* Is gtid known ? */
} table_listener_consistency_t;


/* STL multimap containing the consistency information. Multimap is used
because same table can be found from several servers. */
multimap<std::string, table_listener_consistency_t*> table_consistency_map;

boost::mutex table_consistency_mutex;    /* This mutex is used protect
					 abve data structure from
					 multiple threads */

/* We use this map to store constructed binary log connections */ 
map<int, Binary_log*> table_replication_listeners;

boost::mutex table_replication_mutex;    /* This mutex is used protect
					 abve data structure from
					 multiple threads */

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
	table_listener_consistency_t *tc=NULL;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_consistency_mutex);

	if(table_consistency_map.find(database_dot_table) == table_consistency_map.end()) {
		not_found = true;
	} else {
		// Loop through the consistency values
		for(multimap<std::string, table_listener_consistency_t*>::iterator i = table_consistency_map.find(database_dot_table);
		    i != table_consistency_map.end(); ++i) {
			tc = (*i).second;
			if (tc->server_id == lheader->server_id) {
				not_found = false;
				break;
			}
		}
	}

	if(not_found) {
		// Consistency for this table and server not found, insert a record
		table_listener_consistency_t* tb_c = (table_listener_consistency_t*) malloc(sizeof(table_listener_consistency_t));
		tb_c->database_dot_table = (char *)malloc(database_dot_table.size()+1);
		strcpy(tb_c->database_dot_table, database_dot_table.c_str());
		tb_c->server_id = lheader->server_id;
		tb_c->binlog_pos = lheader->next_position;
		tb_c->gtid_known =  gtid_known;
		tb_c->gtid = (char *)malloc(gtid.get_string().size()+1);
		strcpy(tb_c->gtid, gtid.get_string().c_str());

		table_consistency_map.insert(pair<std::string, table_listener_consistency_t*>(database_dot_table,tb_c));
	} else {
		// Consistency for this table and server found, update the
		// consistency values
		tc->binlog_pos = lheader->next_position;
		free(tc->gtid);
		tc->gtid = (char *)malloc(gtid.get_string().size()+1);
		strcpy(tc->gtid, gtid.get_string().c_str());
		tc->gtid_known = gtid_known;
	}
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
  Gtid gtid(0,1,31);
  bool gtid_known = false;

  try {
	  Binary_log binlog(create_transport(uri), uri);
	  binlog.connect(gtid);

	  {
		  // Need to be protected by mutex to avoid concurrency problems
		  boost::mutex::scoped_lock lock(table_replication_mutex);
		  table_replication_listeners[rlt->listener_id] = &binlog;
	  }

	  server_type = binlog.get_mysql_server_type_str();

	  cout << "Server " << uri << " type: " << server_type << endl;

	  Binary_log_event *event;

	  // While we have events
	  while (true) {
		  Log_event_header *lheader;

		  // Wait for the next event
		  int result = binlog.wait_for_next_event(&event);

		  if (result == ERR_EOF)
			  break;

		  lheader = event->header();

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

			  std::cout << "Thread: " << id << " server_id " << lheader->server_id
				    << " position " << lheader->next_position << " : Found event of type "
				    << event->get_event_type()
				    << " txt " << get_event_type_str(event->get_event_type())
				    << " query " << qevent->query << " db " << qevent->db_name
				    << " gtid " << gtid.get_string()
				    << std::endl;
			  break;
		  }

		  /*
                  Event is global transaction identifier. We need to store
		  value of this and handle actual state later.
		  */
		  case GTID_EVENT_MARIADB:
		  case GTID_EVENT_MYSQL:
		  {
			  Gtid_event *gevent = dynamic_cast<Gtid_event *>(event);

			  if (binlog.get_mysql_server_type() == MYSQL_SERVER_TYPE_MARIADB) {
				  gtid_known = true;
				  gtid = Gtid(gevent->domain_id, gevent->server_id, gevent->sequence_number);
			  } else {
				  // TODO MYSQL
			  }

			  std::cout << "Thread: " << id << " server_id " << lheader->server_id
				    << " position " << lheader->next_position << " : Found event of type "
				    << event->get_event_type()
				    << " txt " << get_event_type_str(event->get_event_type())
				    << " gtid " << gtid.get_string()
				    << std::endl;


			  break;

		  }

		  // With this event we know to which database and table the
		  // following events will be targeted.
		  case TABLE_MAP_EVENT:
		  {
			  Table_map_event *table_map_event= dynamic_cast<Table_map_event*>(event);
			  database_dot_table= table_map_event->db_name;
			  database_dot_table.append(".");
			  database_dot_table.append(table_map_event->table_name);
			  tid2tname[table_map_event->table_id]= database_dot_table;
			  break;
		  }

		  /* This is row based replication event containing INSERT,
		  UPDATE or DELETE clause broken to rows */
		  case WRITE_ROWS_EVENT:
		  case UPDATE_ROWS_EVENT:
		  case DELETE_ROWS_EVENT:
		  {
			  Row_event *revent = dynamic_cast<Row_event*>(event);
			  tb_it= tid2tname.begin();
			  tb_it= tid2tname.find(revent->table_id);
			  if (tb_it != tid2tname.end())
			  {
				  database_dot_table= tb_it->second;
			  }

			  // Update the consistency information
			  tbrl_update_consistency(lheader, database_dot_table, gtid_known, gtid);

			  std::cout << "Thread: " << id << " server_id " << lheader->server_id
				    << " position " << lheader->next_position << " : Found event of type "
				    << event->get_event_type()
				    << " txt " << get_event_type_str(event->get_event_type())
				    << " table " << revent->table_id
				    << " tb " << database_dot_table
				    << " gtid " << gtid.get_string()
				    << std::endl;
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
	  std::cerr << "Listener exception: " << e.what() << std::endl;
	  // Re-Throw this one.
	  throw;
  }
  catch(boost::system::error_code e)
  {
	  std::cerr << "Listener system error: " << e.message() << std::endl;
	  // Re-Throw this one.
	  throw;
  }
  // Try and catch all exceptions
  catch(std::exception const& e)
  {
	  std::cerr << "Listener other error: " << e.what() << std::endl;
	  // Re-Throw this one.
	  throw;
  }
  // Rest of them
  catch(...)
  {
	  std::cerr << "Unknown exception: " << std::endl;
	  // Re-Throw this one.
	  // It was not handled so you want to make sure it is handled correctly by
	  // the OS. So just allow the exception to keep propagating.
	  throw;
  }

  // Thread execution will end here
  pthread_exit(NULL);
  return NULL;

}

/***********************************************************************//**
This function is to shutdown the replication listener and free all
resources on table consistency. This function (TODO) will store
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

	b_it = table_replication_listeners.find(server_id);

	if ( b_it != table_replication_listeners.end()) {
		Binary_log *binlog = (*b_it).second;
		try {
			binlog->shutdown();
		}
		catch(ListenerException e)
		{
			std::cerr << "Listener exception: " << e.what() << std::endl;
			// Re-Throw this one.
			throw;
		}
		catch(boost::system::error_code e)
		{
			std::cerr << "Listener system error: " << e.message() << std::endl;
			// Re-Throw this one.
			throw;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			std::cerr << "Listener other error: " << e.what() << std::endl;
			// Re-Throw this one.
			throw;
		}
		// Rest of them
		catch(...)
		{
			std::cerr << "Unknown exception: " << std::endl;
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
        const char          *db_dot_table,   /*!< in: Fully qualified table
					     name. */
	table_consistency_t *tb_consistency, /*!< out: Consistency values. */
	boost::uint32_t     server_no)       /*!< in: Server */
{
	bool found = false;
	boost::uint32_t cur_server = 0;
	table_listener_consistency_t *tc=NULL;

	// Need to be protected by mutex to avoid concurrency problems
	boost::mutex::scoped_lock lock(table_consistency_mutex);

	// Loop through the consistency values
	for(multimap<std::string, table_listener_consistency_t*>::iterator i = table_consistency_map.find(db_dot_table);
	    i != table_consistency_map.end(); ++i, ++cur_server) {
		if (cur_server == server_no) {
			tc = (*i).second;
			memcpy(tb_consistency, tc, sizeof(table_listener_consistency_t));
			found = true;
			break;
		}
	}

	if (found) {
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
			std::cerr << "Listener exception: " << e.what() << std::endl;
			// Re-Throw this one.
			throw;
		}
		catch(boost::system::error_code e)
		{
			std::cerr << "Listener system error: " << e.message() << std::endl;
			// Re-Throw this one.
			throw;
		}
		// Try and catch all exceptions
		catch(std::exception const& e)
		{
			std::cerr << "Listener other error: " << e.what() << std::endl;
			// Re-Throw this one.
			throw;
		}
		// Rest of them
		catch(...)
		{
			std::cerr << "Unknown exception: " << std::endl;
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
	}

	return (1);
}

} // namespace table_replication_listener

} // namespace mysql
