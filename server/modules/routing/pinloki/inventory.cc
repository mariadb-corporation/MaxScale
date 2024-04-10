/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

std::vector<std::string> InventoryWriter::file_names() const
{
    return m_config.binlog_file_names();
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
}
