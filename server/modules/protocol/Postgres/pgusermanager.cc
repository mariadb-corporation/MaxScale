/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pgusermanager.hh"
#include <maxbase/format.hh>
#include <maxbase/threadpool.hh>
#include <maxscale/config.hh>
#include <maxscale/protocol/postgresql/module_names.hh>
#include <maxscale/service.hh>

using std::string;
using std::vector;
using Guard = std::lock_guard<std::mutex>;
using ServerType = SERVER::VersionInfo::Type;

namespace
{
constexpr auto acquire = std::memory_order_acquire;
constexpr auto relaxed = std::memory_order_relaxed;
constexpr auto npos = string::npos;
}

PgUserManager::PgUserManager()
    : m_userdb(std::make_shared<PgUserDatabase>())
{
}

std::string PgUserManager::protocol_name() const
{
    return MXS_POSTGRESQL_PROTOCOL_NAME;
}

std::unique_ptr<mxs::UserAccountCache> PgUserManager::create_user_account_cache()
{
    auto cache = std::make_unique<PgUserCache>(*this);
    cache->update_from_master();
    return cache;
}

PgUserManager::UserDBInfo PgUserManager::get_user_database() const
{
    // A lock is needed to ensure both the db and version number are from the same update.
    Guard guard(m_userdb_lock);
    return UserDBInfo{m_userdb, m_userdb_version.load(relaxed)};
}

int PgUserManager::userdb_version() const
{
    return m_userdb_version.load(acquire);
}

json_t* PgUserManager::users_to_json() const
{
    SUserDB ptr_copy;
    {
        Guard guard(m_userdb_lock);
        ptr_copy = m_userdb;
    }
    return ptr_copy->users_to_json();
}

bool PgUserManager::update_users()
{
    return true;
}

json_t* PgUserDatabase::users_to_json() const
{
    auto rval = json_array();
    return rval;
}

PgUserCache::PgUserCache(const PgUserManager& master)
    : m_master(master)
{
}

void PgUserCache::update_from_master()
{
    if (m_userdb_version < m_master.userdb_version())
    {
        // Master db has updated data, copy the shared pointer.
        auto db_info = m_master.get_user_database();
        m_userdb = std::move(db_info.user_db);
        m_userdb_version = db_info.version;
    }
}

bool PgUserCache::can_update_immediately() const
{
    // Same as with MariaDB.
    return m_userdb_version < m_master.userdb_version() || m_master.can_update_immediately();
}

int PgUserCache::version() const
{
    return m_userdb_version;
}
