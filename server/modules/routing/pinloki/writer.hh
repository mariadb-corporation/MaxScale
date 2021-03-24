/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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
    // Used to generate the connection details used for replication
    using Generator = std::function<maxsql::Connection::ConnectionDetails()>;

    Writer(Generator generator, mxb::Worker* worker, InventoryWriter* inv);
    ~Writer();
    void run();

    // These are thread safe on their own, but can be inconsistent as a group.
    mxq::GtidList get_gtid_io_pos() const;
    Error         get_err() const;

private:
    Generator         m_generator;
    mxb::Worker*      m_worker;
    InventoryWriter&  m_inventory;
    bool              m_is_bootstrap = false;
    bool              m_commit_on_query = false;
    maxsql::GtidList  m_current_gtid_list;
    std::atomic<bool> m_running {true};
    std::thread       m_thread;
    maxbase::Timer    m_timer {10s};
    Error             m_error;

    mutable std::mutex              m_lock;
    mutable std::condition_variable m_cond;

    void save_gtid_list(FileWriter& writer);
    void update_gtid_list(const mxq::Gtid& gtid);
    void start_replication(maxsql::Connection& conn);
    bool has_master_changed(const maxsql::Connection& conn);

    mxq::Connection::ConnectionDetails get_connection_details();
};
}
