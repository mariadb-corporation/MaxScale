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

#pragma once
#include "gtid.hh"

#include <string>
#include <vector>
#include <mutex>

#include "config.hh"


namespace pinloki
{

/**
 * @brief Inventory - list of binlogs from the binlog index file. Thread safe.
 */
class Inventory
{
public:
    Inventory(const Config& config);

    /**
     * @brief push a file name to the end of the list
     * @param file_name
     */
    void push(const std::string& file_name);

    /**
     * @brief pop the first file
     * @param file_name must match the name of the first file
     */
    void pop(const std::string& file_name);

    /**
     * @brief file_names
     * @return the file names
     */
    std::vector<std::string> file_names() const;

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

// Return the string after str in a vector of unique strings, or empty if not found
std::string next_string(const std::vector<std::string>& strs, const std::string& str);
// Return the first string in vector or an empty string if the vector is empty
std::string first_string(const std::vector<std::string>& strs);
// Return the last string in vector or an empty string if the vector is empty
std::string last_string(const std::vector<std::string>& strs);
}
