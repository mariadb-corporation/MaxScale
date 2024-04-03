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
#include "gtid.hh"

#include <maxbase/exception.hh>

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#include "config.hh"


namespace pinloki
{
DEFINE_EXCEPTION(BinlogWriteError);

/**
 * @brief List of binlog file names. Thread safe, writable inventory file.
 */
class InventoryWriter
{
public:
    InventoryWriter(const Config& config);
    InventoryWriter(const InventoryWriter&) = delete;
    InventoryWriter& operator=(const InventoryWriter&) = delete;

    /**
     * @brief file_names
     * @return the file names
     */
    std::vector<std::string> file_names() const;

    /** Requested replication state (set global gtid_slave_pos='a-b-c') */
    void             save_requested_rpl_state(const maxsql::GtidList& gtids);
    void             clear_requested_rpl_state() const;
    maxsql::GtidList requested_rpl_state() const;

    /** Set by the writer **/
    void set_master_id(int64_t id);

    /** Last known master ID */
    int64_t master_id() const;

    /** Is the writer connected */
    void set_is_writer_connected(bool connected);

    /** Is the writer connected */
    bool is_writer_connected() const;

    const Config& config() const
    {
        return m_config;
    }

    void configure();

private:
    // The configuration used to create this inventory
    const Config&        m_config;
    std::atomic<int64_t> m_master_id {0};
    std::atomic<bool>    m_is_writer_connected {false};
};

/**
 * @brief List of binlog file names for single-threaded readers, in
 *        any process.
 */
class InventoryReader
{
public:
    InventoryReader(const Config& config);

    const Config& config() const
    {
        return m_config;
    }

private:
    // The configuration used to create this inventory
    const Config& m_config;
};

// Return the string after str in a vector of unique strings, or empty if not found
std::string next_string(const std::vector<std::string>& strs, const std::string& str);
// Return the first string in vector or an empty string if the vector is empty
std::string first_string(const std::vector<std::string>& strs);
// Return the last string in vector or an empty string if the vector is empty
std::string last_string(const std::vector<std::string>& strs);
}
