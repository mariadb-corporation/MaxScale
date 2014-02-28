#ifndef _DBUSERS_H
#define _DBUSERS_H
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
#include <service.h>

/**
 * @file dbusers.h Extarct user information form the backend database
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/06/13	Mark Riddoch		Initial implementation
 * 25/02/13	Massimiliano Pinto	Added users table refresh rate default values
 *
 * @endverbatim
 */

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME 30           /* Allowed time interval (in seconds) after last update*/
#define USERS_REFRESH_MAX_PER_TIME 4    /* Max number of load calls within the time interval */

/* Max length of fields in the mysql.user table */
#define MYSQL_USER_MAXLEN	128
#define MYSQL_PASSWORD_LEN	41
#define MYSQL_HOST_MAXLEN	60
#define MYSQL_DATABASE_MAXLEN	128

extern int load_mysql_users(SERVICE *service);
extern int reload_mysql_users(SERVICE *service);
extern int replace_mysql_users(SERVICE *service);
#endif
