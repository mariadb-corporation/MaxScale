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
static const char DBUSERS_FILE[] = "dbusers.db";

#define MYSQLAUTH_DATABASE_NAME "file:mysqlauth.db?mode=memory&cache=shared"

/** The table name where we store the users */
#define MYSQLAUTH_USERS_TABLE_NAME    "mysqlauth_users"

/** The table name where we store the users */
#define MYSQLAUTH_DATABASES_TABLE_NAME    "mysqlauth_databases"

/** CREATE TABLE statement for the in-memory users table */
static const char users_create_sql[] =
    "CREATE TABLE IF NOT EXISTS " MYSQLAUTH_USERS_TABLE_NAME
    "(user varchar(255), host varchar(255), db varchar(255), anydb boolean, password text)";

/** CREATE TABLE statement for the in-memory databases table */
static const char databases_create_sql[] =
    "CREATE TABLE IF NOT EXISTS " MYSQLAUTH_DATABASES_TABLE_NAME "(db varchar(255))";

/** Query that checks if there's a grant for the user being authenticated */
static const char mysqlauth_validate_user_query[] =
    "SELECT password FROM " MYSQLAUTH_USERS_TABLE_NAME
    " WHERE user = '%s' AND '%s' LIKE host AND (anydb = '1' OR '%s' = '' OR '%s' LIKE db)"
    " LIMIT 1";

/** Query that checks that the database exists */
static const char mysqlauth_validate_database_query[] =
    "SELECT * FROM " MYSQLAUTH_DATABASES_TABLE_NAME " WHERE db = '%s' LIMIT 1";

/** Delete query used to clean up the database before loading new users */
static const char delete_users_query[] = "DELETE FROM " MYSQLAUTH_USERS_TABLE_NAME;

/** Delete query used to clean up the database before loading new users */
static const char delete_databases_query[] = "DELETE FROM " MYSQLAUTH_DATABASES_TABLE_NAME;

/** The insert query template which adds users to the mysqlauth_users table */
static const char insert_user_query[] =
    "INSERT OR REPLACE INTO " MYSQLAUTH_USERS_TABLE_NAME " VALUES ('%s', '%s', %s, %s, %s)";

/** The insert query template which adds the databases to the table */
static const char insert_database_query[] =
    "INSERT OR REPLACE INTO " MYSQLAUTH_DATABASES_TABLE_NAME " VALUES ('%s')";

static const char dump_users_query[] =
    "SELECT user, host, db, anydb, password FROM " MYSQLAUTH_USERS_TABLE_NAME;

static const char dump_databases_query[] =
    "SELECT db FROM " MYSQLAUTH_DATABASES_TABLE_NAME;

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

void add_mysql_user(sqlite3 *handle, const char *user, const char *host,
                    const char *db, bool anydb, const char *pw);

extern int add_mysql_users_with_host_ipv4(USERS *users, const char *user, const char *host,
                                          char *passwd, const char *anydb, const char *db);
extern bool check_service_permissions(SERVICE* service);
extern bool dbusers_load(sqlite3 *handle, const char *filename);
extern bool dbusers_save(sqlite3 *src, const char *filename);
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
