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

#include <sqlite3.h>
#include <maxsql/mariadb_connector.hh>
#include <maxsql/mariadb.hh>
#include <maxscale/server.hh>
#include <maxscale/paths.h>
#include <maxscale/protocol/mariadb/module_names.hh>

using std::string;
using mxq::MariaDB;
using MutexLock = std::unique_lock<std::mutex>;
using Guard = std::lock_guard<std::mutex>;

namespace
{

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
}

MariaDBUserManager::MariaDBUserManager(const string& service_name)
    : m_service_name(service_name)
{
}

void MariaDBUserManager::start()
{
    mxb_assert(!m_updater_thread.joinable());
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
                    int rows = users_res->get_row_count();
                    if (write_users(std::move(users_res), using_roles))
                    {
                        write_dbs_and_roles(std::move(dbs_res), std::move(roles_res));
                        wrote_data = true;
                        // Add anonymous proxy user search.
                        MXB_NOTICE("Read %lu usernames with a total of %i user@host entries from '%s'.",
                                   m_userdb.size(), rows, srv->name());
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

bool MariaDBUserManager::write_users(QResult users, bool using_roles)
{
    auto get_bool_enum = [&users](int64_t col_ind) {
            string val = users->get_string(col_ind);
            return val == "Y" || val == "y";
        };

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

    bool error = false;
    if (has_required_fields)
    {
        Guard guard(m_usermap_lock);

        // Delete any previous data.
        m_userdb.clear();

        while (users->next_row())
        {
            auto username = users->get_string(ind_user);

            UserEntry new_entry;
            new_entry.host_pattern = users->get_string(ind_host);

            // Treat the user as having global privileges if any of the following global privileges
            // exists.
            new_entry.global_db_priv = get_bool_enum(ind_sel_priv) || get_bool_enum(ind_ins_priv)
                || get_bool_enum(ind_upd_priv) || get_bool_enum(ind_del_priv);

            // Require SSL if the entry is not empty.
            new_entry.ssl = !users->get_string(ind_ssl).empty();

            new_entry.plugin = users->get_string(ind_plugin);
            new_entry.password = users->get_string(ind_pw);
            new_entry.auth_string = users->get_string(ind_auth_str);

            if (using_roles)
            {
                new_entry.is_role = get_bool_enum(ind_is_role);
                new_entry.default_role = users->get_string(ind_def_role);
            }

            m_userdb.add_entry(username, new_entry);
        }
    }
    else
    {
        MXB_ERROR("Received invalid data when querying user accounts.");
        error = true;
    }
    return !error;
}

void MariaDBUserManager::write_dbs_and_roles(QResult dbs, QResult roles)
{
    // Because the database grant and roles tables are quite simple and only require lookups, their contents
    // need not be saved in an sqlite database. This simplifies things quite a bit.

    auto map_builder = [](const string& grant_col_name, QResult source) {
            StringSetMap result;
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
    StringSetMap new_db_grants = map_builder("db", std::move(dbs));
    StringSetMap new_roles_mapping;
    if (roles)
    {
        // Old backends may not have role data.
        new_roles_mapping = map_builder("role", std::move(roles));
    }

    Guard guard(m_usermap_lock);
    m_database_grants = std::move(new_db_grants);
    m_roles_mapping = std::move(new_roles_mapping);
}

void UserDatabase::add_entry(const std::string& username, const UserEntry& entry)
{
    auto& entrylist = m_contents[username];
    // Find the correct spot to insert. Will insert duplicate hostname patterns, although these should
    // not exist in the source data.
    auto insert_iter = std::upper_bound(entrylist.begin(), entrylist.end(), entry,
                                        UserEntry::host_pattern_is_more_specific);
    entrylist.insert(insert_iter, entry);
}

void UserDatabase::clear()
{
    m_contents.clear();
}

const UserEntry* UserDatabase::find_entry(const std::string& username, const std::string& host)
{
    const UserEntry* rval = nullptr;
    auto iter = m_contents.find(username);
    if (iter != m_contents.end())
    {
        auto& entrylist = iter->second;
        // List is already ordered, take the first matching entry.
        for (auto& entry : entrylist)
        {
            // The entry must not be a role (they should have empty hostnames in any case) and the hostname
            // pattern should match the host.
            // TODO: add checking for bitmasks and possibly name lookups (tricky...)
            // TODO: sqlite3_strlike(entry.host_pattern.c_str(), host.c_str(), '\\') == 0
            if (!entry.is_role && entry.host_pattern == host)
            {
                rval = &entry;
                break;
            }
        }
    }
    return rval;
}

size_t UserDatabase::size() const
{
    return m_contents.size();
}

bool UserEntry::host_pattern_is_more_specific(const UserEntry& lhs, const UserEntry& rhs)
{
    // Order entries according to https://mariadb.com/kb/en/library/create-user/
    const string& lhost = lhs.host_pattern;
    const string& rhost = rhs.host_pattern;
    const char wildcards[] = "%_";
    auto lwc_pos = lhost.find_first_of(wildcards);
    auto rwc_pos = rhost.find_first_of(wildcards);
    bool lwc = (lwc_pos != string::npos);
    bool rwc = (rwc_pos != string::npos);

    // The host without wc:s sorts earlier than the one with them,
    return (!lwc && rwc)
            // ... and if both have wc:s, the one with the later wc wins (ties broken by strcmp),
           || (lwc && rwc && ((lwc_pos > rwc_pos) || (lwc_pos == rwc_pos && lhost < rhost)))
            // ... and if neither have wildcards, use string order.
           || (!lwc && !rwc && lhost < rhost);
}
