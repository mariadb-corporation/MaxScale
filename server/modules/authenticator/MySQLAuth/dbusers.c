/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Loading MySQL users from a MySQL backend server
 */

#include "mysql_auth.h"

#include <ctype.h>
#include <netdb.h>
#include <stdio.h>

#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/paths.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/router.h>
#include <maxscale/secrets.h>
#include <maxscale/service.h>
#include <maxscale/users.h>
#include <maxscale/utils.h>

/** Don't include the root user */
#define USERS_QUERY_NO_ROOT " AND user.user NOT IN ('root')"

/** Normal password column name */
#define MYSQL_PASSWORD "password"

/** MySQL 5.7 password column name */
#define MYSQL57_PASSWORD "authentication_string"

#define NEW_LOAD_DBUSERS_QUERY "SELECT u.user, u.host, d.db, u.select_priv, u.%s \
    FROM mysql.user AS u LEFT JOIN mysql.db AS d \
    ON (u.user = d.user AND u.host = d.host) WHERE u.plugin = '' %s \
    UNION \
    SELECT u.user, u.host, t.db, u.select_priv, u.%s \
    FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t \
    ON (u.user = t.user AND u.host = t.host) WHERE u.plugin = '' %s"

static int get_users(SERV_LISTENER *listener, bool skip_local);
static MYSQL *gw_mysql_init(void);
static int gw_mysql_set_timeouts(MYSQL* handle);
static char *mysql_format_user_entry(void *data);
static bool get_hostname(DCB *dcb, char *client_hostname, size_t size);

static char* get_new_users_query(const char *server_version, bool include_root)
{
    const char* password = strstr(server_version, "5.7.") ? MYSQL57_PASSWORD : MYSQL_PASSWORD;
    const char *with_root = include_root ? "" : " AND u.user NOT IN ('root')";

    size_t n_bytes = snprintf(NULL, 0, NEW_LOAD_DBUSERS_QUERY, password, with_root, password, with_root);
    char *rval = MXS_MALLOC(n_bytes + 1);

    if (rval)
    {
        snprintf(rval, n_bytes + 1, NEW_LOAD_DBUSERS_QUERY, password, with_root, password, with_root);
    }

    return rval;
}

int replace_mysql_users(SERV_LISTENER *listener, bool skip_local)
{
    int i = get_users(listener, skip_local);
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

int validate_mysql_user(MYSQL_AUTH* instance, DCB *dcb, MYSQL_session *session,
                        uint8_t *scramble, size_t scramble_len)
{
    sqlite3 *handle = get_handle(instance);
    const char* validate_query = instance->lower_case_table_names ?
        mysqlauth_validate_user_query_lower :
        mysqlauth_validate_user_query;
    size_t len = strlen(validate_query) + 1 + strlen(session->user) * 2 +
                 strlen(session->db) * 2 + MYSQL_HOST_MAXLEN + session->auth_token_len * 4 + 1;
    char sql[len + 1];
    int rval = MXS_AUTH_FAILED;
    char *err;

    if (instance->skip_auth)
    {
        sprintf(sql, mysqlauth_skip_auth_query, session->user, session->db, session->db);
    }
    else
    {
        sprintf(sql, validate_query, session->user, dcb->remote,
                dcb->remote, session->db, session->db);
    }

    struct user_query_result res = {};

    if (sqlite3_exec(handle, sql, auth_cb, &res, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to execute auth query: %s", err);
        sqlite3_free(err);
    }

    /** Check for IPv6 mapped IPv4 address */
    if (!res.ok && strchr(dcb->remote, ':') && strchr(dcb->remote, '.'))
    {
        const char *ipv4 = strrchr(dcb->remote, ':') + 1;
        sprintf(sql, validate_query, session->user, ipv4, ipv4,
                session->db, session->db);

        if (sqlite3_exec(handle, sql, auth_cb, &res, &err) != SQLITE_OK)
        {
            MXS_ERROR("Failed to execute auth query: %s", err);
            sqlite3_free(err);
        }
    }

    if (!res.ok)
    {
        /**
         * Try authentication with the hostname instead of the IP. We do this only
         * as a last resort so we avoid the high cost of the DNS lookup.
         */
        char client_hostname[MYSQL_HOST_MAXLEN] = "";
        get_hostname(dcb, client_hostname, sizeof(client_hostname) - 1);

        sprintf(sql, validate_query, session->user, client_hostname,
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
        if (gw_mysql_set_timeouts(con) != 0)
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

    if ((rc = mysql_optionsv(handle, MYSQL_OPT_READ_TIMEOUT,
                             (void *) &cnf->auth_read_timeout)))
    {
        MXS_ERROR("Failed to set read timeout for backend connection.");
        goto retblock;
    }

    if ((rc = mysql_optionsv(handle, MYSQL_OPT_CONNECT_TIMEOUT,
                             (void *) &cnf->auth_conn_timeout)))
    {
        MXS_ERROR("Failed to set connect timeout for backend connection.");
        goto retblock;
    }

    if ((rc = mysql_optionsv(handle, MYSQL_OPT_WRITE_TIMEOUT,
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
    mysql_optionsv(mysql, MYSQL_OPT_READ_TIMEOUT, &cnf->auth_read_timeout);
    mysql_optionsv(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &cnf->auth_conn_timeout);
    mysql_optionsv(mysql, MYSQL_OPT_WRITE_TIMEOUT, &cnf->auth_write_timeout);
    mysql_optionsv(mysql, MYSQL_PLUGIN_DIR, get_connector_plugindir());

    if (mxs_mysql_real_connect(mysql, server, user, password) == NULL)
    {
        int my_errno = mysql_errno(mysql);

        MXS_ERROR("[%s] Failed to connect to server '%s' ([%s]:%d) when"
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

    if (server->version_string[0] == 0)
    {
        mxs_mysql_set_server_version(mysql, server);
    }

    const char *template = "SELECT user, host, %s, Select_priv FROM mysql.user limit 1";
    const char* query_pw = strstr(server->version_string, "5.7.") ?
                           MYSQL57_PASSWORD : MYSQL_PASSWORD;
    char query[strlen(template) + strlen(query_pw) + 1];
    bool rval = true;
    sprintf(query, template, query_pw);

    if (mxs_mysql_query(mysql, query) != 0)
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

    if (mxs_mysql_query(mysql, "SELECT user, host, db FROM mysql.db limit 1") != 0)
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

    if (mxs_mysql_query(mysql, "SELECT user, host, db FROM mysql.tables_priv limit 1") != 0)
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
    if (rcap_type_required(service_get_capabilities(service), RCAP_TYPE_NO_AUTH) ||
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
        if (server_is_mxs_service(server->server) ||
            check_server_permissions(service, server->server, user, dpasswd))
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
static bool get_hostname(DCB *dcb, char *client_hostname, size_t size)
{
    struct addrinfo *ai = NULL, hint = {};
    hint.ai_flags = AI_ALL;
    int rc;

    if ((rc = getaddrinfo(dcb->remote, NULL, &hint, &ai)) != 0)
    {
        MXS_ERROR("Failed to obtain address for host %s, %s",
                  dcb->remote, gai_strerror(rc));
        return false;
    }

    /* Try to lookup the domain name of the given IP-address. This is a slow
     * i/o-operation, which will stall the entire thread. TODO: cache results
     * if this feature is used often. */
    int lookup_result = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                                    client_hostname, size,
                                    NULL, 0, // No need for the port
                                    NI_NAMEREQD); // Text address only
    freeaddrinfo(ai);

    if (lookup_result != 0 && lookup_result != EAI_NONAME)
    {
        MXS_ERROR("Client hostname lookup failed for '%s', getnameinfo() returned: '%s'.",
                  dcb->remote, gai_strerror(lookup_result));
    }

    return lookup_result == 0;
}

int get_users_from_server(MYSQL *con, SERVER_REF *server_ref, SERVICE *service, SERV_LISTENER *listener)
{
    if (server_ref->server->version_string[0] == 0)
    {
        mxs_mysql_set_server_version(con, server_ref->server);
    }

    char *query = get_new_users_query(server_ref->server->version_string, service->enable_root);
    MYSQL_AUTH *instance = (MYSQL_AUTH*)listener->auth_instance;
    sqlite3* handle = get_handle(instance);
    bool anon_user = false;
    int users = 0;

    if (query)
    {
        if (mxs_mysql_query(con, query) == 0)
        {
            MYSQL_RES *result = mysql_store_result(con);

            if (result)
            {
                MYSQL_ROW row;

                while ((row = mysql_fetch_row(result)))
                {
                    if (service->strip_db_esc)
                    {
                        strip_escape_chars(row[2]);
                    }

                    if (strchr(row[1], '/'))
                    {
                        merge_netmask(row[1]);
                    }

                    add_mysql_user(handle, row[0], row[1], row[2],
                                   row[3] && strcmp(row[3], "Y") == 0, row[4]);
                    users++;

                    if (row[0] && *row[0] == '\0')
                    {
                        /** Empty username is used for the anonymous user. This means
                         that localhost does not match wildcard host. */
                        anon_user = true;
                    }
                }

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
    if (mxs_mysql_query(con, "SHOW DATABASES") == 0)
    {
        MYSQL_RES *result = mysql_store_result(con);
        if (result)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)))
            {
                add_database(handle, row[0]);
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
static int get_users(SERV_LISTENER *listener, bool skip_local)
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

    /** Delete the old users */
    MYSQL_AUTH *instance = (MYSQL_AUTH*)listener->auth_instance;
    sqlite3* handle = get_handle(instance);
    delete_mysql_users(handle);

    SERVER_REF *server = service->dbref;
    int total_users = -1;
    bool no_active_servers = true;

    for (server = service->dbref; !service->svc_do_shutdown && server; server = server->next)
    {
        if (!SERVER_REF_IS_ACTIVE(server) || !SERVER_IS_ACTIVE(server->server) ||
            (skip_local && server_is_mxs_service(server->server)))
        {
            continue;
        }

        no_active_servers = false;

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

    if (no_active_servers)
    {
        // This service has no servers or all servers are local MaxScale services
        total_users = 0;
    }
    else if (server == NULL && total_users == -1)
    {
        MXS_ERROR("Unable to get user data from backend database for service [%s]."
                  " Failed to connect to any of the backend databases.", service->name);
    }

    return total_users;
}
