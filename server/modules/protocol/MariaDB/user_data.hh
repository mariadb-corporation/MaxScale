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
#include <maxsql/sqlite.hh>
#include <maxsql/queryresult.hh>
#include <maxscale/protocol2.hh>

class SERVER;

class MariaDBUserManager : public mxs::UserAccountManager
{
public:
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
    bool prepare_internal_db();
    void updater_thread_function();

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

    // The main mysql.user-table is stored in sqlite to enable complicated queries */
    mxq::SQLite m_users;

    // Using normal maps/sets so that entries can be printed in order.
    using StringSet = std::set<std::string>;
    using UserMap = std::map<std::string, StringSet>;

    UserMap m_database_grants;  /**< Maps "user@host" to allowed databases */
    UserMap m_roles_mapping;    /**< Maps "user@host" to allowed roles */
};
