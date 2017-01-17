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

/**
 * @file dbusers.c  - Loading MySQL users from a MySQL backend server, this needs
 * libmysqlclient.so and header files
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 24/06/2013   Massimiliano Pinto  Initial implementation
 * 08/08/2013   Massimiliano Pinto  Fixed bug for invalid memory access in row[1]+1 when row[1] is ""
 * 06/02/2014   Massimiliano Pinto  Mysql user root selected based on configuration flag
 * 26/02/2014   Massimiliano Pinto  Addd: replace_mysql_users() routine may replace users' table
 *                                  based on a checksum
 * 28/02/2014   Massimiliano Pinto  Added Mysql user@host authentication
 * 29/09/2014   Massimiliano Pinto  Added Mysql user@host authentication with wildcard in IPv4 hosts:
 *                                  x.y.z.%, x.y.%.%, x.%.%.%
 * 03/10/14     Massimiliano Pinto  Added netmask to user@host authentication for wildcard in IPv4 hosts
 * 13/10/14     Massimiliano Pinto  Added (user@host)@db authentication
 * 04/12/14     Massimiliano Pinto  Added support for IPv$ wildcard hosts: a.%, a.%.% and a.b.%
 * 25/05/16     Massimiliano Pinto  Removed log message for duplicate entry while adding an user
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "MySQLAuth"

#include <stdio.h>
#include <ctype.h>
#include <mysql.h>
#include <netdb.h>

#include <maxscale/dcb.h>
#include <maxscale/service.h>
#include <maxscale/users.h>
#include "dbusers.h"
#include <maxscale/log_manager.h>
#include <maxscale/secrets.h>
#include <maxscale/protocol/mysql.h>
#include <mysqld_error.h>
#include <regex.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/alloc.h>
#include <maxscale/modutil.h>

/** Don't include the root user */
#define USERS_QUERY_NO_ROOT " AND user.user NOT IN ('root')"

/** User count without databases */
#define MYSQL_USERS_COUNT "SELECT COUNT(1) AS nusers FROM mysql.user"

/** Normal password column name */
#define MYSQL_PASSWORD "password"

/** MySQL 5.7 password column name */
#define MYSQL57_PASSWORD "authentication_string"

/**
 * Query template which resolves user grants and access to databases at the table level
 *
 * The first two parameters for this template should be the 'password' column name.
 * The third parameter is either an empty string the the contents of USERS_QUERY_NO_ROOT
 * if the root user is not included. These three parameters are then repeated in the same
 * order for the remaining parameters. For an example on how it is used see get_users_db_query()
 */
#define MYSQL_USERS_DB_QUERY_TEMPLATE \
    "SELECT  DISTINCT \
    user.user AS user, \
    user.host AS host, \
    user.%s AS password, \
    concat(user.user,user.host,user.%s, \
           user.Select_priv, COALESCE(db.db, '')) AS userdata, \
    user.Select_priv AS anydb, \
    db.db AS db \
    FROM \
    mysql.user LEFT JOIN \
    mysql.db ON user.user=db.user AND user.host=db.host \
    WHERE user.user IS NOT NULL AND user.user <> '' %s \
    UNION \
    SELECT  DISTINCT \
    user.user AS user, \
    user.host AS host, \
    user.%s AS password, \
    concat(user.user,user.host,user.%s, \
           user.Select_priv, COALESCE(tp.db, '')) AS userdata, \
    user.Select_priv AS anydb, \
    tp.db as db FROM \
    mysql.tables_priv AS tp LEFT JOIN \
    mysql.user ON user.user=tp.user AND user.host=tp.host \
    WHERE user.user IS NOT NULL AND user.user <> '' %s"

#define MYSQL_USERS_QUERY_TEMPLATE "SELECT \
    user, host, %s, concat(user, host, %s, Select_priv) AS userdata, \
    Select_priv AS anydb FROM mysql.user WHERE user.user IS NOT NULL AND user.user <> ''"

/** User count query split into two parts. This way the actual query used to
 * fetch the users can be inserted as a subquery between the START and END
 * portions of them. */
#define MYSQL_USERS_COUNT_TEMPLATE_START "SELECT COUNT(1) AS nusers_db FROM ("
#define MYSQL_USERS_COUNT_TEMPLATE_END ") AS tbl_count"

/** The maximum possible length of the query */
#define MAX_QUERY_STR_LEN strlen(MYSQL_USERS_COUNT_TEMPLATE_START MYSQL_USERS_COUNT_TEMPLATE_END \
    MYSQL_USERS_DB_QUERY_TEMPLATE) + strlen(USERS_QUERY_NO_ROOT) * 2 + strlen(MYSQL57_PASSWORD) * 4 + 1

#define LOAD_MYSQL_DATABASE_NAMES "SELECT * \
    FROM ( (SELECT COUNT(1) AS ndbs \
    FROM INFORMATION_SCHEMA.SCHEMATA) AS tbl1, \
    (SELECT GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES \
    WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, \'\\'\',\'\')=CURRENT_USER()) AS tbl2)"

#define ERROR_NO_SHOW_DATABASES "%s: Unable to load database grant information, \
MaxScale authentication will proceed without including database permissions. \
See earlier error messages for user '%s' for more information."

static int add_databases(SERV_LISTENER *listener, MYSQL *con);
static int add_wildcard_users(USERS *users, char* name, char* host,
                              char* password, char* anydb, char* db, HASHTABLE* hash);
static void *dbusers_keyread(int fd);
static int dbusers_keywrite(int fd, void *key);
static void *dbusers_valueread(int fd);
static int dbusers_valuewrite(int fd, void *value);
static int get_all_users(SERV_LISTENER *listener, USERS *users);
static int get_databases(SERV_LISTENER *listener, MYSQL *users);
static int get_users(SERV_LISTENER *listener, USERS *users);
static MYSQL *gw_mysql_init(void);
static int gw_mysql_set_timeouts(MYSQL* handle);
static bool host_has_singlechar_wildcard(const char *host);
static bool host_matches_singlechar_wildcard(const char* user, const char* wild);
static bool is_ipaddress(const char* host);
static char *mysql_format_user_entry(void *data);
static char *mysql_format_user_entry(void *data);
static int normalize_hostname(const char *input_host, char *output_host);
static int resource_add(HASHTABLE *, char *, char *);
static HASHTABLE *resource_alloc();
static void *resource_fetch(HASHTABLE *, char *);
static void resource_free(HASHTABLE *resource);
static int uh_cmpfun(const void* v1, const void* v2);
static int uh_hfun(const void* key);
static MYSQL_USER_HOST *uh_keydup(const MYSQL_USER_HOST* key);
static void uh_keyfree(MYSQL_USER_HOST* key);
static int wildcard_db_grant(char* str);
static void merge_netmask(char *host);
static bool wildcard_domain_match(const char* host1, const char* host2);

/**
 * Get the user data query with databases
 *
 * @param server_version Server version string
 * @param include_root Include root user
 * @param buffer Destination where the query is written. Must be at least
 * MAX_QUERY_STR_LEN bytes long
 * @return Users query with databases included
 */
static char* get_users_db_query(const char* server_version, bool include_root, char* buffer)
{
    const char* password = strstr(server_version, "5.7.") ?
                           MYSQL57_PASSWORD : MYSQL_PASSWORD;

    int nchars = snprintf(buffer, MAX_QUERY_STR_LEN, MYSQL_USERS_DB_QUERY_TEMPLATE,
                          password, password, include_root ? "" : USERS_QUERY_NO_ROOT,
                          password, password, include_root ? "" : USERS_QUERY_NO_ROOT);
    ss_dassert(nchars < MAX_QUERY_STR_LEN);
    (void) nchars;
    return buffer;
}

/**
 * Get the user data query
 *
 * @param server_version Server version string
 * @param include_root Include root user
 * @param buffer Destination where the query is written. Must be at least
 * MAX_QUERY_STR_LEN bytes long
 * @return Users query
 */
static char* get_users_query(const char* server_version, bool include_root, char* buffer)
{
    const char* password = strstr(server_version, "5.7.") ?
                           MYSQL57_PASSWORD : MYSQL_PASSWORD;

    int nchars = snprintf(buffer, MAX_QUERY_STR_LEN, MYSQL_USERS_QUERY_TEMPLATE "%s",
                          password, password, include_root ? "" : USERS_QUERY_NO_ROOT);
    ss_dassert(nchars < MAX_QUERY_STR_LEN);
    (void) nchars;
    return buffer;
}

/**
 * Get the user count query
 *
 * @param server_version Server version string
 * @param buffer Destination where the query is written. Must be at least
 * MAX_QUERY_STR_LEN bytes long
 * @return User count query
 * */
static char* get_usercount_query(const char* server_version, bool include_root, char* buffer)
{
    const char* password = strstr(server_version, "5.7.") ?
                           MYSQL57_PASSWORD : MYSQL_PASSWORD;

    int nchars = snprintf(buffer, MAX_QUERY_STR_LEN, MYSQL_USERS_COUNT_TEMPLATE_START
                          MYSQL_USERS_DB_QUERY_TEMPLATE MYSQL_USERS_COUNT_TEMPLATE_END,
                          password, password, include_root ? "" : USERS_QUERY_NO_ROOT,
                          password, password, include_root ? "" : USERS_QUERY_NO_ROOT);
    ss_dassert(nchars < MAX_QUERY_STR_LEN);
    (void) nchars;
    return buffer;
}

/**
 * Check if the IP address of the user matches the one in the grant. This assumes
 * that the grant has one or more single-character wildcards in it.
 * @param userhost User host address
 * @param wildcardhost Host address in the grant
 * @return True if the host address matches
 */
static bool host_matches_singlechar_wildcard(const char* user, const char* wild)
{
    while (*user != '\0' && *wild != '\0')
    {
        if (*user != *wild && *wild != '_')
        {
            return false;
        }
        user++;
        wild++;
    }
    return true;
}

/**
 * Replace the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 * The replacement is succesful only if the users' table checksums differ
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
int
replace_mysql_users(SERV_LISTENER *listener)
{
    USERS *newusers = mysql_users_alloc();

    if (newusers == NULL)
    {
        return -1;
    }

    spinlock_acquire(&listener->lock);

    /** TODO: Make the listener resource a part of the USERS struct */
    HASHTABLE *oldresources = listener->resources;

    /* load users and grants from the backend database */
    int i = get_users(listener, newusers);

    if (i <= 0)
    {
        /** Failed to load users */
        if (listener->users)
        {
            /* Restore old users and resources */
            users_free(newusers);
            listener->resources = oldresources;
        }
        else
        {
            /* No users allocated, use the empty new one */
            listener->users = newusers;
        }
        spinlock_release(&listener->lock);
        return i;
    }

    /** TODO: Figure out a way to create a checksum function in the backend server
     * so that we can avoid querying the complete list of users every time we
     * need to refresh the users */
    MXS_DEBUG("%lu [replace_mysql_users] users' tables replaced", pthread_self());
    USERS *oldusers = listener->users;
    listener->users = newusers;

    spinlock_release(&listener->lock);

    /* free old resources */
    resource_free(oldresources);

    if (oldusers)
    {
        /* free the old table */
        users_free(oldusers);
    }

    return i;
}

/**
 * Check if the IP address is a valid MySQL IP address. The IP address can contain
 * single or multi-character wildcards as used by MySQL.
 * @param host IP address to check
 * @return True if the address is a valid, MySQL type IP address
 */
static bool is_ipaddress(const char* host)
{
    while (*host != '\0')
    {
        if (!isdigit(*host) && *host != '.' && *host != '_' && *host != '%')
        {
            return false;
        }
        host++;
    }
    return true;
}

/**
 * Check if an IP address has single-character wildcards. A single-character
 * wildcard is represented by an underscore in the MySQL hostnames.
 * @param host Hostname to check
 * @return True if the hostname is a valid IP address with a single character wildcard
 */
static bool host_has_singlechar_wildcard(const char *host)
{
    const char* chrptr = host;
    bool retval = false;

    while (*chrptr != '\0')
    {
        if (!isdigit(*chrptr) && *chrptr != '.')
        {
            if (*chrptr == '_')
            {
                retval = true;
            }
            else
            {
                return false;
            }
        }
        chrptr++;
    }
    return retval;
}

/**
 * Add a new MySQL user with host, password and netmask into the service users table
 *
 * The netmask values are:
 * 0 for any, 32 for single IPv4
 * 24 for a class C from a.b.c.%, 16 for a Class B from a.b.%.% and 8 for a Class A from a.%.%.%
 *
 * @param users         The users table
 * @param user          The user name
 * @param host          The host to add, with possible wildcards
 * @param passwd        The sha1(sha1(passoword)) to add
 * @return              1 on success, 0 on failure and -1 on duplicate user
 */

int add_mysql_users_with_host_ipv4(USERS *users, const char *user, const char *host,
                                   char *passwd, const char *anydb, const char *db)
{
    struct sockaddr_in serv_addr;
    MYSQL_USER_HOST key;
    char ret_ip[400] = "";
    int ret = 0;

    if (users == NULL || user == NULL || host == NULL)
    {
        return ret;
    }

    /* prepare the user@host data struct */
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(&key, 0, sizeof(key));

    /* set user */
    key.user = MXS_STRDUP(user);

    if (key.user == NULL)
    {
        return ret;
    }

    /* for anydb == Y key.resource is '\0' as set by memset */
    if (anydb == NULL)
    {
        key.resource = NULL;
    }
    else
    {
        if (strcmp(anydb, "N") == 0)
        {
            if (db != NULL)
            {
                key.resource = MXS_STRDUP(db);
                MXS_ABORT_IF_NULL(key.resource);
            }
            else
            {
                key.resource = NULL;
            }
        }
        else
        {
            key.resource = MXS_STRDUP("");
            MXS_ABORT_IF_NULL(key.resource);
        }
    }

    /* handle ANY, Class C,B,A */

    /* ANY */
    if (strcmp(host, "%") == 0)
    {
        strcpy(ret_ip, "0.0.0.0");
        key.netmask = 0;
    }
    else if ((strnlen(host, MYSQL_HOST_MAXLEN + 1) <= MYSQL_HOST_MAXLEN) &&
             /** The host is an ip-address and has a '_'-wildcard but not '%'
              * (combination of both is invalid). */
             ((is_ipaddress(host) && host_has_singlechar_wildcard(host)) ||
              /** The host is not an ip-address and has a '%'- or '_'-wildcard (or both). */
              (!is_ipaddress(host) && strpbrk(host, "%_"))))
    {
        strcpy(key.hostname, host);
        strcpy(ret_ip, "0.0.0.0");
        key.netmask = 0;
    }
    else
    {
        /* hostname without % wildcards has netmask = 32 */
        key.netmask = normalize_hostname(host, ret_ip);

        if (key.netmask == -1)
        {
            MXS_ERROR("strdup() failed in normalize_hostname for %s@%s", user, host);
        }
    }

    /* fill IPv4 data struct */
    if (setipaddress(&serv_addr.sin_addr, ret_ip) && strlen(ret_ip))
    {

        /* copy IPv4 data into key.ipv4 */
        memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

        /* if netmask < 32 there are % wildcards */
        if (key.netmask < 32)
        {
            /* let's zero the last IP byte: a.b.c.0 we may have set above to 1*/
            key.ipv4.sin_addr.s_addr &= 0x00FFFFFF;
        }

        /* add user@host as key and passwd as value in the MySQL users hash table */
        if (mysql_users_add(users, &key, passwd))
        {
            ret = 1;
        }
        else if (key.user)
        {
            ret = -1;
        }
    }

    MXS_FREE(key.user);
    MXS_FREE(key.resource);

    return ret;
}

/**
 * Add the database specific grants from mysql.db table into the service resources hashtable
 * environment.
 *
 * @param service   The current service
 * @param users     The users table into which to load the users
 * @return          -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
add_databases(SERV_LISTENER *listener, MYSQL *con)
{
    SERVICE *service = listener->service;
    MYSQL_ROW row;
    MYSQL_RES *result = NULL;
    char *service_user = NULL;
    char *service_passwd = NULL;
    int ndbs = 0;

    char *get_showdbs_priv_query = LOAD_MYSQL_DATABASE_NAMES;

    serviceGetUser(service, &service_user, &service_passwd);

    if (service_user == NULL || service_passwd == NULL)
    {
        return -1;
    }

    if (mysql_query(con, get_showdbs_priv_query))
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error: %s.",
                  service->name,
                  mysql_error(con));
        return -1;
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error: %s.",
                  service->name,
                  mysql_error(con));
        return -1;
    }

    /* Result has only one row */
    row = mysql_fetch_row(result);

    if (row)
    {
        ndbs = atoi(row[0]);
    }
    else
    {
        ndbs = 0;

        MXS_ERROR("Failed to retrieve database names: %s", mysql_error(con));
        MXS_ERROR(ERROR_NO_SHOW_DATABASES, service->name, service_user);
    }

    /* free resut set */
    mysql_free_result(result);

    if (!ndbs)
    {
        /* return if no db names are available */
        return 0;
    }

    if (mysql_query(con, "SHOW DATABASES"))
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error: %s.",
                  service->name,
                  mysql_error(con));

        return -1;
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error: %s.",
                  service->name,
                  mysql_error(con));

        return -1;
    }

    /* insert key and value "" */
    while ((row = mysql_fetch_row(result)))
    {
        if (resource_add(listener->resources, row[0], ""))
        {
            MXS_DEBUG("%s: Adding database %s to the resouce hash.", service->name, row[0]);
        }
    }

    mysql_free_result(result);

    return ndbs;
}

/**
 * Load the database specific grants from mysql.db table into the service resources hashtable
 * environment.
 *
 * @param service   The current service
 * @param users     The users table into which to load the users
 * @return          -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
get_databases(SERV_LISTENER *listener, MYSQL *con)
{
    SERVICE *service = listener->service;
    MYSQL_ROW row;
    MYSQL_RES *result = NULL;
    char *service_user = NULL;
    char *service_passwd = NULL;
    int ndbs = 0;

    char *get_showdbs_priv_query = LOAD_MYSQL_DATABASE_NAMES;

    serviceGetUser(service, &service_user, &service_passwd);

    if (service_user == NULL || service_passwd == NULL)
    {
        return -1;
    }

    if (mysql_query(con, get_showdbs_priv_query))
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error when querying database privileges: %s.",
                  service->name,
                  mysql_error(con));
        return -1;
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error when storing result set of database privilege query: %s.",
                  service->name,
                  mysql_error(con));
        return -1;
    }

    /* Result has only one row */
    row = mysql_fetch_row(result);

    if (row)
    {
        ndbs = atoi(row[0]);
    }
    else
    {
        ndbs = 0;

        MXS_ERROR("Failed to retrieve database names: %s", mysql_error(con));
        MXS_ERROR(ERROR_NO_SHOW_DATABASES, service->name, service_user);
    }

    /* free resut set */
    mysql_free_result(result);

    if (!ndbs)
    {
        /* return if no db names are available */
        return 0;
    }

    if (mysql_query(con, "SHOW DATABASES"))
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error when executing SHOW DATABASES query: %s.",
                  service->name,
                  mysql_error(con));

        return -1;
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading database names for service %s encountered "
                  "error when storing the result set: %s.",
                  service->name,
                  mysql_error(con));

        return -1;
    }

    /* Now populate service->resources hashatable with db names */
    listener->resources = resource_alloc();

    /* insert key and value "" */
    while ((row = mysql_fetch_row(result)))
    {
        MXS_DEBUG("%s: Adding database %s to the resouce hash.", service->name, row[0]);
        resource_add(listener->resources, row[0], "");
    }

    mysql_free_result(result);

    return ndbs;
}

/**
 * Load the user/passwd from mysql.user table into the service users' hashtable
 * environment from all the backend servers.
 *
 * @param service   The current service
 * @param users     The users table into which to load the users
 * @return          -1 on any error or the number of users inserted
 */
static int
get_all_users(SERV_LISTENER *listener, USERS *users)
{
    SERVICE *service = listener->service;
    MYSQL *con = NULL;
    MYSQL_ROW row;
    MYSQL_RES *result = NULL;
    char *service_user = NULL;
    char *service_passwd = NULL;
    char *dpwd = NULL;
    int total_users = 0;
    SERVER_REF *server;
    const char *userquery;
    char *tmp;
    unsigned char hash[SHA_DIGEST_LENGTH] = "";
    char *users_data = NULL;
    char *final_data = NULL;
    char dbnm[MYSQL_DATABASE_MAXLEN + 1];
    int nusers = -1;
    int users_data_row_len = MYSQL_USER_MAXLEN + MYSQL_HOST_MAXLEN +
                             MYSQL_PASSWORD_LEN + sizeof(char) + MYSQL_DATABASE_MAXLEN;
    int dbnames = 0;
    int db_grants = 0;
    bool anon_user = false;

    if (serviceGetUser(service, &service_user, &service_passwd) == 0)
    {
        ss_dassert(service_passwd == NULL || service_user == NULL);
        return -1;
    }

    if (service->svc_do_shutdown)
    {
        return -1;
    }

    dpwd = decryptPassword(service_passwd);
    final_data = (char*) MXS_MALLOC(sizeof(char));
    MXS_ABORT_IF_NULL(final_data);
    *final_data = '\0';

    /**
     * Attempt to connect to one of the databases database or until we run
     * out of databases
     * to try
     */
    server = service->dbref;

    if (server == NULL)
    {
        goto cleanup;
    }

    listener->resources = resource_alloc();

    while (server != NULL)
    {
        while (!service->svc_do_shutdown && server != NULL)
        {
            con = gw_mysql_init();
            if (con)
            {
                if (mxs_mysql_real_connect(con, server->server, service_user, dpwd) == NULL)
                {
                    MXS_ERROR("Failure loading users data from backend "
                              "[%s:%i] for service [%s]. MySQL error %i, %s",
                              server->server->name, server->server->port,
                              service->name, mysql_errno(con), mysql_error(con));
                    mysql_close(con);
                }
                else
                {
                    /** Successfully connected to a server */
                    break;
                }
            }
            else
            {
                server = NULL;
                break;
            }

            server = server->next;
        }

        if (server == NULL)
        {
            MXS_ERROR("Unable to get user data from backend database "
                      "for service [%s]. Missing server information.",
                      service->name);
            goto cleanup;
        }

        add_databases(listener, con);
        mysql_close(con);
        server = server->next;
    }

    server = service->dbref;

    while (server != NULL)
    {
        while (!service->svc_do_shutdown && server != NULL)
        {
            con = gw_mysql_init();
            if (con)
            {
                if (mxs_mysql_real_connect(con, server->server, service_user, dpwd) == NULL)
                {
                    MXS_ERROR("Failure loading users data from backend "
                              "[%s:%i] for service [%s]. MySQL error %i, %s",
                              server->server->name, server->server->port,
                              service->name, mysql_errno(con), mysql_error(con));
                    mysql_close(con);
                }
                else
                {
                    /** Successfully connected to a server */
                    break;
                }
            }
            else
            {
                server = NULL;
                break;
            }

            server = server->next;
        }

        if (server == NULL)
        {
            MXS_ERROR("Unable to get user data from backend database "
                      "for service [%s]. Missing server information.",
                      service->name);
            goto cleanup;
        }

        if (server->server->server_string == NULL)
        {
            const char *server_string = mysql_get_server_info(con);
            if (!server_set_version_string(server->server, server_string))
            {
                mysql_close(con);
                goto cleanup;
            }
        }

        char querybuffer[MAX_QUERY_STR_LEN];
        /** Count users. Start with users and db grants for users */
        const char *usercount = get_usercount_query(server->server->server_string,
                                                    service->enable_root, querybuffer);
        if (mysql_query(con, usercount))
        {
            if (mysql_errno(con) != ER_TABLEACCESS_DENIED_ERROR)
            {
                /* This is an error we cannot handle, return */
                MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                          service->name,
                          mysql_error(con));
                mysql_close(con);
                goto cleanup;
            }
            else
            {
                /*
                 * We have got ER_TABLEACCESS_DENIED_ERROR
                 * try counting users from mysql.user without DB names.
                 */
                if (mysql_query(con, MYSQL_USERS_COUNT))
                {
                    MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                              service->name,
                              mysql_error(con));
                    mysql_close(con);
                    goto cleanup;
                }
            }
        }

        result = mysql_store_result(con);

        if (result == NULL)
        {
            MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                      service->name,
                      mysql_error(con));
            mysql_close(con);
            goto cleanup;
        }

        row = mysql_fetch_row(result);

        nusers = atoi(row[0]);

        mysql_free_result(result);

        if (!nusers)
        {
            MXS_ERROR("Counting users for service %s returned 0.", service->name);
            mysql_close(con);
            goto cleanup;
        }

        userquery = get_users_db_query(server->server->server_string,
                                       service->enable_root, querybuffer);

        /* send first the query that fetches users and db grants */
        if (mysql_query(con, userquery))
        {
            /*
             * An error occurred executing the query
             *
             * Check mysql_errno() against ER_TABLEACCESS_DENIED_ERROR)
             */

            if (1142 != mysql_errno(con))
            {
                /* This is an error we cannot handle, return */

                MXS_ERROR("Loading users with dbnames for service [%s] encountered "
                          "error: [%s], MySQL errno %i",
                          service->name,
                          mysql_error(con),
                          mysql_errno(con));

                mysql_close(con);

                goto cleanup;
            }
            else
            {
                /*
                 * We have got ER_TABLEACCESS_DENIED_ERROR
                 * try loading users from mysql.user without DB names.
                 */

                MXS_ERROR("Failed to retrieve users: %s", mysql_error(con));
                MXS_ERROR(ERROR_NO_SHOW_DATABASES, service->name, service_user);

                userquery = get_users_query(server->server->server_string,
                                            service->enable_root, querybuffer);

                if (mysql_query(con, userquery))
                {
                    MXS_ERROR("Loading users for service [%s] encountered "
                              "error: [%s], code %i",
                              service->name,
                              mysql_error(con),
                              mysql_errno(con));

                    mysql_close(con);

                    goto cleanup;
                }

                /* users successfully loaded but without db grants */

                MXS_NOTICE("Loading users from [mysql.user] without access to [mysql.db] for "
                           "service [%s]. MaxScale Authentication with DBname on connect "
                           "will not consider database grants.",
                           service->name);
            }
        }
        else
        {
            /*
             * users successfully loaded with db grants.
             */
            MXS_DEBUG("[%s] Loading users with db grants.", service->name);
            db_grants = 1;
        }

        result = mysql_store_result(con);

        if (result == NULL)
        {
            MXS_ERROR("Loading users for service %s encountered error: %s.",
                      service->name,
                      mysql_error(con));

            mysql_free_result(result);
            mysql_close(con);

            goto cleanup;
        }

        users_data = (char *) MXS_CALLOC(nusers, (users_data_row_len * sizeof(char)) + 1);

        if (users_data == NULL)
        {
            mysql_free_result(result);
            mysql_close(con);

            goto cleanup;
        }

        while ((row = mysql_fetch_row(result)))
        {

            /**
             * Up to six fields could be returned.
             * user,host,passwd,concat(),anydb,db
             * passwd+1 (escaping the first byte that is '*')
             */

            int rc = 0;
            char *password = NULL;

            /** If the username is empty, the backend server still has anonymous
             * user in it. This will mean that localhost addresses do not match
             * the wildcard host '%' */
            if (strlen(row[0]) == 0)
            {
                anon_user = true;
                continue;
            }

            if (row[2] != NULL)
            {
                /* detect mysql_old_password (pre 4.1 protocol) */
                if (strlen(row[2]) == 16)
                {
                    MXS_ERROR("%s: The user %s@%s has on old password in the "
                              "backend database. MaxScale does not support these "
                              "old passwords. This user will not be able to connect "
                              "via MaxScale. Update the users password to correct "
                              "this.",
                              service->name,
                              row[0],
                              row[1]);
                    continue;
                }

                if (strlen(row[2]) > 1)
                {
                    password = row[2] + 1;
                }
                else
                {
                    password = row[2];
                }
            }

            /*
             * add user@host and DB global priv and specificsa grant (if possible)
             */
            bool havedb = false;

            if (db_grants)
            {
                /* we have dbgrants, store them */
                if (row[5])
                {
                    unsigned long *rowlen = mysql_fetch_lengths(result);
                    memcpy(dbnm, row[5], rowlen[5]);
                    memset(dbnm + rowlen[5], 0, 1);
                    havedb = true;
                    if (service->strip_db_esc)
                    {
                        strip_escape_chars(dbnm);
                        MXS_DEBUG("[%s]: %s -> %s",
                                  service->name,
                                  row[5],
                                  dbnm);
                    }
                }

                rc = add_mysql_users_with_host_ipv4(users, row[0], row[1],
                                                    password, row[4],
                                                    havedb ? dbnm : NULL);

                MXS_DEBUG("%s: Adding user:%s host:%s anydb:%s db:%s.",
                          service->name, row[0], row[1], row[4],
                          havedb ? dbnm : NULL);
            }
            else
            {
                /* we don't have dbgrants, simply set ANY DB for the user */
                rc = add_mysql_users_with_host_ipv4(users, row[0], row[1],
                                                    password, "Y", NULL);
            }

            if (rc == 1)
            {
                if (db_grants)
                {
                    char dbgrant[MYSQL_DATABASE_MAXLEN + 1] = "";
                    if (row[4] != NULL)
                    {
                        if (strcmp(row[4], "Y") == 0)
                        {
                            strcpy(dbgrant, "ANY");
                        }
                        else if (row[5])
                        {
                            strncpy(dbgrant, row[5], MYSQL_DATABASE_MAXLEN);
                            dbgrant[MYSQL_DATABASE_MAXLEN] = 0;
                        }
                    }

                    if (!strlen(dbgrant))
                    {
                        strcpy(dbgrant, "no db");
                    }

                    /* Log the user being added with its db grants */
                    MXS_INFO("%s: User %s@%s for database %s added to service user table.",
                             service->name, row[0], row[1], dbgrant);
                }
                else
                {
                    /* Log the user being added (without db grants) */
                    MXS_INFO("%s: User %s@%s added to service user table.",
                             service->name, row[0], row[1]);
                }

                /* Append data in the memory area for SHA1 digest */
                strncat(users_data, row[3], users_data_row_len);
                total_users++;
            }
            else
            {
                /** Log errors and not the duplicate user */
                if (service->log_auth_warnings && rc != -1)
                {
                    MXS_WARNING("Failed to add user %s@%s for service [%s]."
                                " This user will be unavailable via MaxScale.",
                                row[0], row[1], service->name);
                }
            }
        }

        mysql_free_result(result);
        mysql_close(con);

        if ((tmp = MXS_REALLOC(final_data, (strlen(final_data) + strlen(users_data)
                                            + 1) * sizeof(char))) == NULL)
        {
            MXS_FREE(users_data);
            goto cleanup;
        }

        final_data = tmp;

        strcat(final_data, users_data);
        MXS_FREE(users_data);

        if (service->users_from_all)
        {
            server = server->next;
        }
        else
        {
            server = NULL;
        }
    }

    /* compute SHA1 digest for users' data */
    SHA1((const unsigned char *) final_data, strlen(final_data), hash);

    memcpy(users->cksum, hash, SHA_DIGEST_LENGTH);

    /** Set the parameter if it is not configured by the user */
    if (service->localhost_match_wildcard_host == SERVICE_PARAM_UNINIT)
    {
        service->localhost_match_wildcard_host = anon_user ? 0 : 1;
    }
cleanup:

    MXS_FREE(dpwd);
    MXS_FREE(final_data);

    return total_users;
}

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service   The current service
 * @param users     The users table into which to load the users
 * @return          -1 on any error or the number of users inserted
 */
static int
get_users(SERV_LISTENER *listener, USERS *users)
{
    SERVICE *service = listener->service;
    MYSQL *con = NULL;
    MYSQL_ROW row;
    MYSQL_RES *result = NULL;
    char *service_user = NULL;
    char *service_passwd = NULL;
    char *dpwd;
    int total_users = 0;
    SERVER_REF *server;
    const char *userquery;
    unsigned char hash[SHA_DIGEST_LENGTH] = "";
    char *users_data = NULL;
    int nusers = 0;
    int users_data_row_len = MYSQL_USER_MAXLEN +
                             MYSQL_HOST_MAXLEN +
                             MYSQL_PASSWORD_LEN +
                             sizeof(char) +
                             MYSQL_DATABASE_MAXLEN;
    int db_grants = 0;
    char dbnm[MYSQL_DATABASE_MAXLEN + 1];
    bool anon_user = false;

    if (serviceGetUser(service, &service_user, &service_passwd) == 0)
    {
        ss_dassert(service_passwd == NULL || service_user == NULL);
        return -1;
    }

    if (service->users_from_all)
    {
        return get_all_users(listener, users);
    }

    con = gw_mysql_init();

    if (!con)
    {
        return -1;
    }

    /**
     * Attempt to connect to one of the databases database or until we run
     * out of databases
     * to try
     */
    server = service->dbref;
    dpwd = decryptPassword(service_passwd);

    /* Select a server with Master bit, if available */
    while (server != NULL && !(server->server->status & SERVER_MASTER))
    {
        server = server->next;
    }

    if (service->svc_do_shutdown)
    {
        MXS_FREE(dpwd);
        mysql_close(con);
        return -1;
    }

    /* Try loading data from master server */
    if (server != NULL &&
        (mxs_mysql_real_connect(con, server->server, service_user, dpwd) != NULL))
    {
        MXS_DEBUG("Loading data from backend database with "
                  "Master role [%s:%i] for service [%s]",
                  server->server->name,
                  server->server->port,
                  service->name);
    }
    else
    {
        mysql_close(con);
        /* load data from other servers via loop */
        server = service->dbref;

        while (!service->svc_do_shutdown && server != NULL)
        {
            con = gw_mysql_init();
            if (con)
            {
                if (mxs_mysql_real_connect(con, server->server, service_user, dpwd) == NULL)
                {
                    MXS_ERROR("Failure loading users data from backend "
                              "[%s:%i] for service [%s]. MySQL error %i, %s",
                              server->server->name, server->server->port,
                              service->name, mysql_errno(con), mysql_error(con));
                    mysql_close(con);
                }
                else
                {
                    /** Successfully connected to a server */
                    break;
                }
            }
            else
            {
                server = NULL;
                break;
            }

            server = server->next;
        }

        if (service->svc_do_shutdown)
        {
            MXS_FREE(dpwd);
            mysql_close(con);
            return -1;
        }

        if (server != NULL)
        {
            MXS_DEBUG("Loading data from backend database [%s:%i] for service [%s]",
                      server->server->name, server->server->port, service->name);
        }
    }

    MXS_FREE(dpwd);

    if (server == NULL)
    {
        MXS_ERROR("Unable to get user data from backend database for service [%s]."
                  " Failed to connect to any of the backend databases.", service->name);
        return -1;
    }

    if (server->server->server_string == NULL)
    {
        const char *server_string = mysql_get_server_info(con);
        if (!server_set_version_string(server->server, server_string))
        {
            mysql_close(con);
            return -1;
        }
    }

    char querybuffer[MAX_QUERY_STR_LEN];
    const char *usercount = get_usercount_query(server->server->server_string,
                                                service->enable_root, querybuffer);
    /** Count users. Start with users and db grants for users */
    if (mysql_query(con, usercount))
    {
        if (mysql_errno(con) != ER_TABLEACCESS_DENIED_ERROR)
        {
            /* This is an error we cannot handle, return */
            MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                      service->name, mysql_error(con));
            mysql_close(con);
            return -1;
        }
        else
        {
            /*
             * We have got ER_TABLEACCESS_DENIED_ERROR
             * try counting users from mysql.user without DB names.
             */
            if (mysql_query(con, MYSQL_USERS_COUNT))
            {
                MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                          service->name, mysql_error(con));
                mysql_close(con);
                return -1;
            }
        }
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading users for service [%s] encountered error: [%s].",
                  service->name, mysql_error(con));
        mysql_close(con);
        return -1;
    }

    row = mysql_fetch_row(result);

    nusers = atoi(row[0]);

    mysql_free_result(result);

    if (!nusers)
    {
        MXS_ERROR("Counting users for service %s returned 0.", service->name);
        mysql_close(con);
        return -1;
    }

    userquery = get_users_db_query(server->server->server_string,
                                   service->enable_root, querybuffer);
    /* send first the query that fetches users and db grants */
    if (mysql_query(con, userquery))
    {
        /*
         * An error occurred executing the query
         *
         * Check mysql_errno() against ER_TABLEACCESS_DENIED_ERROR)
         */

        if (1142 != mysql_errno(con))
        {
            /* This is an error we cannot handle, return */

            MXS_ERROR("Loading users with dbnames for service [%s] encountered "
                      "error: [%s], MySQL errno %i", service->name,
                      mysql_error(con), mysql_errno(con));

            mysql_close(con);
            return -1;
        }
        else
        {
            /*
             * We have got ER_TABLEACCESS_DENIED_ERROR
             * try loading users from mysql.user without DB names.
            */
            MXS_ERROR("Failed to retrieve users: %s", mysql_error(con));
            MXS_ERROR(ERROR_NO_SHOW_DATABASES, service->name, service_user);

            userquery = get_users_query(server->server->server_string,
                                        service->enable_root, querybuffer);

            if (mysql_query(con, userquery))
            {
                MXS_ERROR("Loading users for service [%s] encountered error: "
                          "[%s], code %i", service->name, mysql_error(con),
                          mysql_errno(con));

                mysql_close(con);
                return -1;
            }

            /* users successfully loaded but without db grants */

            MXS_NOTICE("Loading users from [mysql.user] without access to [mysql.db] for "
                       "service [%s]. MaxScale Authentication with DBname on connect "
                       "will not consider database grants.", service->name);
        }
    }
    else
    {
        /** Users successfully loaded with database  grants */
        db_grants = 1;
    }

    result = mysql_store_result(con);

    if (result == NULL)
    {
        MXS_ERROR("Loading users for service %s encountered error: %s.",
                  service->name, mysql_error(con));

        mysql_free_result(result);
        mysql_close(con);
        return -1;
    }

    users_data = (char *) MXS_CALLOC(nusers, (users_data_row_len * sizeof(char)) + 1);

    if (users_data == NULL)
    {
        mysql_free_result(result);
        mysql_close(con);
        return -1;
    }

    if (db_grants)
    {
        /* load all mysql database names */
        ss_debug(int dbnames = ) get_databases(listener, con);
        MXS_DEBUG("Loaded %d MySQL Database Names for service [%s]",
                  dbnames, service->name);
    }
    else
    {
        listener->resources = NULL;
    }

    while ((row = mysql_fetch_row(result)))
    {

        /**
         * Up to six fields could be returned.
         * user,host,passwd,concat(),anydb,db
         * passwd+1 (escaping the first byte that is '*')
         */

        int rc = 0;
        char *password = NULL;

        /** If the username is empty, the backend server still has anonymous
         * user in it. This will mean that localhost addresses do not match
         * the wildcard host '%' */
        if (strlen(row[0]) == 0)
        {
            anon_user = true;
            continue;
        }

        if (row[2] != NULL)
        {
            /* detect mysql_old_password (pre 4.1 protocol) */
            if (strlen(row[2]) == 16)
            {
                MXS_ERROR("%s: The user %s@%s has on old password in the "
                          "backend database. MaxScale does not support these "
                          "old passwords. This user will not be able to connect "
                          "via MaxScale. Update the users password to correct "
                          "this.", service->name, row[0], row[1]);
                continue;
            }

            if (strlen(row[2]) > 1)
            {
                password = row[2] + 1;
            }
            else
            {
                password = row[2];
            }
        }

        /*
         * add user@host and DB global priv and specificsa grant (if possible)
         */
        if (db_grants)
        {
            bool havedb = false;
            /* we have dbgrants, store them */
            if (row[5])
            {
                unsigned long *rowlen = mysql_fetch_lengths(result);
                memcpy(dbnm, row[5], rowlen[5]);
                memset(dbnm + rowlen[5], 0, 1);
                havedb = true;
                if (service->strip_db_esc)
                {
                    strip_escape_chars(dbnm);
                    MXS_DEBUG("[%s]: %s -> %s", service->name, row[5], dbnm);
                }
            }

            if (havedb && wildcard_db_grant(row[5]))
            {
                /** Use ANYDB for wildcard grants */
                rc = add_mysql_users_with_host_ipv4(users, row[0], row[1],
                                                    password, "Y", NULL);
            }
            else
            {
                rc = add_mysql_users_with_host_ipv4(users, row[0], row[1],
                                                    password, row[4],
                                                    havedb ? dbnm : NULL);
            }

        }
        else
        {
            /* we don't have dbgrants, simply set ANY DB for the user */
            rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], password,
                                                "Y", NULL);
        }

        if (rc == 1)
        {
            if (db_grants)
            {
                char dbgrant[MYSQL_DATABASE_MAXLEN + 1] = "";
                if (row[4] != NULL)
                {
                    if (strcmp(row[4], "Y") == 0)
                    {
                        strcpy(dbgrant, "ANY");
                    }
                    else if (row[5])
                    {
                        strncpy(dbgrant, row[5], MYSQL_DATABASE_MAXLEN);
                        dbgrant[MYSQL_DATABASE_MAXLEN] = 0;
                    }
                }

                if (!strlen(dbgrant))
                {
                    strcpy(dbgrant, "no db");
                }

                /* Log the user being added with its db grants */
                MXS_INFO("%s: User %s@%s for database %s added to "
                         "service user table.",
                         service->name,
                         row[0],
                         row[1],
                         dbgrant);
            }
            else
            {
                /* Log the user being added (without db grants) */
                MXS_INFO("%s: User %s@%s added to service user table.",
                         service->name,
                         row[0],
                         row[1]);
            }

            /* Append data in the memory area for SHA1 digest */
            strncat(users_data, row[3], users_data_row_len);
            total_users++;
        }
        else
        {
            /** Log errors and not the duplicate user */
            if (service->log_auth_warnings && rc != -1)
            {
                MXS_WARNING("Failed to add user %s@%s for"
                            " service [%s]. This user will be unavailable"
                            " via MaxScale.", row[0], row[1], service->name);
            }
        }
    }

    /* compute SHA1 digest for users' data */
    SHA1((const unsigned char *) users_data, strlen(users_data), hash);

    memcpy(users->cksum, hash, SHA_DIGEST_LENGTH);

    /** Set the parameter if it is not configured by the user */
    if (service->localhost_match_wildcard_host == SERVICE_PARAM_UNINIT)
    {
        service->localhost_match_wildcard_host = anon_user ? 0 : 1;
    }

    MXS_FREE(users_data);
    mysql_free_result(result);
    mysql_close(con);

    return total_users;
}

/**
 * Allocate a new MySQL users table for mysql specific users@host as key
 *
 *  @return The users table
 */
USERS *
mysql_users_alloc()
{
    USERS *rval;

    if ((rval = MXS_CALLOC(1, sizeof(USERS))) == NULL)
    {
        return NULL;
    }

    if ((rval->data = hashtable_alloc(USERS_HASHTABLE_DEFAULT_SIZE, uh_hfun,
                                      uh_cmpfun)) == NULL)
    {
        MXS_FREE(rval);
        return NULL;
    }

    /* set the MySQL user@host print routine for the debug interface */
    rval->usersCustomUserFormat = mysql_format_user_entry;

    /* the key is handled by uh_keydup/uh_keyfree.
     * the value is a (char *): it's handled by strdup/free
     */
    hashtable_memory_fns(rval->data,
                         (HASHCOPYFN) uh_keydup, hashtable_item_strdup,
                         (HASHFREEFN) uh_keyfree, hashtable_item_free);

    return rval;
}

/**
 * Add a new MySQL user to the user table. The user name must be unique
 *
 * @param users     The users table
 * @param user      The user name
 * @param auth      The authentication data
 * @return          The number of users added to the table
 */
int
mysql_users_add(USERS *users, MYSQL_USER_HOST *key, char *auth)
{
    int add;

    if (key == NULL || key->user == NULL)
    {
        return 0;
    }

    atomic_add(&users->stats.n_adds, 1);
    add = hashtable_add(users->data, key, auth);
    atomic_add(&users->stats.n_entries, add);

    return add;
}

/**
 * Fetch the authentication data for a particular user from the users table
 *
 * @param users The MySQL users table
 * @param key   The key with user@host
 * @return  The authentication data or NULL on error
 */
char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key)
{
    if (key == NULL)
    {
        return NULL;
    }
    atomic_add(&users->stats.n_fetches, 1);
    return hashtable_fetch(users->data, key);
}

/**
 * The hash function we use for storing MySQL users as: users@hosts.
 * Currently only IPv4 addresses are supported
 *
 * @param key   The key value, i.e. username@host (IPv4)
 * @return      The hash key
 */

static int uh_hfun(const void* key)
{
    const MYSQL_USER_HOST *hu = (const MYSQL_USER_HOST *) key;

    if (key == NULL || hu == NULL || hu->user == NULL)
    {
        return 0;
    }
    else
    {
        return (*hu->user + * (hu->user + 1) +
                (unsigned int) (hu->ipv4.sin_addr.s_addr & 0xFF000000 / (256 * 256 * 256)));
    }
}

/**
 * The compare function we use for compare MySQL users as: users@hosts.
 * Currently only IPv4 addresses are supported
 *
 * @param key1  The key value, i.e. username@host (IPv4)
 * @param key2  The key value, i.e. username@host (IPv4)
 * @return      The compare value
 */

static int uh_cmpfun(const void* v1, const void* v2)
{
    const MYSQL_USER_HOST *hu1 = (const MYSQL_USER_HOST *) v1;
    const MYSQL_USER_HOST *hu2 = (const MYSQL_USER_HOST *) v2;

    if (v1 == NULL || v2 == NULL)
    {
        return 0;
    }

    if (hu1->user == NULL || hu2->user == NULL)
    {
        return 0;
    }

    /** If the stored user has the unmodified address stored, that means we were not able
     * to resolve it at the time we loaded the users. We need to check if the
     * address contains wildcards and if the user's address matches that. */

    const bool wildcard_host = strlen(hu2->hostname) > 0 && strlen(hu1->hostname) > 0;

    if ((strcmp(hu1->user, hu2->user) == 0) &&
        /** Check for wildcard hostnames */
        ((wildcard_host && host_matches_singlechar_wildcard(hu1->hostname, hu2->hostname)) ||
         /** If no wildcard hostname is stored, check for network address. */
         (!wildcard_host && (hu1->ipv4.sin_addr.s_addr == hu2->ipv4.sin_addr.s_addr) &&
          (hu1->netmask >= hu2->netmask)) ||
         /** Finally, one of the hostnames may be a domain name with wildcards
             while the other is an IP-address. This requires a DNS-lookup. */
         (wildcard_host && wildcard_domain_match(hu1->hostname, hu2->hostname))))
    {
        /* if no database name was passed, auth is ok */
        if (hu1->resource == NULL || (hu1->resource && !strlen(hu1->resource)))
        {
            return 0;
        }
        else
        {
            /* (1) check for no database grants at all and deny auth */
            if (hu2->resource == NULL)
            {
                return 1;
            }
            /* (2) check for ANY database grant and allow auth */
            if (!strlen(hu2->resource))
            {
                return 0;
            }
            /* (3) check for database name specific grant and allow auth */
            if (hu1->resource && hu2->resource && strcmp(hu1->resource,
                                                         hu2->resource) == 0)
            {
                return 0;
            }

            if (hu2->resource && strlen(hu2->resource) && strchr(hu2->resource, '%') != NULL)
            {
                regex_t re;
                char db[MYSQL_DATABASE_MAXLEN * 2 + 1];
                strcpy(db, hu2->resource);
                int len = strlen(db);
                char* ptr = strrchr(db, '%');

                if (ptr == NULL)
                {
                    return 1;
                }

                while (ptr)
                {
                    memmove(ptr + 1, ptr, (len - (ptr - db)) + 1);
                    *ptr = '.';
                    *(ptr + 1) = '*';
                    len = strlen(db);
                    ptr = strrchr(db, '%');
                }

                if ((regcomp(&re, db, REG_ICASE | REG_NOSUB)))
                {
                    return 1;
                }

                if (regexec(&re, hu1->resource, 0, NULL, 0) == 0)
                {
                    regfree(&re);
                    return 0;
                }
                regfree(&re);
            }

            /* no matches, deny auth */
            return 1;
        }
    }
    else
    {
        return 1;
    }
}

/**
 *The key dup function we use for duplicate the users@hosts.
 *
 * @param key   The key value, i.e. username@host ip4/ip6 data
 */

static MYSQL_USER_HOST *uh_keydup(const MYSQL_USER_HOST* key)
{
    if ((key == NULL) || (key->user == NULL))
    {
        return NULL;
    }

    MYSQL_USER_HOST *rval = (MYSQL_USER_HOST *) MXS_CALLOC(1, sizeof(MYSQL_USER_HOST));
    char* user = MXS_STRDUP(key->user);
    char* resource = key->resource ? MXS_STRDUP(key->resource) : NULL;

    if (!user || !rval || (key->resource && !resource))
    {
        MXS_FREE(rval);
        MXS_FREE(user);
        MXS_FREE(resource);
        return NULL;
    }

    rval->user = user;
    rval->ipv4 = key->ipv4;
    rval->netmask = key->netmask;
    rval->resource = resource;
    strcpy(rval->hostname, key->hostname);

    return (void *) rval;
}

/**
 * The key free function we use for freeing the users@hosts data
 *
 * @param key   The key value, i.e. username@host ip4 data
 */
static void uh_keyfree(MYSQL_USER_HOST* key)
{
    if (key)
    {
        MXS_FREE(key->user);
        MXS_FREE(key->resource);
        MXS_FREE(key);
    }
}

/**
 * Format the mysql user as user@host
 * The returned memory must be freed by the caller
 *
 *  @param data     Input data
 *  @return         the MySQL user@host
 */
static char *mysql_format_user_entry(void *data)
{
    MYSQL_USER_HOST *entry;
    char *mysql_user;
    /* the returned user string is "USER" + "@" + "HOST" + '\0' */
    int mysql_user_len = MYSQL_USER_MAXLEN + 1 + INET_ADDRSTRLEN + 10 +
                         MYSQL_USER_MAXLEN + 1;

    if (data == NULL)
    {
        return NULL;
    }

    entry = (MYSQL_USER_HOST *) data;

    mysql_user = (char *) MXS_CALLOC(mysql_user_len, sizeof(char));

    if (mysql_user == NULL)
    {
        return NULL;
    }

    /* format user@host based on wildcards */

    if (entry->ipv4.sin_addr.s_addr == INADDR_ANY && entry->netmask == 0)
    {
        snprintf(mysql_user, mysql_user_len - 1, "%s@%%", entry->user);
    }
    else if ((entry->ipv4.sin_addr.s_addr & 0xFF000000) == 0 && entry->netmask == 24)
    {
        snprintf(mysql_user, mysql_user_len - 1, "%s@%i.%i.%i.%%", entry->user,
                 entry->ipv4.sin_addr.s_addr & 0x000000FF,
                 (entry->ipv4.sin_addr.s_addr & 0x0000FF00) / (256),
                 (entry->ipv4.sin_addr.s_addr & 0x00FF0000) / (256 * 256));
    }
    else if ((entry->ipv4.sin_addr.s_addr & 0xFFFF0000) == 0 && entry->netmask == 16)
    {
        snprintf(mysql_user, mysql_user_len - 1, "%s@%i.%i.%%.%%", entry->user,
                 entry->ipv4.sin_addr.s_addr & 0x000000FF,
                 (entry->ipv4.sin_addr.s_addr & 0x0000FF00) / (256));
    }
    else if ((entry->ipv4.sin_addr.s_addr & 0xFFFFFF00) == 0 && entry->netmask == 8)
    {
        snprintf(mysql_user, mysql_user_len - 1, "%s@%i.%%.%%.%%", entry->user,
                 entry->ipv4.sin_addr.s_addr & 0x000000FF);
    }
    else if (entry->netmask == 32)
    {
        strcpy(mysql_user, entry->user);
        strcat(mysql_user, "@");
        inet_ntop(AF_INET, &(entry->ipv4).sin_addr, mysql_user + strlen(mysql_user),
                  INET_ADDRSTRLEN);
    }
    else
    {
        snprintf(mysql_user, MYSQL_USER_MAXLEN - 5, "Err: %s", entry->user);
        strcat(mysql_user, "@");
        inet_ntop(AF_INET, &(entry->ipv4).sin_addr, mysql_user + strlen(mysql_user),
                  INET_ADDRSTRLEN);
    }

    return mysql_user;
}

/**
 * Remove the resources table
 *
 * @param resources The resources table to remove
 */
static void
resource_free(HASHTABLE *resources)
{
    if (resources)
    {
        hashtable_free(resources);
    }
}

/**
 * Allocate a MySQL database names table
 *
 * @return  The database names table
 */
static HASHTABLE *
resource_alloc()
{
    HASHTABLE *resources;

    if ((resources = hashtable_alloc(10, hashtable_item_strhash, hashtable_item_strcmp)) == NULL)
    {
        return NULL;
    }

    hashtable_memory_fns(resources,
                         hashtable_item_strdup, hashtable_item_strdup,
                         hashtable_item_free, hashtable_item_free);

    return resources;
}

/**
 * Add a new MySQL database name to the resources table. The resource name must
 * be unique.
 * @param resources The resources table
 * @param key       The resource name
 * @param value     The value for resource (not used)
 * @return          The number of resources dded to the table
 */
static int
resource_add(HASHTABLE *resources, char *key, char *value)
{
    return hashtable_add(resources, key, value);
}

/**
 * Fetch a particular database name from the resources table
 *
 * @param resources The MySQL database names table
 * @param key       The database name to fetch
 * @return          The database esists or NULL if not found
 */
static void *
resource_fetch(HASHTABLE *resources, char *key)
{
    return hashtable_fetch(resources, key);
}

/**
 * Normalize hostname with % wildcards to a valid IP string.
 *
 * Valid input values:
 * a.b.c.d, a.b.c.%, a.b.%.%, a.%.%.%
 * Short formats a.% and a.%.% are both converted to a.%.%.%
 * Short format a.b.% is converted to a.b.%.%
 *
 * Last host byte is set to 1, avoiding setipadress() failure
 *
 * @param input_host    The hostname with possible % wildcards
 * @param output_host   The normalized hostname (buffer must be preallocated)
 * @return              The calculated netmask or -1 on failure
 */
static int normalize_hostname(const char *input_host, char *output_host)
{
    int netmask, bytes, bits = 0, found_wildcard = 0;
    char *p, *lasts, *tmp;
    int useorig = 0;

    output_host[0] = 0;
    bytes = 0;

    tmp = MXS_STRDUP(input_host);

    if (tmp == NULL)
    {
        return -1;
    }
    /* Handle hosts with netmasks (e.g. "123.321.123.0/255.255.255.0") by
     * replacing the zeros with '%'.
     */
    merge_netmask(tmp);

    p = strtok_r(tmp, ".", &lasts);
    while (p != NULL)
    {

        if (strcmp(p, "%"))
        {
            if (!isdigit(*p))
            {
                useorig = 1;
            }

            strcat(output_host, p);
            bits += 8;
        }
        else if (bytes == 3)
        {
            found_wildcard = 1;
            strcat(output_host, "1");
        }
        else
        {
            found_wildcard = 1;
            strcat(output_host, "0");
        }
        bytes++;
        p = strtok_r(NULL, ".", &lasts);
        if (p)
        {
            strcat(output_host, ".");
        }
    }
    if (found_wildcard)
    {
        netmask = bits;
        while (bytes++ < 4)
        {
            if (bytes == 4)
            {
                strcat(output_host, ".1");
            }
            else
            {
                strcat(output_host, ".0");
            }
        }
    }
    else
    {
        netmask = 32;
    }

    if (useorig == 1)
    {
        netmask = 32;
        strcpy(output_host, input_host);
    }

    MXS_FREE(tmp);

    return netmask;
}

/**
 * Returns a MYSQL object suitably configured.
 *
 * @return An object or NULL if something fails.
 */
MYSQL *gw_mysql_init()
{
    MYSQL* con = mysql_init(NULL);

    if (con)
    {
        if (gw_mysql_set_timeouts(con) == 0)
        {
            // MYSQL_OPT_USE_REMOTE_CONNECTION must be set if the embedded
            // libary is used. With Connector-C (at least 2.2.1) the call
            // fails.
#if !defined(LIBMARIADB)
            if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL) != 0)
            {
                MXS_ERROR("Failed to set external connection. "
                          "It is needed for backend server connections.");
                mysql_close(con);
                con = NULL;
            }
#endif
        }
        else
        {
            MXS_ERROR("Failed to set timeout values for backend connection.");
            mysql_close(con);
            con = NULL;
        }
    }
    else
    {
        MXS_ERROR("mysql_init: %s", mysql_error(NULL));
    }

    return con;
}

/**
 * Set read, write and connect timeout values for MySQL database connection.
 *
 * @param handle            MySQL handle
 * @param read_timeout      Read timeout value in seconds
 * @param write_timeout     Write timeout value in seconds
 * @param connect_timeout   Connect timeout value in seconds
 *
 * @return 0 if succeed, 1 if failed
 */
static int gw_mysql_set_timeouts(MYSQL* handle)
{
    int rc;

    GATEWAY_CONF* cnf = config_get_global_options();

    if ((rc = mysql_options(handle, MYSQL_OPT_READ_TIMEOUT,
                            (void *) &cnf->auth_read_timeout)))
    {
        MXS_ERROR("Failed to set read timeout for backend connection.");
        goto retblock;
    }

    if ((rc = mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT,
                            (void *) &cnf->auth_conn_timeout)))
    {
        MXS_ERROR("Failed to set connect timeout for backend connection.");
        goto retblock;
    }

    if ((rc = mysql_options(handle, MYSQL_OPT_WRITE_TIMEOUT,
                            (void *) &cnf->auth_write_timeout)))
    {
        MXS_ERROR("Failed to set write timeout for backend connection.");
        goto retblock;
    }

retblock:
    return rc;
}

/*
 * Serialise a key for the dbusers hashtable to a file
 *
 * @param fd    File descriptor to write to
 * @param key   The key to write
 * @return      0 on error, 1 if the key was written
 */
static int
dbusers_keywrite(int fd, void *key)
{
    MYSQL_USER_HOST *dbkey = (MYSQL_USER_HOST *) key;
    int tmp;

    tmp = strlen(dbkey->user);
    if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
    {
        return 0;
    }
    if (write(fd, dbkey->user, tmp) != tmp)
    {
        return 0;
    }
    if (write(fd, &dbkey->ipv4, sizeof(dbkey->ipv4)) != sizeof(dbkey->ipv4))
    {
        return 0;
    }
    if (write(fd, &dbkey->netmask, sizeof(dbkey->netmask)) != sizeof(dbkey->netmask))
    {
        return 0;
    }
    if (dbkey->resource)
    {
        tmp = strlen(dbkey->resource);
        if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
        {
            return 0;
        }
        if (write(fd, dbkey->resource, tmp) != tmp)
        {
            return 0;
        }
    }
    else // NULL is valid, so represent with a length of -1
    {
        tmp = -1;
        if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
        {
            return 0;
        }
    }
    return 1;
}

/**
 * Serialise a value for the dbusers hashtable to a file
 *
 * @param fd    File descriptor to write to
 * @param value The value to write
 * @return      0 on error, 1 if the value was written
 */
static int
dbusers_valuewrite(int fd, void *value)
{
    int tmp;

    tmp = strlen(value);
    if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
    {
        return 0;
    }
    if (write(fd, value, tmp) != tmp)
    {
        return 0;
    }
    return 1;
}

/**
 * Unserialise a key for the dbusers hashtable from a file
 *
 * @param fd    File descriptor to read from
 * @return      Pointer to the new key or NULL on error
 */
static void *
dbusers_keyread(int fd)
{
    MYSQL_USER_HOST *dbkey;

    if ((dbkey = (MYSQL_USER_HOST *) MXS_MALLOC(sizeof(MYSQL_USER_HOST))) == NULL)
    {
        return NULL;
    }

    *dbkey->hostname = '\0';

    int user_size;
    if (read(fd, &user_size, sizeof(user_size)) != sizeof(user_size))
    {
        MXS_FREE(dbkey);
        return NULL;
    }
    if ((dbkey->user = (char *) MXS_MALLOC(user_size + 1)) == NULL)
    {
        MXS_FREE(dbkey);
        return NULL;
    }
    if (read(fd, dbkey->user, user_size) != user_size)
    {
        MXS_FREE(dbkey->user);
        MXS_FREE(dbkey);
        return NULL;
    }
    dbkey->user[user_size] = 0; // NULL Terminate
    if (read(fd, &dbkey->ipv4, sizeof(dbkey->ipv4)) != sizeof(dbkey->ipv4))
    {
        MXS_FREE(dbkey->user);
        MXS_FREE(dbkey);
        return NULL;
    }
    if (read(fd, &dbkey->netmask, sizeof(dbkey->netmask)) != sizeof(dbkey->netmask))
    {
        MXS_FREE(dbkey->user);
        MXS_FREE(dbkey);
        return NULL;
    }

    int res_size;
    if (read(fd, &res_size, sizeof(res_size)) != sizeof(res_size))
    {
        MXS_FREE(dbkey->user);
        MXS_FREE(dbkey);
        return NULL;
    }
    else if (res_size != -1)
    {
        if ((dbkey->resource = (char *) MXS_MALLOC(res_size + 1)) == NULL)
        {
            MXS_FREE(dbkey->user);
            MXS_FREE(dbkey);
            return NULL;
        }
        if (read(fd, dbkey->resource, res_size) != res_size)
        {
            MXS_FREE(dbkey->resource);
            MXS_FREE(dbkey->user);
            MXS_FREE(dbkey);
            return NULL;
        }
        dbkey->resource[res_size] = 0; // NULL Terminate
    }
    else // NULL is valid, so represent with a length of -1
    {
        dbkey->resource = NULL;
    }
    return (void *) dbkey;
}

/**
 * Unserialise a value for the dbusers hashtable from a file
 *
 * @param fd    File descriptor to read from
 * @return      Return the new value data or NULL on error
 */
static void *
dbusers_valueread(int fd)
{
    char *value;
    int tmp;

    if (read(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
    {
        return NULL;
    }
    if ((value = (char *) MXS_MALLOC(tmp + 1)) == NULL)
    {
        return NULL;
    }
    if (read(fd, value, tmp) != tmp)
    {
        MXS_FREE(value);
        return NULL;
    }
    value[tmp] = 0;
    return (void *) value;
}

/**
 * Save the dbusers data to a hashtable file
 *
 * @param users     The hashtable that stores the user data
 * @param filename  The filename to save the data in
 * @return      The number of entries saved
 */
int
dbusers_save(USERS *users, const char *filename)
{
    return hashtable_save(users->data, filename, dbusers_keywrite, dbusers_valuewrite);
}

/**
 * Load the dbusers data from a saved hashtable file
 *
 * @param users     The hashtable that stores the user data
 * @param filename  The filename to laod the data from
 * @return      The number of entries loaded
 */
int
dbusers_load(USERS *users, const char *filename)
{
    return hashtable_load(users->data, filename, dbusers_keyread, dbusers_valueread);
}

/**
 * Check if the database name contains a wildcard character
 * @param str Database grant
 * @return 1 if the name contains the '%' wildcard character, 0 if it does not
 */
static int wildcard_db_grant(char* str)
{
    char* ptr = str;

    while (ptr && *ptr != '\0')
    {
        if (*ptr == '%')
        {
            return 1;
        }
        ptr++;
    }

    return 0;
}

/**
 *
 * @param users Pointer to USERS struct
 * @param name Username of the client
 * @param host Host address of the client
 * @param password Client password
 * @param anydb If the user has access to all databases
 * @param db Database, in wildcard form
 * @param hash Hashtable with all database names
 * @return number of unique grants generated from wildcard database name
 */
static int add_wildcard_users(USERS *users, char* name, char* host, char* password,
                              char* anydb, char* db, HASHTABLE* hash)
{
    HASHITERATOR* iter;
    HASHTABLE* ht = hash;
    char *restr, *ptr, *value;
    int len, err, rval = 0;
    char errbuf[1024];
    regex_t re;

    if (db == NULL || hash == NULL)
    {
        return 0;
    }

    if ((restr = MXS_MALLOC(sizeof(char) * strlen(db) * 2)) == NULL)
    {
        return 0;
    }

    strcpy(restr, db);

    len = strlen(restr);
    ptr = strchr(restr, '%');

    if (ptr == NULL)
    {
        MXS_FREE(restr);
        return 0;
    }

    while (ptr)
    {
        memmove(ptr + 1, ptr, (len - (ptr - restr)) + 1);
        *ptr++ = '.';
        *ptr = '*';
        len = strlen(restr);
        ptr = strchr(restr, '%');
    }

    if ((err = regcomp(&re, restr, REG_ICASE | REG_NOSUB)))
    {
        regerror(err, &re, errbuf, 1024);
        MXS_ERROR("Failed to compile regex when resolving wildcard database grants: %s",
                  errbuf);
        MXS_FREE(restr);
        return 0;
    }

    iter = hashtable_iterator(ht);

    while (iter && (value = hashtable_next(iter)))
    {
        if (regexec(&re, value, 0, NULL, 0) == 0)
        {
            rval += add_mysql_users_with_host_ipv4(users, name, host, password,
                                                   anydb, value);
        }
    }

    hashtable_iterator_free(iter);
    regfree(&re);
    MXS_FREE(restr);

    return rval;
}

/**
 * @brief Check service permissions on one server
 *
 * @param server Server to check
 * @param user Username
 * @param password Password
 * @return True if the service permissions are OK, false if one or more permissions
 * are missing.
 */
static bool check_server_permissions(SERVICE *service, SERVER* server,
                                     const char* user, const char* password)
{
    MYSQL *mysql = gw_mysql_init();

    if (mysql == NULL)
    {
        return false;
    }

    GATEWAY_CONF* cnf = config_get_global_options();
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &cnf->auth_read_timeout);
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &cnf->auth_conn_timeout);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &cnf->auth_write_timeout);

    if (mxs_mysql_real_connect(mysql, server, user, password) == NULL)
    {
        int my_errno = mysql_errno(mysql);

        MXS_ERROR("[%s] Failed to connect to server '%s' (%s:%d) when"
                  " checking authentication user credentials and permissions: %d %s",
                  service->name, server->unique_name, server->name, server->port,
                  my_errno, mysql_error(mysql));

        mysql_close(mysql);
        return my_errno != ER_ACCESS_DENIED_ERROR;
    }

    /** Copy the server charset */
    MY_CHARSET_INFO cs_info;
    mysql_get_character_set_info(mysql, &cs_info);
    server->charset = cs_info.number;

    if (server->server_string == NULL)
    {
        const char *server_string = mysql_get_server_info(mysql);
        server_set_version_string(server, server_string);
    }

    char query[MAX_QUERY_STR_LEN];
    const char* query_pw = strstr(server->server_string, "5.7.") ?
                           MYSQL57_PASSWORD : MYSQL_PASSWORD;
    bool rval = true;
    snprintf(query, sizeof(query), "SELECT user, host, %s, Select_priv FROM mysql.user limit 1", query_pw);

    if (mysql_query(mysql, query) != 0)
    {
        if (mysql_errno(mysql) == ER_TABLEACCESS_DENIED_ERROR)
        {
            MXS_ERROR("[%s] User '%s' is missing SELECT privileges"
                      " on mysql.user table. MySQL error message: %s",
                      service->name, user, mysql_error(mysql));
            rval = false;
        }
        else
        {
            MXS_ERROR("[%s] Failed to query from mysql.user table."
                      " MySQL error message: %s", service->name, mysql_error(mysql));
        }
    }
    else
    {

        MYSQL_RES* res = mysql_use_result(mysql);
        if (res == NULL)
        {
            MXS_ERROR("[%s] Result retrieval failed when checking for permissions to "
                      "the mysql.user table: %s", service->name, mysql_error(mysql));
        }
        else
        {
            mysql_free_result(res);
        }
    }

    if (mysql_query(mysql, "SELECT user, host, db FROM mysql.db limit 1") != 0)
    {
        if (mysql_errno(mysql) == ER_TABLEACCESS_DENIED_ERROR)
        {
            MXS_WARNING("[%s] User '%s' is missing SELECT privileges on mysql.db table. "
                        "Database name will be ignored in authentication. "
                        "MySQL error message: %s", service->name, user, mysql_error(mysql));
        }
        else
        {
            MXS_ERROR("[%s] Failed to query from mysql.db table. MySQL error message: %s",
                      service->name, mysql_error(mysql));
        }
    }
    else
    {
        MYSQL_RES* res = mysql_use_result(mysql);
        if (res == NULL)
        {
            MXS_ERROR("[%s] Result retrieval failed when checking for permissions "
                      "to the mysql.db table: %s", service->name, mysql_error(mysql));
        }
        else
        {
            mysql_free_result(res);
        }
    }

    if (mysql_query(mysql, "SELECT user, host, db FROM mysql.tables_priv limit 1") != 0)
    {
        if (mysql_errno(mysql) == ER_TABLEACCESS_DENIED_ERROR)
        {
            MXS_WARNING("[%s] User '%s' is missing SELECT privileges on mysql.tables_priv table. "
                        "Database name will be ignored in authentication. "
                        "MySQL error message: %s", service->name, user, mysql_error(mysql));
        }
        else
        {
            MXS_ERROR("[%s] Failed to query from mysql.tables_priv table. "
                      "MySQL error message: %s", service->name, mysql_error(mysql));
        }
    }
    else
    {
        MYSQL_RES* res = mysql_use_result(mysql);
        if (res == NULL)
        {
            MXS_ERROR("[%s] Result retrieval failed when checking for permissions "
                      "to the mysql.tables_priv table: %s", service->name, mysql_error(mysql));
        }
        else
        {
            mysql_free_result(res);
        }
    }

    mysql_close(mysql);

    return rval;
}

/**
 * @brief Check if the service user has all required permissions to operate properly.
 *
 * This checks for SELECT permissions on mysql.user, mysql.db and mysql.tables_priv
 * tables and for SHOW DATABASES permissions. If permissions are not adequate,
 * an error message is logged and the service is not started.
 *
 * @param service Service to inspect
 * @return True if service permissions are correct on at least one server, false
 * if permissions are missing or if an error occurred.
 */
bool check_service_permissions(SERVICE* service)
{
    if (is_internal_service(service->routerModule) ||
        config_get_global_options()->skip_permission_checks ||
        service->dbref == NULL) // No servers to check
    {
        return true;
    }

    char *user, *password;

    if (serviceGetUser(service, &user, &password) == 0)
    {
        MXS_ERROR("[%s] Service is missing the user credentials for authentication.",
                  service->name);
        return false;
    }

    char *dpasswd = decryptPassword(password);
    bool rval = false;

    for (SERVER_REF *server = service->dbref; server; server = server->next)
    {
        if (check_server_permissions(service, server->server, user, dpasswd))
        {
            rval = true;
        }
    }

    free(dpasswd);

    return rval;
}

/**
 * If the hostname is of form a.b.c.d/e.f.g.h where e-h is 255 or 0, replace
 * the zeros in the first part with '%' and remove the second part. This does
 * not yet support netmasks completely, but should be sufficient for most
 * situations. In case of error, the hostname may end in an invalid state, which
 * will cause an error later on.
 *
 * @param host  The hostname, which is modified in-place. If merging is unsuccessful,
 *              it may end up garbled.
 */
static void merge_netmask(char *host)
{
    char *delimiter_loc = strchr(host, '/');
    if (delimiter_loc == NULL)
    {
        return; // Nothing to do
    }
    /* If anything goes wrong, we put the '/' back in to ensure the hostname
     * cannot be used.
     */
    *delimiter_loc = '\0';

    char *ip_token_loc = host;
    char *mask_token_loc = delimiter_loc + 1; // This is at minimum a \0

    while (ip_token_loc && mask_token_loc)
    {
        if (strncmp(mask_token_loc, "255", 3) == 0)
        {
            // Skip
        }
        else if (*mask_token_loc == '0' && *ip_token_loc == '0')
        {
            *ip_token_loc = '%';
        }
        else
        {
            /* Any other combination is considered invalid. This may leave the
             * hostname in a partially modified state.
             * TODO: handle more cases
             */
            *delimiter_loc = '/';
            MXS_ERROR("Unrecognized IP-bytes in host/mask-combination. "
                      "Merge incomplete: %s", host);
            return;
        }

        ip_token_loc = strchr(ip_token_loc, '.');
        mask_token_loc = strchr(mask_token_loc, '.');
        if (ip_token_loc && mask_token_loc)
        {
            ip_token_loc++;
            mask_token_loc++;
        }
    }
    if (ip_token_loc || mask_token_loc)
    {
        *delimiter_loc = '/';
        MXS_ERROR("Unequal number of IP-bytes in host/mask-combination. "
                  "Merge incomplete: %s", host);
    }
}

/**
 * @brief Check if an ip matches a wildcard hostname.
 *
 * One of the parameters should be an IP-address without wildcards, the other a
 * hostname with wildcards. The hostname corresponding to the ip-address will be
 * looked up and compared to the hostname with wildcard(s). Any error in the
 * parameters or looking up the hostname will result in a false match.
 *
 * @param ip-address or a hostname with wildcard(s)
 * @param ip-address or a hostname with wildcard(s)
 * @return True if the host represented by the IP matches the wildcard string
 */
static bool wildcard_domain_match(const char *host1, const char *host2)
{
    ss_dassert(host1 && host2);

    /* One of the parameters must be a valid IP, the other should be a domain name,
     * with wildcard characters '%' and/or '_'.
     */
    const char *ip_address;
    const char *wc_domain;

    if (is_ipaddress(host1) && !strpbrk(host1, "%_") && !is_ipaddress(host2) &&
        strpbrk(host2, "%_"))
    {
        ip_address = host1;
        wc_domain = host2;
    }
    else if (is_ipaddress(host2) && !strpbrk(host2, "%_") && !is_ipaddress(host1) &&
             strpbrk(host1, "%_"))
    {
        ip_address = host2;
        wc_domain = host1;
    }
    else
    {
        /* TODO: When/if this function is refactored out from hashmap, enable
         * this error message.
         * MXS_ERROR("Invalid parameters: one must be an IP-address and the other a "
         *       "hostname with wildcards. P1: '%s', P2: '%s'.", host1, host2);
         * */
        return false;
    }

    /* Looks like the parameters are valid. First, convert the client IP string
     * to binary form. This is somewhat silly, since just a while ago we had the
     * binary address but had to zero it. dbusers.c should be refactored to fix this.
     */
    struct sockaddr_in bin_address;
    bin_address.sin_family = AF_INET;
    if (inet_pton(bin_address.sin_family, ip_address, &(bin_address.sin_addr)) != 1)
    {
        MXS_ERROR("Could not convert to binary ip-address: '%s'.", ip_address);
        return false;
    }

    /* Try to lookup the domain name of the given IP-address. This is a slow
     * i/o-operation, which will stall the entire thread. TODO: cache results
     * if this feature is used often.
     */
    MXS_DEBUG("Resolving '%s'", ip_address);
    char client_hostname[MYSQL_HOST_MAXLEN];
    int lookup_result = getnameinfo(
                            (struct sockaddr*)&bin_address, sizeof(struct sockaddr_in),
                            client_hostname, sizeof(client_hostname),
                            NULL, 0, // No need for the port
                            NI_NAMEREQD); // Text address only

    if (lookup_result != 0)
    {
        MXS_ERROR("Client hostname lookup failed, getnameinfo() returned: '%s'.",
                  gai_strerror(lookup_result));
    }
    else
    {
        MXS_DEBUG("IP-lookup success, hostname is: '%s'", client_hostname);
        /* We have a host name, try to match regular expression.
         * modutil_mysql_wildcard_match() translates sql-wildcards to pcre2-format. */
        mxs_pcre2_result_t regex_result = modutil_mysql_wildcard_match(wc_domain,
                                                                       client_hostname);
        if (regex_result == MXS_PCRE2_MATCH)
        {
            return true;
        }
        else if (regex_result == MXS_PCRE2_ERROR)
        {
            MXS_ERROR("Malformed host name for regex matching: '%s'.", wc_domain);
        }
    }
    return false;
}
