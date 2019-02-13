/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <string.h>
#include <maxscale/jansson.hh>
#include <maxscale/log.h>
#include <maxscale/secrets.h>
#include <maxscale/mysql_utils.h>

#define DEFAULT_PAM_DATABASE_NAME "file:pam.db?mode=memory&cache=shared"
#define DEFAULT_PAM_TABLE_NAME    "pam_users"
using std::string;

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamInstance* PamInstance::create(char** options)
{
    /** Name of the in-memory database */
    const string pam_db_name = DEFAULT_PAM_DATABASE_NAME;
    /** The table name where we store the users */
    const string pam_table_name = DEFAULT_PAM_TABLE_NAME;
    /** Deletion statement for the in-memory table */
    const string drop_sql = string("DROP TABLE IF EXISTS ") + pam_table_name + ";";
    /** CREATE TABLE statement for the in-memory table */
    const string create_sql = string("CREATE TABLE ") + pam_table_name
        + " (" + FIELD_USER + " varchar(255), "
        + FIELD_HOST + " varchar(255), "
        + FIELD_DB + " varchar(255), "
        + FIELD_ANYDB + " boolean, "
        + FIELD_AUTHSTR + " text, "
        + FIELD_PROXY + " boolean);";

    if (sqlite3_threadsafe() == 0)
    {
        MXS_WARNING("SQLite3 was compiled with thread safety off. May cause "
                    "corruption of in-memory database.");
    }

    bool error = false;
    /* This handle may be used from multiple threads, set full mutex. */
    sqlite3* dbhandle = NULL;
    int db_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
        | SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(pam_db_name.c_str(), &dbhandle, db_flags, NULL) != SQLITE_OK)
    {
        MXS_ERROR("Failed to open SQLite3 handle.");
        error = true;
    }

    char* err;
    if (!error && sqlite3_exec(dbhandle, drop_sql.c_str(), NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to drop table: '%s'", err);
        sqlite3_free(err);
        error = true;
    }
    if (!error && sqlite3_exec(dbhandle, create_sql.c_str(), NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to create table: '%s'", err);
        sqlite3_free(err);
        error = true;
    }

    PamInstance* instance = NULL;
    if (!error
        && ((instance = new(std::nothrow) PamInstance(dbhandle, pam_db_name, pam_table_name)) == NULL))
    {
        sqlite3_close_v2(dbhandle);
    }
    return instance;
}

/**
 * Constructor.
 *
 * @param dbhandle Database handle
 * @param dbname Text-form name of @c dbhandle
 * @param tablename Name of table where authentication data is saved
 */
PamInstance::PamInstance(sqlite3* dbhandle, const string& dbname, const string& tablename)
    : m_dbname(dbname)
    , m_tablename(tablename)
    , m_dbhandle(dbhandle)
{
}

/**
 * @brief Add new PAM user entry to the internal user database
 *
 * @param user   Username
 * @param host   Host
 * @param db     Database
 * @param anydb  Global access to databases
 * @param pam_service The PAM service used
 * @param proxy  Is the user anonymous with a proxy grant
 */
void PamInstance::add_pam_user(const char* user, const char* host, const char* db, bool anydb,
                               const char* pam_service, bool proxy)
{
    /**
     * The insert query template which adds users to the pam_users table.
     *
     * Note that 'db' and 'pam_service' are strings that can be NULL and thus they have
     * no quotes around them. The quotes for strings are added in this function.
     */
    const string insert_sql_template =
        "INSERT INTO " + m_tablename + " VALUES ('%s', '%s', %s, '%s', %s, '%s')";

    /** Used for NULL value creation in the INSERT query */
    const char NULL_TOKEN[] = "NULL";
    string db_str;

    if (db)
    {
        db_str = string("'") + db + "'";
    }
    else
    {
        db_str = NULL_TOKEN;
    }

    string service_str;
    if (pam_service && *pam_service)
    {
        service_str = string("'") + pam_service + "'";
    }
    else
    {
        service_str = NULL_TOKEN;
    }

    size_t len = insert_sql_template.length() + strlen(user) + strlen(host) + db_str.length()
        + service_str.length() + 1;

    char insert_sql[len + 1];
    sprintf(insert_sql,
            insert_sql_template.c_str(),
            user, host,
            db_str.c_str(), anydb ? "1" : "0",
            service_str.c_str(),
            proxy ? "1" : "0");

    char* err;
    if (sqlite3_exec(m_dbhandle, insert_sql, NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to insert user: %s", err);
        sqlite3_free(err);
    }
    else
    {
        if (proxy)
        {
            MXS_INFO("Added anonymous PAM user ''@'%s' with proxy grants using service %s.",
                     host, service_str.c_str());
        }
        else
        {
            MXS_INFO("Added normal PAM user '%s'@'%s' using service %s.", user, host, service_str.c_str());
        }
    }
}

/**
 * @brief Delete old users from the database
 */
void PamInstance::delete_old_users()
{
    /** Delete query used to clean up the database before loading new users */
    const string delete_query = "DELETE FROM " + m_tablename;
    char* err;
    if (sqlite3_exec(m_dbhandle, delete_query.c_str(), NULL, NULL, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to delete old users: %s", err);
        sqlite3_free(err);
    }
}

/**
 * @brief Populates the internal user database by reading from one of the backend servers
 *
 * @param service The service the users should be read from
 *
 * @return MXS_AUTH_LOADUSERS_OK on success, MXS_AUTH_LOADUSERS_ERROR on error
 */
int PamInstance::load_users(SERVICE* service)
{
    /** Query that gets all users that authenticate via the pam plugin */
    const char PAM_USERS_QUERY[] =
        "SELECT u.user, u.host, d.db, u.select_priv, u.authentication_string FROM "
        "mysql.user AS u LEFT JOIN mysql.db AS d ON (u.user = d.user AND u.host = d.host) WHERE "
        "(u.plugin = 'pam' AND (d.db IS NOT NULL OR u.select_priv = 'Y')) "
        "UNION "
        "SELECT u.user, u.host, t.db, u.select_priv, u.authentication_string FROM "
        "mysql.user AS u LEFT JOIN mysql.tables_priv AS t ON (u.user = t.user AND u.host = t.host) WHERE "
        "(u.plugin = 'pam' AND t.db IS NOT NULL AND u.select_priv = 'N') "
        "ORDER BY user";
#if defined (SS_DEBUG)
    const unsigned int PAM_USERS_QUERY_NUM_FIELDS = 5;
#endif

    const char* user;
    const char* password;
    serviceGetUser(service, &user, &password);
    int rval = MXS_AUTH_LOADUSERS_ERROR;
    char* pw;

    if ((pw = decrypt_password(password)))
    {
        for (SERVER_REF* servers = service->dbref; servers; servers = servers->next)
        {
            MYSQL* mysql = mysql_init(NULL);
            if (mxs_mysql_real_connect(mysql, servers->server, user, pw))
            {
                if (mysql_query(mysql, PAM_USERS_QUERY))
                {
                    MXS_ERROR("Failed to query server '%s' for PAM users: '%s'.",
                              servers->server->name, mysql_error(mysql));
                }
                else
                {
                    MYSQL_RES* res = mysql_store_result(mysql);
                    delete_old_users();
                    if (res)
                    {
                        mxb_assert(mysql_num_fields(res) == PAM_USERS_QUERY_NUM_FIELDS);
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res)))
                        {
                            add_pam_user(row[0], row[1], // user, host
                                         row[2], row[3] && strcasecmp(row[3], "Y") == 0, // db, anydb
                                         row[4], // pam service
                                         false); // not a proxy
                        }
                        mysql_free_result(res);
                    }

                    if (fetch_anon_proxy_users(servers->server, mysql))
                    {
                        rval = MXS_AUTH_LOADUSERS_OK;
                    }
                }
                mysql_close(mysql);

                if (rval == MXS_AUTH_LOADUSERS_OK)
                {
                    break;
                }
            }
        }
        MXS_FREE(pw);
    }
    return rval;
}

void PamInstance::diagnostic(DCB* dcb)
{
    json_t* array = diagnostic_json();
    mxb_assert(json_is_array(array));

    string result, separator;
    size_t index;
    json_t* value;
    json_array_foreach(array, index, value)
    {
        // Only print user@host for the non-json version, as this should fit nicely on the console. Add the
        // other fields if deemed useful.
        const char* user = json_string_value(json_object_get(value, FIELD_USER.c_str()));
        const char* host = json_string_value(json_object_get(value, FIELD_HOST.c_str()));
        if (user && host)
        {
            result += separator + user + "@" + host;
            separator = " ";
        }
    }

    if (!result.empty())
    {
        dcb_printf(dcb, "%s", result.c_str());
    }
    json_decref(array);
}

static int diag_cb_json(void* data, int columns, char** row, char** field_names)
{
    mxb_assert(columns == NUM_FIELDS);
    json_t* obj = json_object();
    for (int i = 0; i < columns; i++)
    {
        json_object_set_new(obj, field_names[i], json_string(row[i]));
    }
    json_t* arr = static_cast<json_t*>(data);
    json_array_append_new(arr, obj);
    return 0;
}

json_t* PamInstance::diagnostic_json()
{
    json_t* rval = json_array();
    char* err;
    string select = "SELECT * FROM " + m_tablename + ";";
    if (sqlite3_exec(m_dbhandle, select.c_str(), diag_cb_json, rval, &err) != SQLITE_OK)
    {
        MXS_ERROR("Failed to print users: %s", err);
        sqlite3_free(err);
    }

    return rval;
}

bool PamInstance::fetch_anon_proxy_users(SERVER* server, MYSQL* conn)
{
    bool success = true;
    const char ANON_USER_QUERY[] = "SELECT host,authentication_string FROM mysql.user WHERE "
                                   "(plugin = 'pam' AND user = '');";

    const char GRANT_PROXY[] = "GRANT PROXY ON";

    // Query for the anonymous user which is used with group mappings
    if (mysql_query(conn, ANON_USER_QUERY))
    {
        MXS_ERROR("Failed to query server '%s' for anonymous PAM users: '%s'.",
                  server->name, mysql_error(conn));
        success = false;
    }
    else
    {
        // Temporary storage of host,authentication_string for anonymous pam users.
        std::vector<std::pair<string, string>> anon_users_info;
        MYSQL_RES* res = mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)))
            {
                string host = row[0] ? row[0] : "";
                string auth_str = row[1] ? row[1] : "";
                anon_users_info.push_back(std::make_pair(host, auth_str));
            }
            mysql_free_result(res);
        }

        if (!anon_users_info.empty())
        {
             MXS_INFO("Found %lu anonymous PAM user(s). Checking them for proxy grants.",
                      anon_users_info.size());
        }

        for (const auto& elem : anon_users_info)
        {
            string query =  "SHOW GRANTS FOR ''@'" + elem.first + "';";
            // Check that the anon user has a proxy grant.
            if (mysql_query(conn, query.c_str()))
            {
                MXS_ERROR("Failed to query server '%s' for grants of anonymous PAM user ''@'%s': '%s'.",
                          server->name, elem.first.c_str(), mysql_error(conn));
                success = false;
            }
            else
            {
                if ((res = mysql_store_result(conn)))
                {
                    // The user may have multiple proxy grants, but is only added once.
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(res)))
                    {
                        if (row[0] && strncmp(row[0], GRANT_PROXY, sizeof(GRANT_PROXY) - 1) == 0)
                        {
                            add_pam_user("", elem.first.c_str(), // user, host
                                         NULL, false, // Unused
                                         elem.second.c_str(), true); // service, proxy
                            break;
                        }
                    }
                    mysql_free_result(res);
                }
            }
        }
    }

    return success;
}
