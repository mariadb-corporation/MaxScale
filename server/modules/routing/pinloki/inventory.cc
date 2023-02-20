/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "inventory.hh"
#include "config.hh"

#include <maxbase/filesystem.hh>

#include <fstream>
#include <algorithm>
#include <unistd.h>

namespace pinloki
{

namespace
{
std::vector<std::string> read_inventory_file(const Config& config)
{
    return mxb::strtok(mxb::load_file<std::string>(config.inventory_file_path()).first, "\n");
}

maxsql::GtidList read_rpl_state(const Config& config)
{
    std::string ret;
    if (auto ifs = std::ifstream(config.gtid_file_path()))
    {
        ifs >> ret;
    }

    return maxsql::GtidList::from_string(ret);
}

maxsql::GtidList read_requested_rpl_state(const Config& config)
{
    std::string ret;
    if (auto ifs = std::ifstream(config.requested_gtid_file_path()))
    {
        ifs >> ret;
    }

    return maxsql::GtidList::from_string(ret);
}

void save_gtid(const maxsql::GtidList& gtids, const std::string& filename)
{
    if (auto err = mxb::save_file(filename, gtids.to_string()); !err.empty())
    {
        MXB_THROW(BinlogWriteError, err);
    }
}
}

InventoryWriter::InventoryWriter(const Config& config)
    : m_config(config)
{
}

void InventoryWriter::configure()
{
    m_file_names = read_inventory_file(m_config);
}

void InventoryWriter::push_back(const std::string& file_name)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    m_file_names.push_back(m_config.path(file_name));
    persist();
}

void InventoryWriter::pop_front(const std::string& file_name)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (file_name != m_file_names.front())
    {
        // This can happen if two users issue purge commands at the same time,
        // in addition there is the timeout based purging as well.
        // Not a problem so just an info message. Both (or all) purges will
        // finish successfully.
        MXB_SINFO("pop_front " << file_name << "does not match front " << file_name);
    }
    else
    {
        m_file_names.erase(m_file_names.begin());
        persist();
    }
}

void InventoryWriter::persist()
{
    auto err = mxb::save_file(m_config.inventory_file_path(), mxb::join(m_file_names, "\n"));

    if (!err.empty())
    {
        MXB_ERROR("%s", err.c_str());
    }
}

std::vector<std::string> InventoryWriter::file_names() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_file_names;
}

void InventoryWriter::save_rpl_state(const maxsql::GtidList& gtids)
{
    save_gtid(gtids, m_config.gtid_file_path());
}

maxsql::GtidList InventoryWriter::rpl_state() const
{
    return read_rpl_state(m_config);
}

void InventoryWriter::save_requested_rpl_state(const maxsql::GtidList& gtids)
{
    save_gtid(gtids, m_config.requested_gtid_file_path());
}

void InventoryWriter::clear_requested_rpl_state() const
{
    remove(m_config.requested_gtid_file_path().c_str());
}

maxsql::GtidList InventoryWriter::requested_rpl_state() const
{
    return read_requested_rpl_state(m_config);
}

void InventoryWriter::set_master_id(int64_t id)
{
    m_master_id.store(id, std::memory_order_release);
}

int64_t InventoryWriter::master_id() const
{
    return m_master_id.load(std::memory_order_acquire);
}

void InventoryWriter::set_is_writer_connected(bool connected)
{
    m_is_writer_connected.store(connected, std::memory_order_release);
}

bool InventoryWriter::is_writer_connected() const
{
    return m_is_writer_connected.load(std::memory_order_acquire);
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

InventoryReader::InventoryReader(const Config& config)
    : m_config(config)
{
}

const std::vector<std::string>& InventoryReader::file_names() const
{
    // file reading can be improved, but the file is small
    // and this function called seldomly
    return m_file_names = read_inventory_file(m_config);
}

maxsql::GtidList InventoryReader::rpl_state() const
{
    return read_rpl_state(m_config);
}
}
