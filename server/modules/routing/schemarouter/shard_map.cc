/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "shard_map.hh"

#include <algorithm>

Shard::Shard()
    : m_map(std::make_shared<ServerMap>())
    , m_targets(std::make_shared<TargetSet>())
    , m_last_updated(time(NULL))
{
}

Shard::~Shard()
{
}

void Shard::add_location(std::string db, std::string table, mxs::Target* target)
{
    mxb_assert(m_map.unique());
    (*m_map)[std::move(db)][std::move(table)].insert(target);
    m_targets->insert(target);
}

void Shard::add_statement(std::string stmt, mxs::Target* target)
{
    stmt_map[stmt] = target;
}

void Shard::add_statement(uint32_t id, mxs::Target* target)
{
    MXB_DEBUG("ADDING ID: [%u] server: [%s]", id, target->name());
    m_binary_map[id] = target;
}

std::set<mxs::Target*> Shard::get_all_locations(mxs::Parser::TableName name)
{
    return get_all_locations(std::string(name.db), std::string(name.table));
}

std::set<mxs::Target*> Shard::get_all_locations(std::string db, std::string tbl)
{
    std::set<mxs::Target*> rval;
    std::transform(db.begin(), db.end(), db.begin(), ::tolower);
    std::transform(tbl.begin(), tbl.end(), tbl.begin(), ::tolower);

    auto db_it = m_map->find(db);

    if (db_it != m_map->end())
    {
        auto tbl_it = db_it->second.find(tbl);

        if (tbl_it != db_it->second.end())
        {
            rval = tbl_it->second;
        }
    }

    return rval;
}

std::set<mxs::Target*> Shard::get_all_locations(const std::vector<mxs::Parser::TableName>& tables)
{
    if (tables.empty())
    {
        return {};
    }

    auto it = tables.begin();
    std::set<mxs::Target*> targets = get_all_locations(*it++);

    for (; it != tables.end(); ++it)
    {
        std::set<mxs::Target*> right = get_all_locations(*it);
        std::set<mxs::Target*> left;
        left.swap(targets);
        std::set_intersection(right.begin(), right.end(), left.begin(), left.end(),
                              std::inserter(targets, targets.end()));
    }

    return targets;
}

mxs::Target* Shard::get_statement(std::string stmt)
{
    mxs::Target* rval = NULL;
    auto iter = stmt_map.find(stmt);
    if (iter != stmt_map.end())
    {
        rval = iter->second;
    }
    return rval;
}

mxs::Target* Shard::get_statement(uint32_t id)
{
    mxs::Target* rval = NULL;
    BinaryPSMap::iterator iter = m_binary_map.find(id);
    if (iter != m_binary_map.end())
    {
        rval = iter->second;
    }
    return rval;
}

bool Shard::remove_statement(std::string stmt)
{
    return stmt_map.erase(stmt);
}

bool Shard::remove_statement(uint32_t id)
{
    return m_binary_map.erase(id);
}

bool Shard::stale(double max_interval) const
{
    time_t now = time(NULL);

    return difftime(now, m_last_updated) > max_interval;
}

void Shard::invalidate()
{
    m_last_updated = 0;
}

bool Shard::empty() const
{
    return m_map->size() == 0;
}

const ServerMap& Shard::get_content() const
{
    return *m_map;
}

bool Shard::uses_target(mxs::Target* target) const
{
    return m_targets->count(target);
}

bool Shard::newer_than(const Shard& shard) const
{
    return m_last_updated > shard.m_last_updated;
}

ShardManager::ShardManager()
{
}

ShardManager::~ShardManager()
{
}

Shard ShardManager::get_shard(std::string user, double max_interval)
{
    std::lock_guard<std::mutex> guard(m_lock);

    ShardMap::iterator iter = m_maps.find(user);

    if (iter == m_maps.end())
    {
        // No previous shard
        ++m_stats.misses;
        return Shard();
    }
    else if (iter->second.stale(max_interval))
    {
        // Stale shard
        ++m_stats.stale;
        return Shard();
    }

    // Found valid shard
    ++m_stats.hits;
    return iter->second;
}

Shard ShardManager::get_stale_shard(std::string user, double max_interval, double max_staleness)
{
    std::lock_guard<std::mutex> guard(m_lock);

    ShardMap::iterator iter = m_maps.find(user);

    if (iter == m_maps.end() || iter->second.stale(max_interval + max_staleness))
    {
        // No previous shard or a completely stale shard
        if (iter != m_maps.end())
        {
            m_maps.erase(iter);
        }

        return Shard();
    }

    // Found valid shard
    return iter->second;
}

void ShardManager::update_shard(Shard& shard, const std::string& user)
{
    std::lock_guard<std::mutex> guard(m_lock);
    ShardMap::iterator iter = m_maps.find(user);

    if (iter == m_maps.end() || shard.newer_than(iter->second))
    {
        ++m_stats.updates;
        MXB_INFO("Updated shard map for user '%s'", user.c_str());
        m_maps[user] = shard;
    }

    mxb_assert(m_limits[user] > 0);
    --m_limits[user];
}

void ShardManager::clear()
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_maps.clear();
}

void ShardManager::invalidate()
{
    std::lock_guard<std::mutex> guard(m_lock);

    for (auto& [_, shard] : m_maps)
    {
        shard.invalidate();
    }
}

void ShardManager::set_update_limit(int64_t limit)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_update_limit = limit;
}

bool ShardManager::start_update(const std::string& user)
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_limits[user] < m_update_limit)
    {
        ++m_limits[user];
        rval = true;
    }

    return rval;
}

void ShardManager::cancel_update(const std::string& user)
{
    std::lock_guard<std::mutex> guard(m_lock);
    mxb_assert(m_limits[user] > 0);
    --m_limits[user];
}
