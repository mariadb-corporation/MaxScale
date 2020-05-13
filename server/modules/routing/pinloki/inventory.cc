/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "inventory.hh"
#include "config.hh"

#include <fstream>
#include <algorithm>

namespace pinloki
{

Inventory::Inventory(const Config& config)
    : m_config(config)
{
    std::ifstream ifs(m_config.inventory_file_path());

    while (ifs.good())
    {
        std::string name;
        ifs >> name;
        if (ifs.good())
        {
            m_file_names.push_back(name);
        }
    }
}

void Inventory::add(const std::string& file_name)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_file_names.push_back(m_config.path(file_name));
    persist();
}

void Inventory::remove(const std::string& file_name)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    std::string full_name = m_config.path(file_name);
    m_file_names.erase(std::remove(m_file_names.begin(), m_file_names.end(), full_name), m_file_names.end());
    persist();
}

void Inventory::persist()
{
    std::string tmp = m_config.inventory_file_path() + ".tmp";
    std::ofstream ofs(tmp, std::ios_base::trunc);

    for (const auto& file : m_file_names)
    {
        ofs << file << '\n';
    }

    rename(tmp.c_str(), m_config.inventory_file_path().c_str());
}

std::vector<std::string> Inventory::file_names() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_file_names;
}

int Inventory::count() const
{
    return m_file_names.size();
}

bool Inventory::is_listed(const std::string& file_name) const
{
    std::string full_name = m_config.path(file_name);
    std::unique_lock<std::mutex> lock(m_mutex);
    return std::find(begin(m_file_names), end(m_file_names), full_name) != end(m_file_names);
}

bool Inventory::exists(const std::string& file_name) const
{
    std::string full_name = m_config.path(file_name);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!is_listed(full_name))
    {
        return false;
    }

    std::ifstream ofs(full_name);
    return ofs.good();
}
}
