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

*/

#include "binlog_api.h"
#include "listener_exception.h"
#include "table_replication_consistency.h"
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
#include <my_global.h>
#include <mysql.h>
#include "../gtid.h"

using mysql::Binary_log;
using mysql::system::create_transport;
using namespace std;
using namespace mysql;
using namespace mysql::system;

static char* server_options[] = {
	(char *)"event_dump",
	(char *)"--datadir=/tmp/",
	(char *)"--skip-innodb",
	(char *)"--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] = {
	(char *)"libmysqld_server",
	(char *)"libmysqld_client",
	(char *)"libmysqld_server",
	(char *)"libmysqld_server",
	NULL
};

void* binlog_reader(void * arg)
{
	replication_listener_t *rlt = (replication_listener_t*)arg;
	char *uri = rlt->server_url;
	map<int, string> tid2tname;
	map<int, string>::iterator tb_it;
	pthread_t id = pthread_self();
	string database_dot_table;
	const char* server_type;
	Gtid gtid = Gtid();

	try {
		Binary_log binlog(create_transport(uri));
		binlog.connect();

		server_type = binlog.get_mysql_server_type_str();

		cout << "Server " << uri << " type: " << server_type << endl;

		Binary_log_event *event;

		while (true) {
			Log_event_header *lheader;

			int result = binlog.wait_for_next_event(&event);

			if (result == ERR_EOF)
				break;

			lheader = event->header();

			switch(event->get_event_type()) {

			case QUERY_EVENT: {
				Query_event *qevent = dynamic_cast<Query_event *>(event);

				std::cout << "Thread: " << id << " server_id " << lheader->server_id
					  << " position " << lheader->next_position << " : Found event of type "
					  << event->get_event_type()
					  << " txt " << get_event_type_str(event->get_event_type())
					  << " query " << qevent->query << " db " << qevent->db_name
					  << std::endl;
				break;
			}

			case GTID_EVENT_MARIADB:
			case GTID_EVENT_MYSQL: {
				Gtid_event *gevent = dynamic_cast<Gtid_event *>(event);

				std::cout << "Thread: " << id << " server_id " << lheader->server_id
					  << " position " << lheader->next_position << " : Found event of type "
					  << event->get_event_type()
					  << " txt " << get_event_type_str(event->get_event_type())
					  << " GTID " << std::string((char *)gevent->m_gtid.get_gtid())
					  << " GTID " << gevent->m_gtid.get_string()
					  << std::endl;

				break;
			}

			case TABLE_MAP_EVENT: {
				Table_map_event *table_map_event= dynamic_cast<Table_map_event*>(event);
				database_dot_table= table_map_event->db_name;
				database_dot_table.append(".");
				database_dot_table.append(table_map_event->table_name);
				tid2tname[table_map_event->table_id]= database_dot_table;
				break;
			}

			case WRITE_ROWS_EVENT:
			case UPDATE_ROWS_EVENT:
			case DELETE_ROWS_EVENT: {
				Row_event *revent = dynamic_cast<Row_event*>(event);
				tb_it= tid2tname.begin();
				tb_it= tid2tname.find(revent->table_id);
				if (tb_it != tid2tname.end())
				{
					database_dot_table= tb_it->second;
				}

				std::cout << "Thread: " << id << " server_id " << lheader->server_id
					  << " position " << lheader->next_position << " : Found event of type "
					  << event->get_event_type()
					  << " txt " << get_event_type_str(event->get_event_type())
					  << " table " << revent->table_id
					  << " tb " << database_dot_table
					  << std::endl;
				break;

			}
			default:
				break;
			} // switch
		} // while
	} // try
	catch(ListenerException e)
	{
		std::cerr << "Listener exception: " << e.what() << std::endl;
	}
	catch(boost::system::error_code e)
	{
		std::cerr << "Listener system error: " << e.message() << std::endl;
	}
	// Try and catch all exceptions
	catch(std::exception const& e)
	{
		std::cerr << "Listener other error: " << e.what() << std::endl;
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

	pthread_exit(NULL);
	return NULL;

}

int main(int argc, char** argv) {

	int number_of_args = argc;
	int i=0,k=0;
	pthread_t *tid=NULL;
	char *uri;
	replication_listener_t *mrl;
	int err=0;

	tid = (pthread_t*)malloc(sizeof(pthread_t) * argc);
	mrl = (replication_listener_t*)calloc(argc, sizeof(replication_listener_t));

	if (argc < 2) {
		std::cerr << "Usage: event_dump <uri>" << std::endl;
		exit(2);
	}

	if (mysql_library_init(num_elements, server_options, server_groups)) {
		std::cerr << "Failed to init MySQL server" << std::endl;
		exit(1);
	}

	argc =0;
	while(argc != number_of_args)
	{
		uri= argv[argc++];

		if ( strncmp("mysql://", uri, 8) == 0) {

			mrl[i].server_url = uri;

			if (argc == 1) {
				mrl[i].is_master = 1;
			}

			err = pthread_create(&(tid[i++]), NULL, &binlog_reader, (void *)&mrl[i]);

			if (err ) {
				perror(NULL);
				break;
			}

		}
	}//end of outer while loop

	for(k=0; k < i; k++)
	{
		err = pthread_join(tid[k], (void **)&(mrl[k]));

		if (err) {
			perror(NULL);
		}
	}

	exit(0);

}
