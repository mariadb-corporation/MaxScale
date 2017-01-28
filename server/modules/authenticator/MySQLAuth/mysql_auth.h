#pragma once
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

/*
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 02/02/2016   Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "MySQLAuth"

#include <maxscale/cdefs.h>

#include <stdint.h>
#include <arpa/inet.h>

#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/service.h>
#include <maxscale/sqlite3.h>
#include <maxscale/protocol/mysql.h>

MXS_BEGIN_DECLS

/** Cache directory and file names */
static const char DBUSERS_DIR[] = "cache";
static const char DBUSERS_FILE[] = "dbusers";

#define MYSQLAUTH_DATABASE_NAME "file:mysqlauth.db?mode=memory&cache=shared"

/** The table name where we store the users */
#define MYSQLAUTH_TABLE_NAME    "mysqlauth_users"

/** CREATE TABLE statement for the in-memory table */
static const char create_sql[] =
    "CREATE TABLE IF NOT EXISTS " MYSQLAUTH_TABLE_NAME
    "(user varchar(255), host varchar(255), db varchar(255), anydb boolean, password text)";

/** The query that is executed when a user is authenticated */
static const char mysqlauth_validation_query[] =
    "SELECT password FROM " MYSQLAUTH_TABLE_NAME
    " WHERE user = '%s' AND '%s' LIKE host AND (anydb = '1' OR '%s' = '' OR '%s' LIKE db)"
    " LIMIT 1";

/** Delete query used to clean up the database before loading new users */
static const char delete_query[] = "DELETE FROM " MYSQLAUTH_TABLE_NAME;

/** The insert query template which adds users to the mysqlauth_users table */
static const char insert_sql_pattern[] =
    "INSERT INTO " MYSQLAUTH_TABLE_NAME " VALUES ('%s', '%s', %s, %s, %s)";

/** Used for NULL value creation in the INSERT query */
static const char null_token[] = "NULL";

/** Flags for sqlite3_open_v2() */
static int db_flags = SQLITE_OPEN_READWRITE |
                      SQLITE_OPEN_CREATE |
                      SQLITE_OPEN_URI |
                      SQLITE_OPEN_SHAREDCACHE;

typedef struct mysql_auth
{
    sqlite3 *handle;          /**< SQLite3 database handle */
    char *cache_dir;          /**< Custom cache directory location */
    bool inject_service_user; /**< Inject the service user into the list of users */
    bool skip_auth;           /**< Authentication will always be successful */
} MYSQL_AUTH;

/** Common structure for both backend and client authenticators */
typedef struct gssapi_auth
{
    sqlite3 *handle;              /**< SQLite3 database handle */
} mysql_auth_t;

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
extern int mysql_users_add(USERS *users, MYSQL_USER_HOST *key, char *auth);
extern USERS *mysql_users_alloc();
extern char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key);
extern int replace_mysql_users(SERV_LISTENER *listener);

int gw_check_mysql_scramble_data(DCB *dcb,
                                 uint8_t *token,
                                 unsigned int token_len,
                                 uint8_t *scramble,
                                 unsigned int scramble_len,
                                 const char *username,
                                 uint8_t *stage1_hash);
int check_db_name_after_auth(DCB *dcb, char *database, int auth_ret);
int gw_find_mysql_user_password_sha1(
    const char *username,
    uint8_t *gateway_password,
    DCB *dcb);
bool validate_mysql_user(sqlite3 *handle, DCB *dcb, MYSQL_session *session);

MXS_END_DECLS
