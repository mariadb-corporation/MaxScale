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


namespace pinloki
{

/**
 * @brief Simple MonoState to keep track of binlog Files. This maintains the same
 *        index file (default name binlog.index) as the server.
 */
class Inventory
{
public:
    Inventory();

    /**
     * @brief add
     * @param file_name
     */
    static void add(const std::string& file_name);

    static std::vector<std::string> file_names();

    static int count();


    // Return an fstream positioned at the
    // indicated gtid event. If the gtid is not found,
    // <returned_file>.isgood() == false (and tellg()==0).
    static std::fstream find_gtid(const maxsql::Gtid& gtid);

    /**
     * @brief is_listed
     * @param file_name
     * @return true if file is listed in inventory
     */
    static bool is_listed(const std::string& file_name);

    /**
     * @brief exists -
     * @param file_name
     * @return true if is_listed(), file exists and is readable.
     */
    bool exists(const std::string& file_name);
private:
    static std::vector<std::string> m_file_names;
    static std::mutex               m_mutex;
};
}
