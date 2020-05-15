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

#include <maxbase/exception.hh>
#include <maxbase/worker.hh>
#include "dbconnection.hh"
#include "gtid.hh"
#include "config.hh"
#include "inventory.hh"

#include <atomic>
#include <memory>
#include <thread>
#include <condition_variable>

namespace pinloki
{
// TODO rename to?
class Writer
{
public:
    // Used to generate the connection details used for replication
    using Generator = std::function<maxsql::Connection::ConnectionDetails()>;

    Writer(Generator generator, mxb::Worker* worker, Inventory* inv);
    ~Writer();
    void run();

    mxq::GtidList get_gtid_io_pos() const;

private:
    Generator         m_generator;
    mxb::Worker*      m_worker;
    Inventory&        m_inventory;
    bool              m_is_bootstrap = false;
    bool              m_commit_on_query = false;
    maxsql::GtidList  m_current_gtid_list;
    std::atomic<bool> m_running {true};
    std::thread       m_thread;

    mutable std::mutex              m_lock;
    mutable std::condition_variable m_cond;

    void save_gtid_list();
    void update_gtid_list(const mxq::Gtid& gtid);

    mxq::Connection::ConnectionDetails get_connection_details();
};
}
