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
#include <maxscale/protocol/mariadb/authenticator.hh>

class SERVER;

/**
 * This class contains user data retrieved from the mysql-database.
 */
class UserDatabase
{
public:
    // Using normal maps/sets so that entries can be printed in order.
    using StringSet = std::set<std::string>;
    using StringSetMap = std::map<std::string, StringSet>;

    void   add_entry(const std::string& username, const mariadb::UserEntry& entry);
    void   set_dbs_and_roles(StringSetMap&& db_grants, StringSetMap&& roles_mapping);
    void   add_proxy_grant(const std::string& user, const std::string& host);
    void   clear();
    size_t size() const;

    /**
     * Find a user entry with matching user & host.
     *
     * @param username Client username. This must match exactly with the entry.
     * @param host Client address. This must match the entry host pattern.
     * @return The found entry, or null if not found
     */
    const mariadb::UserEntry*
    find_entry(const std::string& username, const std::string& host) const;

    /**
      * Find a user entry with matching user. Picks the first entry with a matching username without
      * considering the client address.
      *
      * @param username Client username. This must match exactly with the entry.
      * @return The found entry, or null if not found
      */
    const mariadb::UserEntry*
    find_entry(const std::string& username) const;

    /**
     * Check if user entry can access database. The access may be granted with a direct grant or through
     * the default role.
     *
     * @param entry User entry
     * @param db Target database
     * @param ignorecase If true, database names are compared case insensitive
     * @return True if user can access database
     */
    bool check_database_access(const mariadb::UserEntry& entry, const std::string& db,
                               bool ignorecase = false) const;

private:
    bool user_can_access_db(const std::string& user, const std::string& host_pattern, const std::string& db,
                            bool ignorecase) const;
    bool role_can_access_db(const std::string& role, const std::string& db, bool ignorecase) const;

    bool address_matches_host_pattern(const std::string& addr, const std::string& host_pattern) const;

    enum class HostPatternMode
    {
        SKIP,
        MATCH,
    };

    const mariadb::UserEntry*
    find_entry(const std::string& username, const std::string& host, HostPatternMode mode) const;

    enum class AddrType
    {
        UNKNOWN,
        IPV4,
        MAPPED,
        IPV6,
    };

    enum class PatternType
    {
        UNKNOWN,
        ADDRESS,
        MASK,
        HOSTNAME,
    };

    AddrType    parse_address_type(const std::string& addr) const;
    PatternType parse_pattern_type(const std::string& host_pattern) const;

    using EntryList = std::vector<mariadb::UserEntry>;

    /**
     * Map of username -> EntryList. In the list, entries are ordered from most specific hostname pattern to
     * least specific. In effect, contains data from mysql.user-table.
     */
    std::map<std::string, EntryList> m_contents;

    /** Maps "user@host" to allowed databases. Retrieved from mysql.db, mysql.tables_priv and
     * mysql.columns_priv. */
    StringSetMap m_database_grants;

    /** Maps "user@host" to allowed roles. Retrieved from mysql.roles_mapping. */
    StringSetMap m_roles_mapping;
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

    /**
     * Check if user@host exists and can access the requested database. Does not check password or
     * any other authentication credentials.
     *
     * @param user Client username
     * @param host Client hostname
     * @param requested_db Database requested by client. May be empty.
     * @return Found user entry.
     */
    std::unique_ptr<mariadb::UserEntry>
    find_user(const std::string& user, const std::string& host, const std::string& requested_db) const;

    std::unique_ptr<mariadb::UserEntry>
    find_anon_proxy_user(const std::string& user, const std::string& host) const;

    void        update_user_accounts() override;
    void        set_credentials(const std::string& user, const std::string& pw) override;
    void        set_backends(const std::vector<SERVER*>& backends) override;

    std::string protocol_name() const override;

private:
    using QResult = std::unique_ptr<mxq::QueryResult>;

    bool load_users();

    void updater_thread_function();
    bool read_users(QResult users, bool using_roles);
    void read_dbs_and_roles(QResult dbs, QResult roles);
    void read_proxy_grants(QResult proxies);

    // Fields for controlling the updater thread.
    std::thread             m_updater_thread;
    std::atomic_bool        m_keep_running {false};
    std::condition_variable m_notifier;
    std::mutex              m_notifier_lock;
    std::atomic_bool        m_update_users_requested {false};

    // Settings and options. Access to most is protected by the mutex.
    std::mutex           m_settings_lock;
    std::string          m_username;
    std::string          m_password;
    std::vector<SERVER*> m_backends;

    const std::string m_service_name;   /**< Service using this account data manager. Used for logging. */

    /** Warn if no valid servers to query from. Starts false, as in the beginning monitors may not have
     * ran yet. */
    bool m_warn_no_servers {false};

    mutable std::mutex m_userdb_lock;           /**< Protects UserDatabase from concurrent access */
    UserDatabase       m_userdb;                /**< Contains user account info */
};
