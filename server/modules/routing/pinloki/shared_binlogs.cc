/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#include "shared_binlogs.hh"

namespace pinloki
{

std::shared_ptr<BinlogFile> SharedBinlogFile::binlog_file(const std::string& file_name) const
{
    std::shared_ptr<BinlogFile> ret;
    std::lock_guard<std::mutex> lock(m_binlog_mutex);

    auto ite = m_binlog_map.find(file_name);
    if (ite != end(m_binlog_map))
    {
        ret = ite->second.lock();
        if (!ret)
        {
            m_binlog_map.erase(ite);
        }
    }

    if (!ret)
    {
        ret = std::make_shared<BinlogFile>(file_name);
        m_binlog_map.emplace(file_name, ret);
    }

    // Simple eviction. The m_binlog_map contains just std::weak_ptr:s, so is small.
    // In normal cases it is unlikely that many binlog files are in use, so this runs
    // rarely and is fast.
    if (m_binlog_map.size() > 100)
    {
        auto ite2 = begin(m_binlog_map);
        while (ite2 != end(m_binlog_map))
        {
            if (ite2->second.expired())
            {
                ite2 = m_binlog_map.erase(ite2);
            }
            else
            {
                ++ite2;
            }
        }
    }

    return ret;
}
}
