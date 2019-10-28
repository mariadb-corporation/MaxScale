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

#pragma once

#include <maxscale/ccdefs.hh>

#include <condition_variable>
#include <map>
#include <set>
#include <thread>
#include <maxbase/stopwatch.hh>
#include <maxsql/queryresult.hh>
#include <maxscale/protocol2.hh>

class SERVER;

struct UserEntry
{
    std::string host_pattern;   /**< Hostname or IP, may have wildcards */
    std::string plugin;         /**< Auth plugin to use */
    std::string password;       /**< Auth data used by native auth plugin */
    std::string auth_string;    /**< Auth data used by other plugins */

    bool ssl {false};           /**< Should the user connect with ssl? */
    bool global_db_priv {false};/**< Does the user have access to all databases? */
    bool proxy_grant {false};   /**< Does the user have proxy grants? */

    bool        is_role {false};/**< Is the user a role? */
    std::string default_role;   /**< Default role if any */

    static bool host_pattern_is_more_specific(const UserEntry& lhs, const UserEntry& rhs);
};

/**
 * This class contains data from mysql.user-table. The data is a mapping from username to a list of entries,
 * where each entry is a hostname pattern with other relevant info.
 */
class UserDatabase
{
public:
    void             add_entry(const std::string& username, const UserEntry& entry);
    void             clear();
    size_t           size() const;
    const UserEntry* find_entry(const std::string& username, const std::string& host);

private:
    using EntryList = std::vector<UserEntry>;

    /**
     * Map of username -> EntryList. In the list, entries are ordered from most specific hostname pattern to
     * least specific
     */
    std::map<std::string, EntryList> m_contents;
};

class MariaDBUserManager : public mxs::UserAccountManager
{
public:
    explicit MariaDBUserManager(const std::string& service_name);

    /**
     * Start the updater thread. Should only be called when the updater is stopped or has just been created.
     */
    void start() override;

    /**
     * Stop the updater thread. Should only be called when the updater is running.
     */
    void stop() override;

    bool check_user(const std::string& user, const std::string& host,
                    const std::string& requested_db) override;

    void        update_user_accounts() override;
    void        set_credentials(const std::string& user, const std::string& pw) override;
    void        set_backends(const std::vector<SERVER*>& backends) override;
    std::string protocol_name() const override;

private:
    using QResult = std::unique_ptr<mxq::QueryResult>;

    bool load_users();

    void updater_thread_function();
    bool write_users(QResult users, bool using_roles);
    void write_dbs_and_roles(QResult dbs, QResult roles);

    // Fields for controlling the updater thread.
    std::thread      m_updater_thread;
    std::atomic_bool m_keep_running {false};

    // Fields needed for notifying the updater thread.
    std::condition_variable m_update_users_notifier;
    std::mutex              m_update_users_lock;
    std::atomic_bool        m_update_users_requested {false};

    // Settings and options. Access to most is protected by the mutex.
    std::mutex           m_settings_lock;
    std::string          m_username;
    std::string          m_password;
    std::vector<SERVER*> m_backends;
    mxb::Duration        m_update_interval {-1};

    const std::string m_service_name;   /**< Service using this account data manager. Used for logging. */

    // Using normal maps/sets so that entries can be printed in order.
    using StringSet = std::set<std::string>;
    using StringSetMap = std::map<std::string, StringSet>;

    std::mutex   m_usermap_lock;        /**< Protects maps from concurrent access */
    UserDatabase m_userdb;              /**< username -> entrylist mapping */
    StringSetMap m_database_grants;     /**< Maps "user@host" to allowed databases */
    StringSetMap m_roles_mapping;       /**< Maps "user@host" to allowed roles */
};
