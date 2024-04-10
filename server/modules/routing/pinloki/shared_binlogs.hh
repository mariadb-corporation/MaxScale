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
#pragma once

#include "binlog_file.hh"
#include <map>

namespace pinloki
{

/**
 * @brief SharedBinlogFile allows clients to share BinlogFile:s, in effect
 *        sharing the result of ongoing decompression.
 */
class SharedBinlogFile final
{
public:
    SharedBinlogFile() = default;
    SharedBinlogFile(SharedBinlogFile&&) = delete;

    // Works the same way as instantiating a BinlogFile directly, but returns
    // a shared_ptr to an existing one if one happens to be available.
    std::shared_ptr<BinlogFile> binlog_file(const std::string& file_name) const;

private:
    using BinlogMap = std::map<std::string, std::weak_ptr<BinlogFile>>;

    mutable std::mutex m_binlog_mutex;
    mutable BinlogMap  m_binlog_map;
};
}
