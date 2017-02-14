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
 * Loading MySQL users from a MySQL backend server
 */

#include "mysql_auth.h"

#include <stdio.h>
#include <ctype.h>
#include <mysql.h>
#include <netdb.h>

#include <maxscale/dcb.h>
#include <maxscale/service.h>
#include <maxscale/users.h>
#include <maxscale/log_manager.h>
#include <maxscale/secrets.h>
#include <maxscale/protocol/mysql.h>
#include <mysqld_error.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/alloc.h>

/** Don't include the root user */
#define USERS_QUERY_NO_ROOT " AND user.user NOT IN ('root')"

/** Normal password column name */
#define MYSQL_PASSWORD "password"

/** MySQL 5.7 password column name */
#define MYSQL57_PASSWORD "authentication_string"

#define NEW_LOAD_DBUSERS_QUERY "SELECT u.user, u.host, d.db, u.select_priv, u.%s \
    FROM mysql.user AS u LEFT JOIN mysql.db AS d \
    ON (u.user = d.user AND u.host = d.host) %s \
    UNION \
    SELECT u.user, u.host, t.db, u.select_priv, u.%s \
    FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t \
    ON (u.user = t.user AND u.host = t.host) %s"

static int get_users(SERV_LISTENER *listener);
static MYSQL *gw_mysql_init(void);
static int gw_mysql_set_timeouts(MYSQL* handle);
static char *mysql_format_user_entry(void *data);
static bool get_hostname(const char *ip_address, char *client_hostname);

static char* get_new_users_query(const char *server_version, bool include_root)
{
    const char* password = strstr(server_version, "5.7.") ? MYSQL57_PASSWORD : MYSQL_PASSWORD;
    const char *with_root = include_root ? "" : "WHERE u.user NOT IN ('root')";

    size_t n_bytes = snprintf(NULL, 0, NEW_LOAD_DBUSERS_QUERY, password, with_root, password, with_root);
    char *rval = MXS_MALLOC(n_bytes + 1);

    if (rval)
    {
        snprintf(rval, n_bytes + 1, NEW_LOAD_DBUSERS_QUERY, password, with_root, password, with_root);
    }

    return rval;
}

int replace_mysql_users(SERV_LISTENER *listener)
{
    spinlock_acquire(&listener->lock);
    int i = get_users(listener);
    spinlock_release(&listener->lock);
    return i;
}

static bool check_password(const char *output, uint8_t *token, size_t token_len,
                           uint8_t *scramble, size_t scramble_len, uint8_t *phase2_scramble)
{
    uint8_t stored_token[SHA_DIGEST_LENGTH] = {};
    size_t stored_token_len = sizeof(stored_token);

    if (*output)
    {
        /** Convert the hexadecimal string to binary */
        gw_hex2bin(stored_token, output, strlen(output));
    }

    /**
     * The client authentication token is made up of:
     *
     * XOR( SHA1(real_password), SHA1( CONCAT( scramble, <value of mysql.user.password> ) ) )
     *
     * Since we know the scramble and the value stored in mysql.user.password,
     * we can extract the SHA1 of the real password by doing a XOR of the client
     * authentication token with the SHA1 of the scramble concatenated with the
     * value of mysql.user.password.
     *
     * Once we have the SHA1 of the original password,  we can create the SHA1
     * of this hash and compare the value with the one stored in the backend
     * database. If the values match, the user has sent the right password.
     */

    /** First, calculate the SHA1 of the scramble and the hash stored in the database */
    uint8_t step1[SHA_DIGEST_LENGTH];
    gw_sha1_2_str(scramble, scramble_len, stored_token, stored_token_len, step1);

    /** Next, extract the SHA1 of the real password by XOR'ing it with
     * the output of the previous calculation */
    uint8_t step2[SHA_DIGEST_LENGTH];
    gw_str_xor(step2, token, step1, token_len);

    /** The phase 2 scramble needs to be copied to the shared data structure as it
     * is required when the backend authentication is done. */
    memcpy(phase2_scramble, step2, SHA_DIGEST_LENGTH);

    /** Finally, calculate the SHA1 of the hashed real password */
    uint8_t final_step[SHA_DIGEST_LENGTH];
    gw_sha1_str(step2, SHA_DIGEST_LENGTH, final_step);

    /** If the two values match, the client has sent the correct password */
    return memcmp(final_step, stored_token, stored_token_len) == 0;
}

/** Callback for check_database() */
static int database_cb(void *data, int columns, char** rows, char** row_names)
{
    bool *rval = (bool*)data;
    *rval = true;
    return 0;
}

static bool check_database(sqlite3 *handle, const char *database)
{
    bool rval = true;

    if (*database)
    {
        rval = false;
        size_t len = sizeof(mysqlauth_validate_database_query) + strlen(database) + 1;
        char sql[len];

        sprintf(sql, mysqlauth_validate_database_query, database);

        char *err;

        if (sqlite3_exec(handle, sql, database_cb, &rval, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute auth query: %s", err);
            sqlite3_free(err);
            rval = false;
        }
    }

    return rval;
}

static bool no_password_required(const char *result, size_t tok_len)
{
    return *result == '\0' && tok_len == 0;
}

/** Used to detect empty result sets */
struct user_query_result
{
    bool ok;
    char output[SHA_DIGEST_LENGTH * 2 + 1];
};

/** @brief Callback for sqlite3_exec() */
static int auth_cb(void *data, int columns, char** rows, char** row_names)
{
    struct user_query_result *res = (struct user_query_result*)data;
    strcpy(res->output, rows[0] ? rows[0] : "");
    res->ok = true;
    return 0;
}

int validate_mysql_user(sqlite3 *handle, DCB *dcb, MYSQL_session *session,
                        uint8_t *scramble, size_t scramble_len)
{
    size_t len = sizeof(mysqlauth_validate_user_query) + strlen(session->user) * 2 +
                 strlen(session->db) * 2 + MYSQL_HOST_MAXLEN + session->auth_token_len * 4 + 1;
    char sql[len + 1];
    int rval = MXS_AUTH_FAILED;
    char *err;

    sprintf(sql, mysqlauth_validate_user_query, session->user, dcb->remote,
            dcb->remote, session->db, session->db);

    struct user_query_result res = {};

    if (sqlite3_exec(handle, sql, auth_cb, &res, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to execute auth query: %s", err);
        sqlite3_free(err);
    }

    if (!res.ok)
    {
        /**
         * Try authentication with the hostname instead of the IP. We do this only
         * as a last resort so we avoid the high cost of the DNS lookup.
         */
        char client_hostname[MYSQL_HOST_MAXLEN];
        get_hostname(dcb->remote, client_hostname);
        sprintf(sql, mysqlauth_validate_user_query, session->user, client_hostname,
                client_hostname, session->db, session->db);

        if (sqlite3_exec(handle, sql, auth_cb, &res, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute auth query: %s", err);
            sqlite3_free(err);
        }
    }

    if (res.ok)
    {
        /** Found a matching row */

        if (no_password_required(res.output, session->auth_token_len) ||
            check_password(res.output, session->auth_token, session->auth_token_len,
                           scramble, scramble_len, session->client_sha1))
        {
            /** Password is OK, check that the database exists */
            if (check_database(handle, session->db))
            {
                rval = MXS_AUTH_SUCCEEDED;
            }
            else
            {
                rval = MXS_AUTH_FAILED_DB;
            }
        }
        else if (session->auth_token_len)
        {
            /** If authentication fails, this will trigger the right
             * error message with `Using password : YES` */
            session->client_sha1[0] = '_';
        }
    }

    return rval;
}

/**
 * @brief Delete all users
 *
 * @param handle SQLite handle
 */
static bool delete_mysql_users(sqlite3 *handle)
{
    bool rval = true;
    char *err;

    if (sqlite3_exec(handle, delete_users_query, NULL, NULL, &err) != SQLITE_OK ||
        sqlite3_exec(handle, delete_databases_query, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to delete old users: %s", err);
        sqlite3_free(err);
        rval = false;
    }

    return rval;
}

void add_mysql_user(sqlite3 *handle, const char *user, const char *host,
                    const char *db, bool anydb, const char *pw)
{
    size_t dblen = db && *db ? strlen(db) + 2 : sizeof(null_token); /** +2 for single quotes */
    char dbstr[dblen + 1];

    if (db && *db)
    {
        sprintf(dbstr, "'%s'", db);
    }
    else
    {
        strcpy(dbstr, null_token);
    }

    size_t pwlen = pw && *pw ? strlen(pw) + 2 : sizeof(null_token); /** +2 for single quotes */
    char pwstr[pwlen + 1];

    if (pw && *pw)
    {
        if (strlen(pw) == 16)
        {
            MXS_ERROR("The user %s@%s has on old password in the "
                      "backend database. MaxScale does not support these "
                      "old passwords. This user will not be able to connect "
                      "via MaxScale. Update the users password to correct "
                      "this.", user, host);
            return;
        }
        else if (*pw == '*')
        {
            pw++;
        }
        sprintf(pwstr, "'%s'", pw);
    }
    else
    {
        strcpy(pwstr, null_token);
    }

    size_t len = sizeof(insert_user_query) + strlen(user) + strlen(host) + dblen + pwlen + 1;

    char insert_sql[len + 1];
    sprintf(insert_sql, insert_user_query, user, host, dbstr, anydb ? "1" : "0", pwstr);

    char *err;
    if (sqlite3_exec(handle, insert_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to insert user: %s", err);
        sqlite3_free(err);
    }

    MXS_INFO("Added user: %s", insert_sql);
}

static void add_database(sqlite3 *handle, const char *db)
{
    size_t len = sizeof(insert_database_query) + strlen(db) + 1;
    char insert_sql[len + 1];

    sprintf(insert_sql, insert_database_query, db);

    char *err;
    if (sqlite3_exec(handle, insert_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to insert database: %s", err);
        sqlite3_free(err);
    }
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

    MXS_CONFIG* cnf = config_get_global_options();

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

    MXS_CONFIG* cnf = config_get_global_options();
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

    const char *template = "SELECT user, host, %s, Select_priv FROM mysql.user limit 1";
    const char* query_pw = strstr(server->server_string, "5.7.") ?
    MYSQL57_PASSWORD : MYSQL_PASSWORD;
    char query[strlen(template) + strlen(query_pw) + 1];
    bool rval = true;
    sprintf(query, template, query_pw);

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

    char *dpasswd = decrypt_password(password);
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
 * @brief Get client hostname
 *
 * Queries the DNS server for the client's hostname.
 *
 * @param ip_address      Client IP address
 * @param client_hostname Output buffer for hostname
 *
 * @return True if the hostname query was successful
 */
static bool get_hostname(const char *ip_address, char *client_hostname)
{
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
    int lookup_result = getnameinfo((struct sockaddr*)&bin_address,
                                    sizeof(struct sockaddr_in),
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
    }

    return false;
}

void start_sqlite_transaction(sqlite3 *handle)
{
    char *err;
    if (sqlite3_exec(handle, "BEGIN", NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to start transaction: %s", err);
        sqlite3_free(err);
    }
}

void commit_sqlite_transaction(sqlite3 *handle)
{
    char *err;
    if (sqlite3_exec(handle, "COMMIT", NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to commit transaction: %s", err);
        sqlite3_free(err);
    }
}

int get_users_from_server(MYSQL *con, SERVER_REF *server, SERVICE *service, SERV_LISTENER *listener)
{
    if (server->server->server_string == NULL)
    {
        const char *server_string = mysql_get_server_info(con);
        if (!server_set_version_string(server->server, server_string))
        {
            return -1;
        }
    }

    char *query = get_new_users_query(server->server->server_string, service->enable_root);
    MYSQL_AUTH *instance = (MYSQL_AUTH*)listener->auth_instance;
    bool anon_user = false;
    int users = 0;

    if (query)
    {
        if (mysql_query(con, query) == 0)
        {
            MYSQL_RES *result = mysql_store_result(con);

            if (result)
            {
                start_sqlite_transaction(instance->handle);

                /** Delete the old users */
                delete_mysql_users(instance->handle);
                MYSQL_ROW row;

                while ((row = mysql_fetch_row(result)))
                {
                    if (service->strip_db_esc)
                    {
                        strip_escape_chars(row[2]);
                    }

                    add_mysql_user(instance->handle, row[0], row[1], row[2],
                                   row[3] && strcmp(row[3], "Y") == 0, row[4]);
                    users++;

                    if (row[0] && *row[0] == '\0')
                    {
                        /** Empty username is used for the anonymous user. This means
                         that localhost does not match wildcard host. */
                        anon_user = true;
                    }
                }

                commit_sqlite_transaction(instance->handle);

                mysql_free_result(result);
            }
        }
        else
        {
            MXS_ERROR("Failed to load users: %s", mysql_error(con));
        }

        MXS_FREE(query);
    }

    /** Set the parameter if it is not configured by the user */
    if (service->localhost_match_wildcard_host == SERVICE_PARAM_UNINIT)
    {
        service->localhost_match_wildcard_host = anon_user ? 0 : 1;
    }

    /** Load the list of databases */
    if (mysql_query(con, "SHOW DATABASES") == 0)
    {
        MYSQL_RES *result = mysql_store_result(con);
        if (result)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)))
            {
                add_database(instance->handle, row[0]);
            }

            mysql_free_result(result);
        }
    }
    else
    {
        MXS_ERROR("Failed to load list of databases: %s", mysql_error(con));
    }

    return users;
}

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service   The current service
 * @param users     The users table into which to load the users
 * @return          -1 on any error or the number of users inserted
 */
static int get_users(SERV_LISTENER *listener)
{
    char *service_user = NULL;
    char *service_passwd = NULL;
    SERVICE *service = listener->service;

    if (serviceGetUser(service, &service_user, &service_passwd) == 0)
    {
        return -1;
    }

    char *dpwd = decrypt_password(service_passwd);

    if (dpwd == NULL)
    {
        return -1;
    }

    SERVER_REF *server = service->dbref;
    int total_users = -1;

    for (server = service->dbref; !service->svc_do_shutdown && server; server = server->next)
    {
        MYSQL *con = gw_mysql_init();
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
                int users = get_users_from_server(con, server, service, listener);

                if (users > total_users)
                {
                    total_users = users;
                }

                mysql_close(con);

                if (!service->users_from_all)
                {
                    break;
                }
            }
        }
    }

    MXS_FREE(dpwd);

    if (server == NULL)
    {
        MXS_ERROR("Unable to get user data from backend database for service [%s]."
                  " Failed to connect to any of the backend databases.", service->name);
    }

    return total_users;
}
