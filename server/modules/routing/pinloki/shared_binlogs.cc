/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
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

    // TODO m_binlog_map grows without bounds so eviction
    // is needed. Once the strategy, or strategies, for when
    // compression vs purging happens, things will be clear.

    // TODO does this sort of sharing become more efficient if
    // TempFile uses an fd and __gnu_cxx::stdio_filebuf (doubt it).

    return ret;
}
}
