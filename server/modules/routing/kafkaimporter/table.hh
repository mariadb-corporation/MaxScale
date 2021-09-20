/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <mysql.h>
#include <vector>

namespace kafkaimporter
{

class Table final
{
public:
    Table& operator=(const Table&) = delete;
    Table(const Table&) = delete;

    Table& operator=(Table&&) noexcept;
    Table(Table&&) noexcept;

    Table(const std::string& table);
    ~Table();

    bool prepare(MYSQL* mysql);
    bool insert(const std::string& value);
    bool flush();

private:
    void free_values();

    std::string m_table;
    MYSQL_STMT* m_stmt {nullptr};

    // The values and lengths, fed directly to Connector/C
    std::vector<char*>    m_values;
    std::vector<uint64_t> m_lengths;
};
}
