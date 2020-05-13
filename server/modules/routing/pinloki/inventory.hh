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

#pragma once
#include "gtid.hh"

#include <string>
#include <vector>
#include <mutex>

#include "config.hh"


namespace pinloki
{

/**
 * @brief Simple MonoState to keep track of binlog Files. This maintains the same
 *        index file (default name binlog.index) as the server.
 */
class Inventory
{
public:
    Inventory(const Config& config);

    // Adds a file to the inventory
    void add(const std::string& file_name);

    // Removes a file from the inventory (the file itself is not removed)
    void remove(const std::string& file_name);

    std::vector<std::string> file_names() const;

    int count() const;


    // Return an fstream positioned at the
    // indicated gtid event. If the gtid is not found,
    // <returned_file>.isgood() == false (and tellg()==0).
    std::fstream find_gtid(const maxsql::Gtid& gtid) const;

    /**
     * @brief is_listed
     * @param file_name
     * @return true if file is listed in inventory
     */
    bool is_listed(const std::string& file_name) const;

    /**
     * @brief exists -
     * @param file_name
     * @return true if is_listed(), file exists and is readable.
     */
    bool exists(const std::string& file_name) const;

    const Config& config() const
    {
        return m_config;
    }

private:
    // Saves the file list on disk
    void persist();

    // The configuration used to create this inventory
    const Config& m_config;

    std::vector<std::string> m_file_names;
    mutable std::mutex       m_mutex;
};
}
