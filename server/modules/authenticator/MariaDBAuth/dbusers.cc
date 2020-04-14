/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Loading MySQL users from a MySQL backend server
 */

#include "mysql_auth.hh"

#include <stdio.h>

#include <mysql.h>
#include <mysqld_error.h>
#include <maxbase/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/router.hh>
#include <maxscale/secrets.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxbase/format.hh>

namespace
{
using AuthRes = mariadb::ClientAuthenticator::AuthRes;
using mariadb::UserEntry;
}

// Query used with 10.0 or older
const char* mariadb_users_query_format =
    "SELECT u.user, u.host, d.db, u.select_priv, u.%s "
    "FROM mysql.user AS u LEFT JOIN mysql.db AS d "
    "ON (u.user = d.user AND u.host = d.host) WHERE u.plugin IN ('', 'mysql_native_password') %s "
    "UNION "
    "SELECT u.user, u.host, t.db, u.select_priv, u.%s "
    "FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
    "ON (u.user = t.user AND u.host = t.host) WHERE u.plugin IN ('', 'mysql_native_password') %s";

const char* clustrix_users_query_format =
    "SELECT u.username AS user, u.host, a.dbname AS db, "
    "       IF(a.privileges & 1048576, 'Y', 'N') AS select_priv, u.password "
    "FROM system.users AS u LEFT JOIN system.user_acl AS a ON (u.user = a.role) "
    "WHERE u.plugin IN ('', 'mysql_native_password') %s";

// Used with 10.2 or newer, supports composite roles
const char* mariadb_102_users_query =
    // `t` is users that are not roles
    "WITH RECURSIVE t AS ( "
    "  SELECT u.user, u.host, d.db, u.select_priv, "
    "         IF(u.password <> '', u.password, u.authentication_string) AS password, "
    "         u.is_role, u.default_role"
    "  FROM mysql.user AS u LEFT JOIN mysql.db AS d "
    "  ON (u.user = d.user AND u.host = d.host) "
    "  WHERE u.plugin IN ('', 'mysql_native_password') "
    "  UNION "
    "  SELECT u.user, u.host, t.db, u.select_priv, "
    "         IF(u.password <> '', u.password, u.authentication_string), "
    "         u.is_role, u.default_role "
    "  FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
    "  ON (u.user = t.user AND u.host = t.host)"
    "  WHERE u.plugin IN ('', 'mysql_native_password') "
    "), users AS ("
    // Select the root row, the actual user
    "  SELECT t.user, t.host, t.db, t.select_priv, t.password, t.default_role AS role FROM t"
    "  WHERE t.is_role = 'N'"
    "  UNION"
    // Recursively select all roles for the users
    "  SELECT u.user, u.host, t.db, t.select_priv, u.password, r.role FROM t"
    "  JOIN users AS u"
    "  ON (t.user = u.role)"
    "  LEFT JOIN mysql.roles_mapping AS r"
    "  ON (t.user = r.user)"
    "  WHERE t.is_role = 'Y'"
    ")"
    "SELECT DISTINCT t.user, t.host, t.db, t.select_priv, t.password FROM users AS t %s";

// Query used with MariaDB 10.1, supports basic roles
const char* mariadb_101_users_query
    =   // First, select all users
        "SELECT t.user, t.host, t.db, t.select_priv, t.password FROM "
        "( "
        "    SELECT u.user, u.host, d.db, u.select_priv, u.password AS password, u.is_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.db AS d "
        "    ON (u.user = d.user AND u.host = d.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        "    UNION "
        "    SELECT u.user, u.host, t.db, u.select_priv, u.password AS password, u.is_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
        "    ON (u.user = t.user AND u.host = t.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        ") AS t "
        // Discard any users that are roles
        "WHERE t.is_role <> 'Y' %s "
        "UNION "
        // Then select all users again
        "SELECT r.user, r.host, u.db, u.select_priv, t.password FROM "
        "( "
        "    SELECT u.user, u.host, d.db, u.select_priv, u.password AS password, u.default_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.db AS d "
        "    ON (u.user = d.user AND u.host = d.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        "    UNION "
        "    SELECT u.user, u.host, t.db, u.select_priv, u.password AS password, u.default_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
        "    ON (u.user = t.user AND u.host = t.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        ") AS t "
        // Join it to the roles_mapping table to only have users with roles
        "JOIN mysql.roles_mapping AS r "
        "ON (r.user = t.user AND r.host = t.host) "
        // Then join it into itself to get the privileges of the role with the name of the user
        "JOIN "
        "( "
        "    SELECT u.user, u.host, d.db, u.select_priv, u.password AS password, u.is_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.db AS d "
        "    ON (u.user = d.user AND u.host = d.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        "    UNION "
        "    SELECT u.user, u.host, t.db, u.select_priv, u.password AS password, u.is_role "
        "    FROM mysql.user AS u LEFT JOIN mysql.tables_priv AS t "
        "    ON (u.user = t.user AND u.host = t.host) "
        "    WHERE u.plugin IN ('', 'mysql_native_password') "
        ") AS u "
        "ON (u.user = r.role AND u.is_role = 'Y') "
        // We only care about users that have a default role assigned
        "WHERE t.default_role = u.user %s;";

enum server_category_t
{
    SERVER_NO_ROLES,
    SERVER_ROLES,
    SERVER_CLUSTRIX
};

static char* get_mariadb_102_users_query(bool include_root)
{
    const char* with_root = include_root ? "" : " WHERE t.user <> 'root'";

    size_t n_bytes = snprintf(NULL, 0, mariadb_102_users_query, with_root);
    char* rval = static_cast<char*>(MXS_MALLOC(n_bytes + 1));
    MXS_ABORT_IF_NULL(rval);
    snprintf(rval, n_bytes + 1, mariadb_102_users_query, with_root);

    return rval;
}

static char* get_mariadb_101_users_query(bool include_root)
{
    const char* with_root = include_root ? "" : " AND t.user NOT IN ('root')";

    size_t n_bytes = snprintf(NULL, 0, mariadb_101_users_query, with_root, with_root);
    char* rval = static_cast<char*>(MXS_MALLOC(n_bytes + 1));
    MXS_ABORT_IF_NULL(rval);
    snprintf(rval, n_bytes + 1, mariadb_101_users_query, with_root, with_root);

    return rval;
}

/**
 * Return the column name of the password hash in the mysql.user table.
 *
 * @param version Server version
 * @return Column name
 */
static const char* get_password_column_name(const SERVER::Version& version)
{
    const char* rval = "password";      // Usual result, used in MariaDB.
    auto major = version.major;
    auto minor = version.minor;
    if ((major == 5 && minor == 7) || (major == 8 && minor == 0))
    {
        rval = "authentication_string";
    }
    return rval;
}

static char* get_mariadb_users_query(bool include_root, const SERVER::Version& version)
{
    const char* password = get_password_column_name(version);
    const char* with_root = include_root ? "" : " AND u.user NOT IN ('root')";

    size_t n_bytes = snprintf(NULL, 0, mariadb_users_query_format, password, with_root, password, with_root);
    char* rval = static_cast<char*>(MXS_MALLOC(n_bytes + 1));
    MXS_ABORT_IF_NULL(rval);
    snprintf(rval, n_bytes + 1, mariadb_users_query_format, password, with_root, password, with_root);

    return rval;
}

static char* get_clustrix_users_query(bool include_root)
{
    const char* with_root;

    if (include_root)
    {
        with_root =
            "UNION ALL "
            "SELECT 'root' AS user, '127.0.0.1', '*' AS db, 'Y' AS select_priv, '' AS password";
    }
    else
    {
        with_root = "AND u.username <> 'root'";
    }

    size_t n_bytes = snprintf(NULL, 0, clustrix_users_query_format, with_root);
    char* rval = static_cast<char*>(MXS_MALLOC(n_bytes + 1));
    MXS_ABORT_IF_NULL(rval);
    snprintf(rval, n_bytes + 1, clustrix_users_query_format, with_root);

    return rval;
}

/**
 * Check if auth token sent by client matches the one in the user account entry.
 *
 * @param session Client session with auth token
 * @param stored_pw_hash2 SHA1(SHA1(password)) in hex form, as queried from server.
 * @return Authentication result
 */
AuthRes MariaDBClientAuthenticator::check_password(MYSQL_session* session, const std::string& stored_pw_hash2)
{

    const auto& auth_token = session->auth_token;   // Hex-form token sent by client.

    bool empty_token = auth_token.empty();
    bool empty_pw = stored_pw_hash2.empty();
    if (empty_token || empty_pw)
    {
        AuthRes rval;
        if (empty_token && empty_pw)
        {
            // If the user entry has empty password and the client gave no password, accept.
            rval.status = AuthRes::Status::SUCCESS;
        }
        else if (m_log_pw_mismatch)
        {
            // Save reason of failure.
            rval.msg = empty_token ? "Client gave no password when one was expected" :
                "Client gave a password when none was expected";
        }
        return rval;
    }

    uint8_t stored_pw_hash2_bin[SHA_DIGEST_LENGTH] = {};
    size_t stored_hash_len = sizeof(stored_pw_hash2_bin);

    // Convert the hexadecimal string to binary.
    mxs::hex2bin(stored_pw_hash2.c_str(), stored_pw_hash2.length(), stored_pw_hash2_bin);

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

    // First, calculate the SHA1(scramble + stored pw hash).
    uint8_t step1[SHA_DIGEST_LENGTH];
    gw_sha1_2_str(session->scramble, sizeof(session->scramble), stored_pw_hash2_bin, stored_hash_len, step1);

    // Next, extract SHA1(password) by XOR'ing the auth token sent by client with the previous step result.
    uint8_t step2[SHA_DIGEST_LENGTH] = {};
    mxs::bin_bin_xor(auth_token.data(), step1, auth_token.size(), step2);

    // SHA1(password) needs to be copied to the shared data structure as it is required during
    // backend authentication. */
    session->auth_token_phase2.assign(step2, step2 + SHA_DIGEST_LENGTH);

    // Finally, calculate the SHA1(SHA1(password). */
    uint8_t final_step[SHA_DIGEST_LENGTH];
    gw_sha1_str(step2, SHA_DIGEST_LENGTH, final_step);

    // If the two values match, the client has sent the correct password.
    bool match = (memcmp(final_step, stored_pw_hash2_bin, stored_hash_len) == 0);
    AuthRes rval;
    rval.status = match ? AuthRes::Status::SUCCESS : AuthRes::Status::FAIL_WRONG_PW;
    if (!match && m_log_pw_mismatch)
    {
        // Convert the SHA1(SHA1(password)) from client to hex before printing.
        char received_pw[2 * SHA_DIGEST_LENGTH + 1];
        mxs::bin2hex(final_step, SHA_DIGEST_LENGTH, received_pw);
        rval.msg = mxb::string_printf("Client gave wrong password. Got hash %s, expected %s",
                                      received_pw, stored_pw_hash2.c_str());
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
static void merge_netmask(char* host)
{
    char* delimiter_loc = strchr(host, '/');
    if (delimiter_loc == NULL)
    {
        return;     // Nothing to do
    }
    /* If anything goes wrong, we put the '/' back in to ensure the hostname
     * cannot be used.
     */
    *delimiter_loc = '\0';

    char* ip_token_loc = host;
    char* mask_token_loc = delimiter_loc + 1;   // This is at minimum a \0

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
                      "Merge incomplete: %s",
                      host);
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
                  "Merge incomplete: %s",
                  host);
    }
}

/**
 * @brief Check permissions for a particular table.
 *
 * @param mysql         A valid MySQL connection.
 * @param service       The service in question.
 * @param user          The user in question.
 * @param table         The table whose permissions are checked.
 * @param query         The query using which the table permissions are checked.
 * @param log_priority  The priority using which a possible ER_TABLE_ACCESS_DENIED_ERROR
 *                      should be logged.
 * @param message       Additional log message.
 *
 * @return True if the table could accessed or if the priority is less than LOG_ERR,
 *         false otherwise.
 */
static bool check_table_permissions(MYSQL* mysql,
                                    SERVICE* service,
                                    const char* user,
                                    const char* table,
                                    const char* query,
                                    int log_priority,
                                    const char* message = nullptr)
{
    bool rval = true;

    if (mxs_mysql_query(mysql, query) != 0)
    {
        if (mysql_errno(mysql) == ER_TABLEACCESS_DENIED_ERROR)
        {
            if (log_priority >= LOG_ERR)
            {
                rval = false;
            }

            MXS_LOG_MESSAGE(log_priority,
                            "[%s] User '%s' is missing SELECT privileges "
                            "on %s table.%sMySQL error message: %s",
                            service->name(),
                            user,
                            table,
                            message ? message : " ",
                            mysql_error(mysql));
        }
        else
        {
            MXS_ERROR("[%s] Failed to query from %s table."
                      " MySQL error message: %s",
                      service->name(),
                      table,
                      mysql_error(mysql));
        }
    }
    else
    {

        MYSQL_RES* res = mysql_use_result(mysql);
        if (res == NULL)
        {
            MXS_ERROR("[%s] Result retrieval failed when checking for permissions to "
                      "the %s table: %s",
                      service->name(),
                      table,
                      mysql_error(mysql));
        }
        else
        {
            mysql_free_result(res);
        }
    }

    return rval;
}

/**
 * @brief Check table permissions on MySQL/MariaDB server
 *
 * @return True if the table permissions are OK, false otherwise.
 */
static bool check_default_table_permissions(MYSQL* mysql,
                                            SERVICE* service,
                                            SERVER* server,
                                            const char* user)
{
    bool rval = true;

    const char* format = "SELECT user, host, %s, Select_priv FROM mysql.user limit 1";
    const char* query_pw = get_password_column_name(server->version());

    char query[strlen(format) + strlen(query_pw) + 1];
    sprintf(query, format, query_pw);

    rval = check_table_permissions(mysql, service, user, "mysql.user", query, LOG_ERR);

    check_table_permissions(mysql, service, user,
                            "mysql.db",
                            "SELECT user, host, db FROM mysql.db limit 1",
                            LOG_WARNING,
                            "Database name will be ignored in authentication. ");

    check_table_permissions(mysql, service, user,
                            "mysql.tables_priv",
                            "SELECT user, host, db FROM mysql.tables_priv limit 1",
                            LOG_WARNING,
                            "Database name will be ignored in authentication. ");

    // Check whether the current user has the SHOW DATABASES privilege
    if (mxs_mysql_query(mysql, "SHOW GRANTS") == 0)
    {
        if (MYSQL_RES* res = mysql_use_result(mysql))
        {
            bool found = false;

            for (MYSQL_ROW row = mysql_fetch_row(res); row; row = mysql_fetch_row(res))
            {
                if (strcasestr(row[0], "SHOW DATABASES") || strcasestr(row[0], "ALL PRIVILEGES ON *.*"))
                {
                    // GRANT ALL PRIVILEGES ON *.* will overwrite SHOW DATABASES so it needs to be checked
                    // separately
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                MXS_WARNING("[%s] User '%s' is missing the SHOW DATABASES privilege. "
                            "This means that MaxScale cannot see all databases and authentication can fail.",
                            service->name(),
                            user);
            }
            mysql_free_result(res);
        }
    }

    return rval;
}

/**
 * @brief Check table permissions on a Clustrix server
 *
 * @return True if the table permissions are OK, false otherwise.
 */
static bool check_clustrix_table_permissions(MYSQL* mysql,
                                             SERVICE* service,
                                             SERVER* server,
                                             const char* user)
{
    bool rval = true;

    if (!check_table_permissions(mysql, service, user,
                                 "system.users",
                                 "SELECT username, host, password FROM system.users LIMIT 1",
                                 LOG_ERR))
    {
        rval = false;
    }

    if (!check_table_permissions(mysql, service, user,
                                 "system.user_acl",
                                 "SELECT privileges, role FROM system.user_acl LIMIT 1",
                                 LOG_ERR))
    {
        rval = false;
    }

    // TODO: SHOW DATABASES privilege is not checked.

    return rval;
}

// Contains loaded user definitions, only used temporarily
struct User
{
    std::string user;
    std::string host;
    std::string db;
    bool        anydb;
    std::string pw;
};
