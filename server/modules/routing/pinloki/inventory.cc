/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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
    read_file();
}

void Inventory::read_file() const
{
    std::ifstream ifs(m_config.inventory_file_path());

    m_file_names.clear();
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

void Inventory::push_back(const std::string& file_name)
{
    m_file_names.push_back(m_config.path(file_name));
    persist();
}

void Inventory::pop_front(const std::string& file_name)
{
    if (file_name != m_file_names.front())
    {
        // This can happen if two users issue purge commands at the same time,
        // in addition there is the timeout based purging as well.
        // Not a problem so just an info message. Both (or all) purges will
        // finish succesfully.
        MXS_SINFO("pop_front " << file_name << "does not match front " << file_name);
    }
    else
    {
        m_file_names.erase(m_file_names.begin());
        persist();
    }
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
    // file reading can be improved, but the file is small
    // and this function called seldomly
    read_file();
    return m_file_names;
}

std::string next_string(const std::vector<std::string>& strs, const std::string& str)
{
    // search in reverse since the file is likely at the end of the vector
    auto rite = std::find(rbegin(strs), rend(strs), str);
    if (rite != rend(strs) && rite != rbegin(strs))
    {
        return *--rite;
    }
    else
    {
        return "";
    }
}

std::string first_string(const std::vector<std::string>& strs)
{
    if (strs.empty())
    {
        return "";
    }

    return strs.front();
}

std::string last_string(const std::vector<std::string>& strs)
{
    if (strs.empty())
    {
        return "";
    }

    return strs.back();
}
}
