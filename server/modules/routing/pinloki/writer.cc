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

#include "writer.hh"
#include "config.hh"
#include "file_writer.hh"
#include "inventory.hh"
#include "pinloki.hh"
#include <maxbase/hexdump.hh>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <assert.h>

#include <assert.h>
#include <mariadb_rpl.h>

using namespace std::literals::string_literals;

// TODO multidomain is not handled, except for the state of replication (or m_current_gtid_list).
//      Incidentally this works with multidomain, as long as the master and any new master have
//      the same exact binlogs.
namespace pinloki
{

Writer::Writer(const maxsql::Connection::ConnectionDetails& details, Inventory* inv)
    : m_inventory(*inv)
    , m_current_gtid_list(mxq::GtidList::from_string(m_inventory.config().boot_strap_gtid_list()))
    , m_details(details)
{
    m_thread = std::thread(&Writer::run, this);
}

Writer::~Writer()
{
    m_running = false;
    m_thread.join();
}

void Writer::run()
{
    while (m_running)
    {
        try
        {
            FileWriter file(&m_inventory);
            mxq::Connection conn(m_details);
            conn.start_replication(m_inventory.config().server_id(), m_current_gtid_list);

            while (m_running)
            {
                auto rpl_msg = conn.get_rpl_msg();
                const auto& rpl_event = rpl_msg.event();
                MXB_SDEBUG(rpl_msg);

                switch (rpl_event.event_type)
                {
                case GTID_EVENT:
                    {
                        save_gtid_list();

                        auto& egtid = rpl_event.event.gtid;
                        auto gtid = maxsql::Gtid(egtid.domain_id, rpl_event.server_id, egtid.sequence_nr);
                        m_current_gtid_list.replace(gtid);
                    }
                    break;

                // TODO, which events can be commits?
                case QUERY_EVENT:
                case XID_EVENT:
                    save_gtid_list();
                    break;

                default:
                    break;
                }

                file.add_event(rpl_msg);
            }
        }
        catch (const std::exception& x)
        {
            MXS_ERROR("%s", x.what());
        }
    }
}

void Writer::save_gtid_list()
{
    if (m_current_gtid_list.is_valid())
    {
        std::ofstream ofs(m_inventory.config().gtid_file_path());
        ofs << m_current_gtid_list;
        // m_current_gtid_list.clear(); TODO change of logic after gitid => gtid list change
    }
}
}
