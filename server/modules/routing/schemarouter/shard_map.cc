/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "shard_map.hh"

#include <algorithm>

#include <maxbase/alloc.h>

Shard::Shard()
    : m_last_updated(time(NULL))
{
}

Shard::~Shard()
{
}

void Shard::add_location(std::string db, mxs::Target* target)
{
    m_map.emplace(db, target);
}

void Shard::add_statement(std::string stmt, mxs::Target* target)
{
    stmt_map[stmt] = target;
}

void Shard::add_statement(uint32_t id, mxs::Target* target)
{
    MXS_DEBUG("ADDING ID: [%u] server: [%s]", id, target->name());
    m_binary_map[id] = target;
}

std::set<mxs::Target*> Shard::get_all_locations(const std::vector<std::string>& tables)
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

std::set<mxs::Target*> Shard::get_all_locations(std::string table)
{
    std::set<mxs::Target*> rval;
    std::transform(table.begin(), table.end(), table.begin(), ::tolower);
    bool db_only = table.find(".") == std::string::npos;

    for (const auto& a : m_map)
    {
        std::string db = db_only ? a.first.substr(0, a.first.find(".")) : a.first;

        if (db == table)
        {
            rval.insert(a.second);
        }
    }

    return rval;
}

mxs::Target* Shard::get_location(const std::vector<std::string>& tables)
{
    auto targets = get_all_locations(tables);
    return targets.empty() ? nullptr : *targets.begin();
}

mxs::Target* Shard::get_location(std::string table)
{
    auto targets = get_all_locations(table);
    return targets.empty() ? nullptr : *targets.begin();
}

mxs::Target* Shard::get_statement(std::string stmt)
{
    mxs::Target* rval = NULL;
    ServerMap::iterator iter = stmt_map.find(stmt);
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

bool Shard::empty() const
{
    return m_map.size() == 0;
}

void Shard::get_content(ServerMap& dest)
{
    for (ServerMap::iterator it = m_map.begin(); it != m_map.end(); it++)
    {
        dest.insert(*it);
    }
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

    if (iter == m_maps.end() || iter->second.stale(max_interval))
    {
        // No previous shard or a stale shard, construct a new one

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
        MXS_INFO("Updated shard map for user '%s'", user.c_str());
        m_maps[user] = shard;
    }

    mxb_assert(m_limits[user] > 0);
    --m_limits[user];
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
