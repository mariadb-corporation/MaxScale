/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pam_instance.hh"

#include <string>
#include <string.h>
#include <maxbase/format.hh>
#include <maxscale/jansson.hh>
#include <maxscale/paths.h>
#include <maxscale/secrets.h>
#include <maxscale/mysql_utils.hh>

using std::string;
using mxq::QueryResult;

/**
 * Create an instance.
 *
 * @param options Listener options
 * @return New client authenticator instance or NULL on error
 */
PamInstance* PamInstance::create(char** options)
{
    // Name of the in-memory database.
    // TODO: Once Centos6 is no longer needed and Sqlite version 3.7+ can be assumed,
    // use a memory-only db with a URI filename (e.g. file:pam.db?mode=memory&cache=shared)
    const string pam_db_fname = string(get_cachedir()) + "/pam_db.sqlite3";

    if (sqlite3_threadsafe() == 0)
    {
        MXB_WARNING("SQLite3 was compiled with thread safety off. May cause corruption of "
                    "in-memory database.");
    }

    /* This handle may be used from multiple threads, set full mutex. */
    int db_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                   SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_FULLMUTEX;
    string sqlite_error;
    PamInstance* instance = nullptr;
    auto sqlite = SQLite::create(pam_db_fname, db_flags, &sqlite_error);
    if (sqlite)
    {
        instance = new PamInstance(std::move(sqlite), pam_db_fname);
        if (!instance->prepare_tables())
        {
            delete instance;
            instance = nullptr;
        }
    }
    else
    {
        MXB_ERROR("Could not create PAM authenticator: %s", sqlite_error.c_str());
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
PamInstance::PamInstance(SQLite::SSQLite dbhandle, const string& dbname)
    : m_dbname(dbname)
    , m_sqlite(std::move(dbhandle))
{
}

bool PamInstance::prepare_tables()
{
    struct ColDef
    {
        enum class ColType {
            BOOL,
            TEXT,
        };
        string name;
        ColType type;
    };
    using ColDefArray = std::vector<ColDef>;
    using Type = ColDef::ColType;

    /** Deletion statement for the in-memory table */
    auto gen_drop_sql = [](const string& tblname) {
        return "DROP TABLE IF EXISTS " + tblname + ";";
    };

    /** CREATE TABLE statement for the in-memory table */
    auto gen_create_sql = [](const string& tblname, const ColDefArray& coldefs) {
        string rval = "CREATE TABLE " + tblname + " (";
        string sep;
        for (const auto& coldef : coldefs)
        {
            string column_type;
            switch (coldef.type)
            {
                case Type::BOOL:
                    column_type = "BOOLEAN";
                    break;
                case Type::TEXT:
                    column_type = "TINYTEXT";
                    break;
            }
            rval += sep + coldef.name + " " + column_type;
            sep = ",";
        }
        rval += "\n);";
        return rval;
    };

    auto drop_recreate_table = [gen_drop_sql, gen_create_sql](SQLite* db, const string& tblname,
                                                              const ColDefArray& coldefs) {
        bool rval = false;
        string drop_query = gen_drop_sql(tblname);
        string create_query = gen_create_sql(tblname, coldefs);
        if (!db->exec(drop_query))
        {
            MXB_ERROR("Failed to delete sqlite3 table: %s", db->error());
        }
        else if (!db->exec(create_query))
        {
            MXB_ERROR("Failed to create sqlite3 table: %s", db->error());
        }
        else
        {
            rval = true;
        }
        return rval;
    };

    // Sqlite3 doesn't require datatypes in the create-statement but it's good to have for clarity.
    const ColDefArray users_coldef = {{FIELD_USER, Type::TEXT},
                                      {FIELD_HOST, Type::TEXT},
                                      {FIELD_AUTHSTR, Type::TEXT},
                                      {FIELD_DEF_ROLE, Type::TEXT},
                                      {FIELD_ANYDB, Type::BOOL},
                                      {FIELD_IS_ROLE, Type::BOOL},
                                      {FIELD_HAS_PROXY, Type::BOOL}};
    const ColDefArray dbs_coldef = {{FIELD_USER, Type::TEXT},
                                    {FIELD_HOST, Type::TEXT},
                                    {FIELD_DB, Type::TEXT}};
    const ColDefArray roles_coldef = {{FIELD_USER, Type::TEXT},
                                      {FIELD_HOST, Type::TEXT},
                                      {FIELD_ROLE, Type::TEXT}};

    bool rval = false;
    auto sqlite = m_sqlite.get();
    if (drop_recreate_table(sqlite, TABLE_USER, users_coldef)
        && drop_recreate_table(sqlite, TABLE_DB, dbs_coldef)
        && drop_recreate_table(sqlite, TABLE_ROLES_MAPPING, roles_coldef))
    {
        rval = true;
    }
    return rval;
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
        "INSERT INTO " + TABLE_USER + " VALUES ('%s', '%s', %s, '%s', %s, '%s')";

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

    if (m_sqlite->exec(insert_sql))
    {
        if (proxy)
        {
            MXB_INFO("Added anonymous PAM user ''@'%s' with proxy grants using service %s.",
                     host, service_str.c_str());
        }
        else
        {
            MXB_INFO("Added normal PAM user '%s'@'%s' using service %s.", user, host, service_str.c_str());
        }
    }
    else
    {
        MXB_ERROR("Failed to insert user: %s", m_sqlite->error());
    }
}

/**
 * @brief Delete old users from the database
 */
void PamInstance::delete_old_users()
{
    /** Delete query used to clean up the database before loading new users */
    const string delete_query = "DELETE FROM " + TABLE_USER + ";";
    if (!m_sqlite->exec(delete_query))
    {
        MXB_ERROR("Failed to delete old users: %s", m_sqlite->error());
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

    string users_query, db_query, role_query;
    auto prepare_queries = [&users_query, &db_query, &role_query](bool using_roles) {
        string user_cols = "user, host, select_priv, insert_priv, update_priv, delete_priv, "
                           "authentication_string";
        string filter = "plugin = 'pam'";
        if (using_roles)
        {
            user_cols += ", default_role, is_role";
            filter += " OR is_role = 'Y'";     // If using roles, accept them as well.
        }
        else
        {
            user_cols += ", '' AS default_role, 'N' AS is_role"; // keeps the number of columns constant
        }
        users_query = mxb::string_printf("SELECT %s FROM mysql.user WHERE %s;",
                                         user_cols.c_str(), filter.c_str());

        string join_filter = "b.plugin = 'pam'";
        if (using_roles)
        {
            // Roles do not have plugins, yet may affect authentication.
            join_filter += " OR b.is_role = 'Y'";
        }
        const string inner_join = "INNER JOIN mysql.user AS b ON (a.user = b.user AND a.host = b.host "
                                  "AND (" + join_filter + "))";

        // Read database grants for pam users and roles. This is combined with table grants.
        db_query = "SELECT DISTINCT * FROM ("
                   // Select users/roles with general db-level privs ...
                   "(SELECT a.user, a.host, a.db FROM mysql.db AS a " + inner_join + ") "
                   "UNION "
                   // and combine with table privs counting as db-level privs.
                   "(SELECT a.user, a.host, a.db FROM mysql.tables_priv AS a " + inner_join + ")) AS c;";

        if (using_roles)
        {
            role_query = "SELECT a.user, a.host, a.role FROM mysql.roles_mapping AS a "
                    + inner_join + ";";
        }

    };

    const char* user;
    const char* pw_crypt;
    serviceGetUser(service, &user, &pw_crypt);
    int rval = MXS_AUTH_LOADUSERS_ERROR;

    char* pw_clear = decrypt_password(pw_crypt);
    if (pw_clear)
    {
        bool found_valid_server = false;
        bool got_data = false;
        for (auto sref = service->dbref; sref && !got_data; sref = sref->next)
        {
            SERVER* srv = sref->server;
            if (srv->is_active && srv->is_usable())
            {
                bool using_roles = false;
                auto version = srv->version();
                // Default roles are in server version 10.1.1.
                if (version.major > 10 || (version.major == 10 && (version.minor > 1
                    || (version.minor == 1 && version.patch == 1))))
                {
                    using_roles = true;
                }
                prepare_queries(using_roles);

                found_valid_server = true;
                MYSQL* mysql = mysql_init(NULL);
                if (mxs_mysql_real_connect(mysql, srv, user, pw_clear))
                {
                    string error_msg;
                    QResult users_res, dbs_res, roles_res;
                    // Perform the queries. All must succeed on the same backend.
                    // TODO: Think if it would be faster to do these queries concurrently.
                    if (((users_res = mxs::execute_query(mysql, users_query, &error_msg)) != nullptr)
                        && ((dbs_res = mxs::execute_query(mysql, db_query, &error_msg)) != nullptr))
                    {
                        if (using_roles)
                        {
                            if ((roles_res = mxs::execute_query(mysql, role_query, &error_msg)) != nullptr)
                            {
                                got_data = true;
                            }
                        }
                        else
                        {
                            got_data = true;
                        }
                    }

                    if (got_data)
                    {
                        fill_user_arrays(std::move(users_res), std::move(dbs_res), std::move(roles_res));
                        fetch_anon_proxy_users(srv, mysql);
                        rval = MXS_AUTH_LOADUSERS_OK;
                    }
                    else
                    {
                        MXB_ERROR("Failed to query server '%s' for PAM users. %s",
                                  srv->name(), error_msg.c_str());
                    }
                }
                mysql_close(mysql);
            }
        }

        if (!found_valid_server)
        {
            MXB_ERROR("Service '%s' had no valid servers to query PAM users from.", service->name());
        }
        MXS_FREE(pw_clear);
    }
    return rval;
}

void PamInstance::fill_user_arrays(QResult user_res, QResult db_res, QResult roles_mapping_res)
{
    m_sqlite->exec("BEGIN");
    // Delete any previous data.
    const char delete_fmt[] = "DELETE FROM %s;";
    for (const auto& tbl : {TABLE_USER, TABLE_DB, TABLE_ROLES_MAPPING})
    {
        string query = mxb::string_printf(delete_fmt, tbl.c_str());
        m_sqlite->exec(query);
    }

    // TODO: use prepared stmt:s
    if (user_res)
    {
        auto get_bool_enum = [&user_res](int64_t col_ind) {
            string val = user_res->get_string(col_ind);
            return (val == "Y" || val == "y");
        };

        auto get_bool_any = [&get_bool_enum](int64_t col_ind_min, int64_t col_ind_max) {
            bool rval = false;
            for (auto i = col_ind_min; i <= col_ind_max && !rval; i++)
            {
                bool val = get_bool_enum(i);
                if (val)
                {
                    rval = true;
                }
            }
            return rval;
        };
        // Input data order is: 0=user, 1=host, 2=select_priv, 3=insert_priv, 4=update_priv, 5=delete_priv,
        // 6=authentication_string, 7=default_role, 8=is_role

        // Output data order is: user, host, authentication_string, default_role, anydb, is_role, has_proxy.
        // The proxy-part is sorted out later.
        string insert_fmt = "INSERT INTO " + TABLE_USER + " VALUES ('%s', '%s', '%s', '%s', %i, %i, 0);";
        while (user_res->next_row())
        {
            auto username = user_res->get_string(0);
            auto host = user_res->get_string(1);
            bool has_global_priv = get_bool_any(2, 5);
            auto auth_string = user_res->get_string(6);
            string default_role = user_res->get_string(7);
            bool is_role = get_bool_enum(8);

            m_sqlite->exec(mxb::string_printf(insert_fmt.c_str(), username.c_str(), host.c_str(),
                                              auth_string.c_str(), default_role.c_str(), has_global_priv,
                                              is_role));
        }
    }

    if (db_res)
    {
        string insert_db_fmt = "INSERT INTO " + TABLE_DB + " VALUES ('%s', '%s', '%s');";
        while (db_res->next_row())
        {
            auto username = db_res->get_string(0);
            auto host = db_res->get_string(1);
            auto datab = db_res->get_string(2);
            m_sqlite->exec(mxb::string_printf(insert_db_fmt.c_str(),
                           username.c_str(), host.c_str(), datab.c_str()));
        }
    }

    if (roles_mapping_res)
    {
        string insert_roles_fmt = "INSERT INTO " + TABLE_ROLES_MAPPING + " VALUES ('%s', '%s', '%s');";
        while (roles_mapping_res->next_row())
        {
            auto username = roles_mapping_res->get_string(0);
            auto host = roles_mapping_res->get_string(1);
            auto role = roles_mapping_res->get_string(2);
            m_sqlite->exec(mxb::string_printf(insert_roles_fmt.c_str(),
                                              username.c_str(), host.c_str(), role.c_str()));
        }
    }
    m_sqlite->exec("COMMIT");
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

static int diag_cb_json(json_t* data, int columns, char** row, char** field_names)
{
    json_t* obj = json_object();
    for (int i = 0; i < columns; i++)
    {
        json_object_set_new(obj, field_names[i], json_string(row[i]));
    }
    json_array_append_new(data, obj);
    return 0;
}

json_t* PamInstance::diagnostic_json()
{
    json_t* rval = json_array();
    string select = "SELECT * FROM " + TABLE_USER + ";";
    if (!m_sqlite->exec(select, diag_cb_json, rval))
    {
        MXS_ERROR("Failed to print users: %s", m_sqlite->error());
    }
    return rval;
}

bool PamInstance::fetch_anon_proxy_users(SERVER* server, MYSQL* conn)
{
    bool success = true;
    const char anon_user_query[] = "SELECT host FROM mysql.user WHERE (user = '' AND plugin = 'pam');";

    // Query for anonymous users used with group mappings
    string error_msg;
    QResult anon_res;
    if ((anon_res = mxs::execute_query(conn, anon_user_query, &error_msg)) == nullptr)
    {
        MXS_ERROR("Failed to query server '%s' for anonymous PAM users. %s",
                  server->name(), error_msg.c_str());
        success = false;
    }
    else
    {
        auto anon_rows = anon_res->get_row_count();
        if (anon_rows > 0)
        {
            MXS_INFO("Found %lu anonymous PAM user(s). Checking them for proxy grants.", anon_rows);
        }

        while (anon_res->next_row())
        {
            string entry_host = anon_res->get_string(0);
            string query = mxb::string_printf("SHOW GRANTS FOR ''@'%s';", entry_host.c_str());
            // Check that the anon user has a proxy grant.
            QResult grant_res;
            if ((grant_res = mxs::execute_query(conn, query, &error_msg)) == nullptr)
            {
                MXS_ERROR("Failed to query server '%s' for grants of anonymous PAM user ''@'%s'. %s",
                          server->name(), entry_host.c_str(), error_msg.c_str());
                success = false;
            }
            else
            {
                const char grant_proxy[] = "GRANT PROXY ON";
                // The user may have multiple proxy grants. Just one is enough.
                const string update_query_fmt = "UPDATE " + TABLE_USER + " SET " + FIELD_HAS_PROXY
                        + " = 1 WHERE (" + FIELD_USER + " = '') AND (" + FIELD_HOST + " = '%s');";
                while (grant_res->next_row())
                {
                    string grant = grant_res->get_string(0);
                    if (grant.find(grant_proxy) != string::npos)
                    {
                        string update_query = mxb::string_printf(update_query_fmt.c_str(),
                                                                 entry_host.c_str());
                        m_sqlite->exec(update_query);
                        break;
                    }
                }
            }
        }
    }

    return success;
}
