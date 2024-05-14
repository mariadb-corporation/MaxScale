/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <condition_variable>
#include <thread>
#include <maxbase/semaphore.hh>
#include <maxscale/protocol2.hh>

namespace maxscale
{
class BaseUserManager : public mxs::UserAccountManager
{
public:
    ~BaseUserManager() override = default;

    static constexpr const char* RECENTLY_UPDATED_FMT = "User accounts have been recently updated, "
                                                        "cannot update again for %s.";
    bool   can_update_immediately() const;
    time_t last_update() const override;
    void   update_user_accounts() override;

    void set_credentials(const std::string& user, const std::string& pw) override;
    void set_backends(const std::vector<SERVER*>& backends) override;
    void set_union_over_backends(bool union_over_backends) override;
    void set_strip_db_esc(bool strip_db_esc) override;
    void set_user_accounts_file(const std::string& filepath, UsersFileUsage file_usage) override;
    void set_service(SERVICE* service) override;

    void start() override;
    void stop() override;

protected:
    BaseUserManager();

    bool        strip_db_esc() const;
    bool        union_over_backends() const;
    const char* svc_name() const;

    struct LoadSettings
    {
        std::string          conn_user;
        std::string          conn_pw;
        std::string          conn_prev_pw;
        std::vector<SERVER*> backends;
        std::string          users_file_path;
        UsersFileUsage       users_file_usage;
    };
    LoadSettings get_load_settings() const;

    /** Warn if no valid servers to query from. Starts false, as in the beginning monitors may not have
     *  ran yet. */
    std::atomic_bool m_warn_no_servers {false};

private:
    virtual bool update_users() = 0;

private:
    void updater_thread_function();

    // Fields for controlling the updater thread.
    std::thread             m_updater_thread;
    std::atomic_bool        m_keep_running {false};
    std::condition_variable m_notifier;
    std::mutex              m_notifier_lock;
    std::atomic_bool        m_update_users_requested {false};
    mxb::Semaphore          m_thread_started;   /* Posted when updater thread has properly started. */

    std::atomic_bool m_can_update {false};      /**< User accounts can or are about to be updated */
    int              m_successful_loads {0};    /**< Successful refreshes */

    /** How many times user loading has continuously failed. User for suppressing error messages. */
    int m_consecutive_failed_loads {0};

    /** The last time the users were loaded */
    std::atomic<time_t> m_last_update;

    // Settings and options. Access to arraylike fields is protected by the mutex.
    mutable std::mutex   m_settings_lock;
    std::string          m_username;
    std::string          m_password;
    std::string          m_prev_password;
    std::vector<SERVER*> m_backends;
    SERVICE*             m_service {nullptr};   /**< Service using this account data manager */

    /** User accounts file related settings. */
    std::string    m_users_file_path;
    UsersFileUsage m_users_file_usage {UsersFileUsage::ADD_WHEN_LOAD_OK};

    /** Fetch users from all backends and store the union. */
    std::atomic_bool m_union_over_backends {false};
    /** Remove escape characters '\' from database names when fetching user info from backend. */
    std::atomic_bool m_strip_db_esc {true};
};
}
