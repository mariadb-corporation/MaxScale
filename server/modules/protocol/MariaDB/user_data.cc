/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "user_data.hh"

#include <maxsql/mariadb_connector.hh>
#include <maxsql/mariadb.hh>
#include <maxscale/server.hh>
#include <maxscale/paths.h>
#include <maxscale/protocol/mariadb/module_names.hh>

using std::string;
using mxq::SQLite;
using mxq::SQLiteStmt;
using mxq::SQLiteQueryResult;
using mxq::MariaDB;

namespace
{
/**
 * Table and column names used in the internal sqlite database. The names mostly match the server.
 */
const string TABLE_USER = "user";

const string FIELD_USER = "user";
const string FIELD_HOST = "host";
const string FIELD_PW = "password";
const string FIELD_GLOBAL_PRIV = "global_priv";
const string FIELD_SSL = "ssl";
const string FIELD_PLUGIN = "plugin";
const string FIELD_AUTHSTR = "authentication_string";
const string FIELD_DEF_ROLE = "default_role";
const string FIELD_IS_ROLE = "is_role";
const string FIELD_HAS_PROXY = "proxy_grant";

auto acquire = std::memory_order_acquire;
auto release = std::memory_order_release;

namespace backend_queries
{
const string users_query = "SELECT * FROM mysql.user;";
const string db_grants_query =
    "SELECT DISTINCT * FROM ("
    // Select users/roles with general db-level privs ...
    "(SELECT a.user, a.host, a.db FROM mysql.db AS a) UNION "
    // and combine with table privs counting as db-level privs
    "(SELECT a.user, a.host, a.db FROM mysql.tables_priv AS a) UNION "
    // and finally combine with column-level privs as db-level privs
    "(SELECT a.user, a.host, a.db FROM mysql.columns_priv AS a) ) AS c;";
const string roles_query = "SELECT a.user, a.host, a.role FROM mysql.roles_mapping AS a;";
}

namespace sqlite_constants
{

struct ColDef
{
    enum class ColType
    {
        BOOL,
        TEXT,
    };
    string  name;
    ColType type;
};
using ColDefArray = std::vector<ColDef>;
using Type = ColDef::ColType;

// Define the schema for the internal mysql.user-table.
// Sqlite3 doesn't require datatypes in the create-statement but it's good to have for clarity.
const ColDefArray users_table_columns = {
    {FIELD_USER,        Type::TEXT},    // Username, must match exactly, except for anon users
    {FIELD_HOST,        Type::TEXT},    // User host, may have wildcards
    {FIELD_GLOBAL_PRIV, Type::BOOL},    // Does the user have access to all databases?
    {FIELD_SSL,         Type::BOOL},    // Should the user connect with ssl?
    {FIELD_PLUGIN,      Type::TEXT},    // Auth plugin to use
    {FIELD_PW,          Type::TEXT},    // Auth data used by native auth plugin
    {FIELD_AUTHSTR,     Type::TEXT},    // Auth data used by other plugins
    {FIELD_IS_ROLE,     Type::BOOL},    // Is the user a role?
    {FIELD_DEF_ROLE,    Type::TEXT},    // Default role if any
    {FIELD_HAS_PROXY,   Type::BOOL},    // Does the user have proxy grants?
};

// Precalculate some of the more complicated queries.

string gen_create_table(const string& tblname, const ColDefArray& coldefs)
{
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
        sep = ", ";
    }
    rval += ");";
    return rval;
}

string gen_insert_elem()
{
    string rval = "INSERT INTO " + TABLE_USER + " VALUES (";
    string sep;
    for (auto field : users_table_columns)
    {
        rval += sep + ":" + field.name;
        sep = ", ";
    }
    rval += ");";
    return rval;
}

const string drop_table = "DROP TABLE IF EXISTS " + TABLE_USER + ";";
const string create_table = gen_create_table(TABLE_USER, users_table_columns);
const string insert_elem = gen_insert_elem();
}
}

using MutexLock = std::unique_lock<std::mutex>;
using Guard = std::lock_guard<std::mutex>;

MariaDBUserManager::MariaDBUserManager(const string& name)
    : m_users_filename(string(get_cachedir()) + "/" + name + ".sqlite3")
{
}

void MariaDBUserManager::start()
{
    mxb_assert(!m_updater_thread.joinable());
    prepare_internal_db();
    m_keep_running.store(true, release);
    m_update_users_requested.store(true, release);      // Update users immediately.

    m_updater_thread = std::thread([this] {
                                       updater_thread_function();
                                   });
}

void MariaDBUserManager::stop()
{
    mxb_assert(m_updater_thread.joinable());
    m_keep_running.store(false, release);
    m_update_users_notifier.notify_one();
    m_updater_thread.join();
}

bool MariaDBUserManager::check_user(const std::string& user, const std::string& host,
                                    const std::string& requested_db)
{
    return false;
}

void MariaDBUserManager::update_user_accounts()
{
    MutexLock lock(m_update_users_lock);
    m_update_users_requested.store(true, release);
    lock.unlock();
    m_update_users_notifier.notify_one();
}

void MariaDBUserManager::set_credentials(const std::string& user, const std::string& pw)
{
    Guard guard(m_settings_lock);
    m_username = user;
    m_password = pw;
}

void MariaDBUserManager::set_backends(const std::vector<SERVER*>& backends)
{
    Guard guard(m_settings_lock);
    m_backends = backends;
}

std::string MariaDBUserManager::protocol_name() const
{
    return MXS_MARIADB_PROTOCOL_NAME;
}

void MariaDBUserManager::updater_thread_function()
{
    while (m_keep_running.load(acquire))
    {
        auto cv_predicate = [this] {
                return m_update_users_requested.load(acquire) || !m_keep_running.load(acquire);
            };

        // Wait for something to do. Regular user account updates could be added here.
        MutexLock lock(m_update_users_lock);
        if (m_update_interval.secs() > 0)
        {
            m_update_users_notifier.wait_for(lock, m_update_interval, cv_predicate);
        }
        else
        {
            m_update_users_notifier.wait(lock, cv_predicate);
        }
        lock.unlock();

        load_users();

        // Users updated, can accept new requests. TODO: add rate limits etc.
        m_update_users_requested.store(false, release);
    }
}

bool MariaDBUserManager::load_users()
{
    MariaDB::ConnectionSettings sett;
    std::vector<SERVER*> backends;

    // Copy all settings under a lock.
    MutexLock lock(m_settings_lock);
    sett.user = m_username;
    sett.password = m_password;
    backends = m_backends;
    lock.unlock();

    mxq::MariaDB con;
    con.set_connection_settings(sett);

    bool found_valid_server = false;
    bool got_data = false;
    bool wrote_data = false;

    for (size_t i = 0; i < backends.size() && !got_data; i++)
    {
        SERVER* srv = backends[i];
        if (srv->is_active && srv->is_usable())
        {
            bool using_roles = false;
            auto version = srv->version();
            // Default roles are in server version 10.1.1.
            if (version.major > 10 || (version.major == 10
                                       && (version.minor > 1 || (version.minor == 1 && version.patch == 1))))
            {
                using_roles = true;
            }

            found_valid_server = true;
            if (con.open(srv->address, srv->port))
            {
                QResult users_res, dbs_res, roles_res;
                // Perform the queries. All must succeed on the same backend.
                if (((users_res = con.query(backend_queries::users_query)) != nullptr)
                    && ((dbs_res = con.query(backend_queries::db_grants_query)) != nullptr))
                {
                    if (using_roles)
                    {
                        if ((roles_res = con.query(backend_queries::roles_query)) != nullptr)
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
                    if (write_users(std::move(users_res), using_roles))
                    {
                        write_dbs_and_roles(std::move(dbs_res), std::move(roles_res));
                        wrote_data = true;
                        // Add anonymous proxy user search.
                    }
                }
                else
                {
                    MXB_ERROR("Failed to query server '%s' for user account info. %s",
                              srv->name(), con.error());
                }
            }
        }
    }

    if (!found_valid_server)
    {
        MXB_ERROR("No valid servers from which to query MariaDB user accounts found.");
    }
    return wrote_data;
}

bool MariaDBUserManager::prepare_internal_db()
{
    if (m_users.open_inmemory())
    {
        bool rval = false;
        if (!m_users.exec(sqlite_constants::drop_table))
        {
            MXB_ERROR("Failed to delete sqlite3 table: %s", m_users.error());
        }
        else if (!m_users.exec(sqlite_constants::create_table))
        {
            MXB_ERROR("Failed to create sqlite3 table: %s", m_users.error());
        }
        else
        {
            rval = true;
        }
        return rval;
    }

    return false;
}

bool MariaDBUserManager::write_users(QResult users, bool using_roles)
{
    auto get_bool_enum = [&users](int64_t col_ind) {
            string val = users->get_string(col_ind);
            return val == "Y" || val == "y";
        };
    bool rval = false;

    // Get column indexes for the interesting fields. Depending on backend version, they may not all
    // exist. Some of the field name start with a capital and some don't. Should the index search be
    // ignorecase?
    auto ind_user = users->get_col_index("User");
    auto ind_host = users->get_col_index("Host");
    auto ind_sel_priv = users->get_col_index("Select_priv");
    auto ind_ins_priv = users->get_col_index("Insert_priv");
    auto ind_upd_priv = users->get_col_index("Update_priv");
    auto ind_del_priv = users->get_col_index("Delete_priv");
    auto ind_ssl = users->get_col_index("ssl_type");
    auto ind_plugin = users->get_col_index("plugin");
    auto ind_pw = users->get_col_index("Password");
    auto ind_auth_str = users->get_col_index("authentication_string");
    auto ind_is_role = users->get_col_index("is_role");
    auto ind_def_role = users->get_col_index("default_role");

    bool has_required_fields = (ind_user >= 0) && (ind_host >= 0)
        && (ind_sel_priv >= 0) && (ind_ins_priv >= 0) && (ind_upd_priv >= 0) && (ind_del_priv >= 0)
        && (ind_ssl >= 0) && (ind_plugin >= 0) && (ind_pw >= 0) && (ind_auth_str >= 0)
        && (!using_roles || (ind_is_role >= 0 && ind_def_role >= 0));

    if (has_required_fields)
    {
        // Do everything in one big trx.
        m_users.exec("BEGIN;");
        // Delete any previous data.
        m_users.exec("DELETE FROM " + TABLE_USER + ";");

        auto insert_stmt = m_users.prepare(sqlite_constants::insert_elem);
        if (insert_stmt)
        {
            // Now, get the parameter bind indexes.
            int param_ind_user = insert_stmt->bind_parameter_index(FIELD_USER);
            int param_ind_host = insert_stmt->bind_parameter_index(FIELD_HOST);
            int param_ind_global_priv = insert_stmt->bind_parameter_index(FIELD_GLOBAL_PRIV);
            int param_ind_ssl = insert_stmt->bind_parameter_index(FIELD_SSL);
            int param_ind_plugin = insert_stmt->bind_parameter_index(FIELD_PLUGIN);
            int param_ind_pw = insert_stmt->bind_parameter_index(FIELD_PW);
            int param_ind_auth_str = insert_stmt->bind_parameter_index(FIELD_AUTHSTR);
            int param_ind_is_role = insert_stmt->bind_parameter_index(FIELD_IS_ROLE);
            int param_ind_def_role = insert_stmt->bind_parameter_index(FIELD_DEF_ROLE);
            int param_ind_proxy = insert_stmt->bind_parameter_index(FIELD_HAS_PROXY);
            mxb_assert(param_ind_user > 0 && param_ind_host > 0 && param_ind_global_priv > 0
                       && param_ind_ssl > 0 && param_ind_plugin > 0 && param_ind_pw > 0
                       && param_ind_auth_str > 0
                       && param_ind_is_role > 0 && param_ind_def_role > 0 && param_ind_proxy > 0);

            bool prepared_stmt_error = false;
            while (users->next_row() && !prepared_stmt_error)
            {
                // Bind the row values to the insert statement.
                auto username = users->get_string(ind_user);
                auto host = users->get_string(ind_host);
                insert_stmt->bind_string(param_ind_user, username);
                insert_stmt->bind_string(param_ind_host, host);

                // Treat the user as having global privileges if any of the following global privileges
                // exists.
                bool global_priv = get_bool_enum(ind_sel_priv) || get_bool_enum(ind_ins_priv)
                    || get_bool_enum(ind_upd_priv) || get_bool_enum(ind_del_priv);
                insert_stmt->bind_bool(param_ind_global_priv, global_priv);

                // Require SSL if the entry is not empty.
                insert_stmt->bind_bool(param_ind_ssl, !users->get_string(ind_ssl).empty());

                auto plugin = users->get_string(ind_plugin);
                auto pw = users->get_string(ind_pw);
                auto auth_str = users->get_string(ind_auth_str);
                insert_stmt->bind_string(param_ind_plugin, plugin);
                insert_stmt->bind_string(param_ind_pw, pw);
                insert_stmt->bind_string(param_ind_auth_str, auth_str);

                if (using_roles)
                {
                    insert_stmt->bind_bool(param_ind_is_role, get_bool_enum(ind_is_role));
                    insert_stmt->bind_string(param_ind_def_role, users->get_string(ind_def_role));
                }

                // Write false to the proxy grant as it's added later.
                insert_stmt->bind_bool(param_ind_proxy, false);
                // All elements prepared, execute statement and reset.
                if (!insert_stmt->step_execute() || !insert_stmt->reset())
                {
                    prepared_stmt_error = true;
                }
            }

            if (prepared_stmt_error)
            {
                MXB_ERROR("SQLite error when writing to user account table: %s", insert_stmt->error());
            }
            else
            {
                rval = true;
            }
        }
        else
        {
            MXB_ERROR("Could not prepare SQLite statement: %s", m_users.error());
        }
        m_users.exec("COMMIT");
    }
    else
    {
        MXB_ERROR("Received invalid data when querying user accounts.");
    }
    return rval;
}

void MariaDBUserManager::write_dbs_and_roles(QResult dbs, QResult roles)
{
    // Because the database grant and roles tables are quite simple and only require lookups, their contents
    // need not be saved in an sqlite database. This simplifies things quite a bit.

    auto map_builder = [](const string& grant_col_name, QResult source) {
            UserMap result;
            auto ind_user = source->get_col_index("user");
            auto ind_host = source->get_col_index("host");
            auto ind_grant = source->get_col_index(grant_col_name);
            bool valid_data = (ind_user >= 0 && ind_host >= 0 && ind_grant >= 0);
            if (valid_data)
            {
                while (source->next_row())
                {
                    string key = source->get_string(ind_user) + "@" + source->get_string(ind_host);
                    string grant = source->get_string(ind_grant);
                    result[key].insert(grant);
                }
            }
            return result;
        };

    // The maps are mutex-protected. Before locking, prepare the result maps entirely.
    UserMap new_db_grants = map_builder("db", std::move(dbs));
    UserMap new_roles_mapping;
    if (roles)
    {
        // Old backends may not have role data.
        new_roles_mapping = map_builder("role", std::move(roles));
    }

    Guard guard(m_usermap_lock);
    m_database_grants = std::move(new_db_grants);
    m_roles_mapping = std::move(new_roles_mapping);
}
