#pragma once
#ifndef _MAXSCALE_DBUSERS_H
#define _MAXSCALE_DBUSERS_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <maxscale/service.h>
#include <arpa/inet.h>

MXS_BEGIN_DECLS

/**
 * @file dbusers.h Extarct user information form the backend database
 *
 * @verbatim
 * Revision History
 *
 * Date     Who                  Description
 * 25/06/13 Mark Riddoch         Initial implementation
 * 25/02/13 Massimiliano Pinto   Added users table refresh rate default values
 * 28/02/14 Massimiliano Pinto   Added MySQL user and host data structure
 * 03/10/14 Massimiliano Pinto   Added netmask to MySQL user and host data structure
 * 13/10/14 Massimiliano Pinto   Added resource to MySQL user and host data structure
 *
 * @endverbatim
 */

/* Refresh rate limits for load users from database */
#define USERS_REFRESH_TIME         30           /* Allowed time interval (in seconds) after last update*/
#define USERS_REFRESH_MAX_PER_TIME 4    /* Max number of load calls within the time interval */

/** Default timeout values used by the connections which fetch user authentication data */
#define DEFAULT_AUTH_CONNECT_TIMEOUT 3
#define DEFAULT_AUTH_READ_TIMEOUT    1
#define DEFAULT_AUTH_WRITE_TIMEOUT   2

/* Max length of fields in the mysql.user table */
#define MYSQL_USER_MAXLEN     128
#define MYSQL_PASSWORD_LEN    41
#define MYSQL_HOST_MAXLEN     60
#define MYSQL_DATABASE_MAXLEN 128
#define MYSQL_TABLE_MAXLEN    64

/** Cache directory and file names */
static const char DBUSERS_DIR[] = "cache";
static const char DBUSERS_FILE[] = "dbusers";

/**
 * MySQL user and host data structure
 */
typedef struct mysql_user_host_key
{
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
extern int load_mysql_users(SERV_LISTENER *listener);
extern int mysql_users_add(USERS *users, MYSQL_USER_HOST *key, char *auth);
extern USERS *mysql_users_alloc();
extern char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key);
extern int reload_mysql_users(SERV_LISTENER *listener);
extern int replace_mysql_users(SERV_LISTENER *listener);

MXS_END_DECLS

#endif
