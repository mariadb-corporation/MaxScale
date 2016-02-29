#ifndef _DBUSERS_H
#define _DBUSERS_H
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

#include <service.h>
#include <arpa/inet.h>


/**
 * @file dbusers.h Extarct user information form the backend database
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/06/13	Mark Riddoch		Initial implementation
 * 25/02/13	Massimiliano Pinto	Added users table refresh rate default values
 * 28/02/14	Massimiliano	Pinto	Added MySQL user and host data structure
 * 03/10/14	Massimiliano	Pinto	Added netmask to MySQL user and host data structure
 * 13/10/14	Massimiliano	Pinto	Added resource to MySQL user and host data structure
 *
 * @endverbatim
 */

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME 30           /* Allowed time interval (in seconds) after last update*/
#define USERS_REFRESH_MAX_PER_TIME 4    /* Max number of load calls within the time interval */

/** Default timeout values used by the connections which fetch user authentication data */
#define DEFAULT_AUTH_CONNECT_TIMEOUT 3
#define DEFAULT_AUTH_READ_TIMEOUT 1
#define DEFAULT_AUTH_WRITE_TIMEOUT 2

/* Max length of fields in the mysql.user table */
#define MYSQL_USER_MAXLEN	128
#define MYSQL_PASSWORD_LEN	41
#define MYSQL_HOST_MAXLEN	60
#define MYSQL_DATABASE_MAXLEN	128
#define MYSQL_TABLE_MAXLEN	64

/**
 * MySQL user and host data structure
 */
typedef struct mysql_user_host_key {
        char *user;
        struct sockaddr_in ipv4;
        int netmask;
	char *resource;
    char hostname[MYSQL_HOST_MAXLEN + 1];
} MYSQL_USER_HOST;

extern int add_mysql_users_with_host_ipv4(USERS *users, const char *user, const char *host,
                                          char *passwd, const char *anydb, const char *db);
extern bool check_service_permissions(SERVICE* service);
extern int dbusers_load(USERS *, const char *filename);
extern int dbusers_save(USERS *, const char *filename);
extern int load_mysql_users(SERVICE *service);
extern int mysql_users_add(USERS *users, MYSQL_USER_HOST *key, char *auth);
extern USERS *mysql_users_alloc();
extern char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key);
extern int reload_mysql_users(SERVICE *service);
extern int replace_mysql_users(SERVICE *service);

#endif
