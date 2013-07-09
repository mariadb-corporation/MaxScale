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
 * @file dbusers.c  - Loading MySQL users from a MySQL backend server, this needs libmysqlclient.so and header files
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 24/06/2013	Massimiliano Pinto	Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <mysql.h>

#include <dcb.h>
#include <service.h>
#include <users.h>

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
int 
load_mysql_users(SERVICE *service)
{  
	MYSQL      *con = NULL;
	MYSQL_ROW  row;
	MYSQL_RES  *result = NULL;
	int        num_fields = 0;
	char       *service_user = NULL;
	char       *service_passwd = NULL;
	int        total_users = 0;
    SERVER	   *server;
    
	serviceGetUser(service, &service_user, &service_passwd);
    /** multi-thread environment requires that thread init succeeds. */
    if (mysql_thread_init()) {
        skygw_log_write_flush(NULL, "ERROR : mysql_thread_init failed.\n");
        return -1;
    }
    
	con = mysql_init(NULL);

 	if (con == NULL) {
		fprintf(stderr, "%s\n", mysql_error(con));
		return -1;
	}

    if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL)) {
        skygw_log_write_flush(NULL, "Fatal : failed to set external connection. "
                              "It is needed for backend server connections. Exiting.\n");
        return -1;
    }
	/*
	 * Attempt to connect to each database in the service in turn until
	 * we find one that we can connect to or until we run out of databases
	 * to try
	 */
	server = service->databases;
	while (server && mysql_real_connect(con,
                                        server->name,
                                        service_user,
                                        service_passwd,
                                        NULL,
                                        server->port,
                                        NULL,
                                        0) == NULL)
	{
		server = server->nextdb;
	}  
	if (server == NULL)
	{
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;
	}

	if (mysql_query(con, "SELECT user, password FROM mysql.user")) {
		fprintf(stderr, ">>>>> %s\n", mysql_error(con));
		mysql_close(con);
		return -1;
	}

	result = mysql_store_result(con);
  
	if (result == NULL) {
		fprintf(stderr, "%s\n", mysql_error(con));
		mysql_close(con);
		return -1;
	}

	num_fields = mysql_num_fields(result);
 
	while ((row = mysql_fetch_row(result))) { 
		// we assume here two fields are returned !!!
		//printf("User %s , Passwd %s\n", row[0], row[1]);

		// now adding to the hastable user and passwd+1 (escaping the first byte that is '*')
		users_add(service->users, row[0], row[1]+1);
		total_users++;
	}

	mysql_free_result(result);
	mysql_close(con);
    mysql_thread_end();
	return total_users;

}
/////

