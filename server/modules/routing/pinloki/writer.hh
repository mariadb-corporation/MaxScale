/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/exception.hh>
#include <maxbase/worker.hh>
#include <maxbase/stopwatch.hh>
#include "dbconnection.hh"
#include "gtid.hh"
#include "config.hh"
#include "inventory.hh"

#include <memory>
#include <thread>
#include <condition_variable>

using namespace std::chrono_literals;

namespace pinloki
{
class FileWriter;

struct Error
{
    int         code {0};
    std::string str {};
};

class Writer
{
public:
    Writer(const mxq::Connection::ConnectionDetails& details, InventoryWriter* inv);
    ~Writer();
    void run();

    // These are thread safe on their own, but can be inconsistent as a group.
    Error get_err() const;
    void  set_connection_details(
        const mxq::Connection::ConnectionDetails& details);
    std::pair<std::string, uint32_t> master_log_pos() const;

    // internal
    const mxq::GtidList& current_gtid_list() const;

private:
    InventoryWriter&  m_inventory;
    bool              m_is_bootstrap = false;
    bool              m_commit_on_query = false;
    maxsql::GtidList  m_current_gtid_list;
    std::atomic<bool> m_running {true};
    std::thread       m_thread;
    maxbase::Timer    m_timer {10s};
    Error             m_error;
    std::string       m_log_file;
    uint32_t          m_log_pos {0};

    mxq::Connection::ConnectionDetails m_details;

    mutable std::mutex              m_lock;
    mutable std::condition_variable m_cond;

    void save_gtid_list(FileWriter& writer);
    void update_gtid_list(const mxq::Gtid& gtid);
    void start_replication(maxsql::Connection& conn);
    bool has_master_changed(const maxsql::Connection& conn);

    mxq::Connection::ConnectionDetails get_connection_details();
};
}
