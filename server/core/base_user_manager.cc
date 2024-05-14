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

#include <maxscale/base_user_manager.hh>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/threadpool.hh>
#include <maxscale/config.hh>
#include <maxscale/service.hh>

namespace
{
using std::string;
using Guard = std::lock_guard<std::mutex>;
using MutexLock = std::unique_lock<std::mutex>;
constexpr auto acquire = std::memory_order_acquire;
constexpr auto release = std::memory_order_release;
constexpr auto relaxed = std::memory_order_relaxed;

/** How many times users can be successfully loaded before throttling kicks in. */
const int throttling_start_loads = 5;

/** Max user load attempts when starting. If this limit is exceeded, throttling kicks in. */
const int user_load_fail_limit = 10;
}

namespace maxscale
{
BaseUserManager::BaseUserManager()
    : m_last_update{time(nullptr)}
{
}

bool BaseUserManager::strip_db_esc() const
{
    return m_strip_db_esc.load(relaxed);
}

bool BaseUserManager::union_over_backends() const
{
    return m_union_over_backends.load(relaxed);
}

void BaseUserManager::start()
{
    mxb_assert(!m_updater_thread.joinable());
    m_keep_running.store(true, release);
    m_updater_thread = std::thread([this] {
        updater_thread_function();
    });
    mxb::set_thread_name(m_updater_thread, "UserManager");
    m_thread_started.wait();
}

void BaseUserManager::stop()
{
    mxb_assert(m_updater_thread.joinable());
    m_keep_running.store(false, release);
    m_notifier.notify_one();
    m_updater_thread.join();
}

void BaseUserManager::update_user_accounts()
{
    {
        Guard guard(m_notifier_lock);
        m_update_users_requested.store(true, release);
    }
    m_warn_no_servers.store(true, relaxed);
    m_notifier.notify_one();
}

void BaseUserManager::set_credentials(const std::string& user, const std::string& pw)
{
    Guard guard(m_settings_lock);
    if (user != m_username)
    {
        m_username = user;
        m_password = pw;
        m_prev_password.clear();
    }
    else if (pw != m_password)
    {
        m_prev_password = m_password;
        m_password = pw;
    }
}

void BaseUserManager::set_backends(const std::vector<SERVER*>& backends)
{
    Guard guard(m_settings_lock);
    m_backends = backends;
}

void BaseUserManager::set_user_accounts_file(const string& filepath, UsersFileUsage file_usage)
{
    Guard guard(m_settings_lock);
    m_users_file_path = filepath;
    m_users_file_usage = file_usage;
}

void BaseUserManager::set_union_over_backends(bool union_over_backends)
{
    m_union_over_backends.store(union_over_backends, relaxed);
}

void BaseUserManager::set_strip_db_esc(bool strip_db_esc)
{
    m_strip_db_esc.store(strip_db_esc, relaxed);
}

void BaseUserManager::updater_thread_function()
{
    using mxb::TimePoint;
    using mxb::Duration;
    using mxb::Clock;

    // Minimum wait between update loops. User accounts should not be changing continuously.
    const std::chrono::seconds default_min_interval(1);

    // Default value for scheduled updates. Cannot set too far in the future, as the cv wait_until bugs and
    // doesn't wait.
    const std::chrono::hours default_max_interval(24);

    bool first_iteration = true;
    bool throttling = false;
    TimePoint last_update = Clock::now();

    auto should_stop_waiting = [this]() {
        return !m_keep_running.load(acquire) || m_update_users_requested.load(acquire);
    };

    while (m_keep_running.load(acquire))
    {
        /**
         *  The user updating is controlled by several factors:
         *  1) In the beginning, a hardcoded interval is used to try to repeatedly update users as
         *  the monitor is performing its first loop.
         *  2) User refresh requests from the owning service. These can come at any time and rate.
         *  3) users_refresh_time, the minimum time which should pass between refreshes. This means that
         *  rapid update requests may be ignored.
         *  4) users_refresh_interval, the maximum time between refreshes. Users should be refreshed
         *  automatically if this time elapses.
         */
        mxs::Config& glob_config = mxs::Config::get();
        auto max_refresh_interval = glob_config.users_refresh_interval.get();
        auto min_refresh_interval = glob_config.users_refresh_time.get();
        bool throttling_enabled = min_refresh_interval.count() > 0;

        // Calculate the earliest allowed time for next update. If throttling is not on, next update can
        // happen immediately.
        TimePoint next_possible_update = last_update;
        if (throttling)
        {
            mxb_assert(throttling_enabled);
            next_possible_update += min_refresh_interval;
        }

        // Calculate the time for the next scheduled update.
        TimePoint next_scheduled_update = last_update;
        if (first_iteration)
        {
            // Try to update immediately.
        }
        else if (!throttling && m_successful_loads == 0)
        {
            // If updating has not succeeded even once yet, keep trying again and again,
            // with just a minimal wait.
            next_scheduled_update += default_min_interval;
        }
        else
        {
            next_scheduled_update += (max_refresh_interval.count() > 0) ?
                max_refresh_interval : default_max_interval;
        }

        MutexLock lock(m_notifier_lock);
        // Wait until "next_possible_update", or until the thread should stop.
        m_notifier.wait_until(lock, next_possible_update, should_stop_waiting);

        m_can_update.store(true, release);
        if (first_iteration)
        {
            // Thread has properly started and the "can_update"-state is visible to other threads.
            m_thread_started.post();
            first_iteration = false;
        }

        // Wait until "next_scheduled_update", or until update requested or thread stop.
        m_notifier.wait_until(lock, next_scheduled_update, should_stop_waiting);
        lock.unlock();

        if (m_keep_running.load(acquire))
        {
            if (update_users())
            {
                m_consecutive_failed_loads = 0;
                m_successful_loads++;
                m_warn_no_servers.store(true, relaxed);
            }
            else
            {
                m_consecutive_failed_loads++;
            }
        }

        /**
         * Throttling kicks in if users have been loaded a few times, or if loading has failed repeatedly
         * often enough. This allows a few quick user account updates at the beginning. The quick updates
         * are useful for test situations, where users are often created just after MaxScale has started. */
        throttling = (m_successful_loads > throttling_start_loads
            || m_consecutive_failed_loads > user_load_fail_limit)
            && throttling_enabled;

        if (throttling)
        {
            m_can_update.store(false, release);
        }

        m_service->sync_user_account_caches();
        m_update_users_requested.store(false, release);
        last_update = Clock::now();
        m_last_update.store(time(nullptr), std::memory_order_relaxed);
    }

    // Possible race here: If throttling=false and m_keep_running=false, m_can_update may be momentarily
    // "true" even when thread is exiting the loop. If a client is logging at that exact moment, the session
    // may be put on standby without ever waking up. This is not an issue if the thread stops only when
    // MaxScale is shutting down.
    m_can_update.store(false, release);
}

void BaseUserManager::set_service(SERVICE* service)
{
    mxb_assert(!m_service);
    m_service = service;
}

bool BaseUserManager::can_update_immediately() const
{
    return m_can_update.load(acquire);
}

time_t BaseUserManager::last_update() const
{
    return m_last_update.load(std::memory_order_relaxed);
}

const char* BaseUserManager::svc_name() const
{
    return m_service->name();
}

BaseUserManager::LoadSettings BaseUserManager::get_load_settings() const
{
    LoadSettings rval;
    // Some settings are arraylike so copy under a lock.
    MutexLock lock(m_settings_lock);
    rval.conn_user = m_username;
    rval.conn_pw = m_password;
    rval.conn_prev_pw = m_prev_password;
    rval.backends = m_backends;
    rval.users_file_path = m_users_file_path;
    rval.users_file_usage = m_users_file_usage;
    lock.unlock();
    return rval;
}
}
