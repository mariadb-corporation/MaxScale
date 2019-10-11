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

#include <maxsql/mariadb.hh>
#include <maxscale/server.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxbase/stopwatch.hh>

using std::string;

namespace
{
/** Table and column names. The names mostly match the ones in the server. */
const string TABLE_USER = "user";
const string TABLE_DB = "db";
const string TABLE_ROLES_MAPPING = "roles_mapping";

const string FIELD_USER = "user";
const string FIELD_HOST = "host";
const string FIELD_AUTHSTR = "authentication_string";
const string FIELD_DEF_ROLE = "default_role";
const string FIELD_ANYDB = "anydb";
const string FIELD_IS_ROLE = "is_role";
const string FIELD_HAS_PROXY = "proxy_grant";

const string FIELD_DB = "db";
const string FIELD_ROLE = "role";

auto acquire = std::memory_order_acquire;
auto release = std::memory_order_release;
}

using MutexLock = std::unique_lock<std::mutex>;
using Guard = std::lock_guard<std::mutex>;

void MariaDBUserManager::start()
{
    mxb_assert(!m_updater_thread.joinable());
    prepare_internal_db();
    m_keep_running.store(true, release);
    m_update_users_requested.store(true, release); // Update users immediately.

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
    string user;
    string pw;
    std::vector<SERVER*> backends;

    // Copy all settings under a lock.
    MutexLock lock(m_settings_lock);
    user = m_username;
    pw = m_password;
    backends = m_backends;
    lock.unlock();


    return true;
}

bool MariaDBUserManager::prepare_internal_db()
{
    if (m_users.open_inmemory())
    {
        // Prepare internal mysql.user
        return true;
    }

    return false;
}

