/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <memory>
#include <string>
#include <vector>

#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/mysql_binlog.h>

/**
 * A result set consisting of VARCHAR(255) columns
 */
class ResultSet
{
public:

    /**
     * Create a new result set
     *
     * @param names List of column names
     *
     * @return The new result set
     */
    static std::unique_ptr<ResultSet> create(const std::vector<std::string>& names);

    /**
     * Add a row to the result set
     *
     * @param values List of values for the row
     */
    void add_row(const std::vector<std::string>& values);

    /**
     * Convert the resultset into its raw binary form
     *
     * @return The resultset as a mxs::Buffer
     */
    mxs::Buffer as_buffer() const;

private:
    std::vector<std::string>              m_columns;
    std::vector<std::vector<std::string>> m_rows;

    ResultSet(const std::vector<std::string>& names);
};
