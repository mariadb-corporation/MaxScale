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
#include "dbconnection.hh"
#include "gtid.hh"
#include "config.hh"
#include "inventory.hh"

#include <atomic>
#include <memory>
#include <thread>

namespace pinloki
{
// TODO rename to?
class Writer
{
public:
    Writer(const maxsql::Connection::ConnectionDetails& details, Inventory* inv);
    ~Writer();
    void run();

private:
    Inventory&                         m_inventory;
    bool                               m_is_bootstrap = false;
    maxsql::GtidList                   m_current_gtid_list;
    std::atomic<bool>                  m_running {true};
    mxq::Connection::ConnectionDetails m_details;
    std::thread                        m_thread;

    void save_gtid_list();
};
}
