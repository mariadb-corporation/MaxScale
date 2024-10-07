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

#include "writer.hh"
#include "config.hh"
#include "file_writer.hh"
#include "inventory.hh"
#include "pinloki.hh"
#include "find_gtid.hh"
#include <maxbase/hexdump.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/threadpool.hh>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <assert.h>

#include <assert.h>
#include <mariadb_rpl.h>
using namespace std::chrono_literals;

using namespace std::literals::string_literals;

// TODO multidomain is not handled, except for the state of replication (or m_current_gtid_list).
//      Incidentally this works with multidomain, as long as the master and any new master have
//      the same exact binlogs.
namespace pinloki
{

Writer::Writer(const mxq::Connection::ConnectionDetails& details, InventoryWriter* inv)
    : m_inventory(*inv)
    , m_details(details)
{
    m_inventory.set_is_writer_connected(false);

    m_current_gtid_list = find_last_gtid_list(m_inventory);
    m_inventory.config().save_rpl_state(m_current_gtid_list);

    std::vector<maxsql::Gtid> gtids;
    auto req_state = m_inventory.requested_rpl_state();
    if (req_state.is_valid())
    {
        if (m_current_gtid_list.is_included(req_state))
        {
            MXB_SDEBUG("The requested gtid is already in the logs, removing request.");
            m_inventory.clear_requested_rpl_state();
        }
        else
        {
            m_current_gtid_list = req_state;
        }
    }

    std::lock_guard<std::mutex> guard(m_lock);
    m_thread = std::thread(&Writer::run, this);
    mxb::set_thread_name(m_thread, "Writer");
}

Writer::~Writer()
{
    m_running = false;
    m_cond.notify_one();
    m_thread.join();
}

void Writer::set_connection_details(const mxq::Connection::ConnectionDetails& details)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_details = details;
}

mxq::Connection::ConnectionDetails Writer::get_connection_details()
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_details;
}

mxq::GtidList Writer::get_gtid_io_pos() const
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_current_gtid_list;
}

Error Writer::get_err() const
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_error;
}

void Writer::update_gtid_list(const mxq::Gtid& gtid)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_current_gtid_list.replace(gtid);
}

void Writer::start_replication(mxq::Connection& conn)
{
    conn.start_replication(m_inventory.config().server_id(), m_current_gtid_list);
}

bool Writer::has_master_changed(const mxq::Connection& conn)
{
    auto details = get_connection_details();

    return conn.host() != details.host;
}

void Writer::run()
{
    std::unique_lock<std::mutex> guard(m_lock);
    guard.unlock();
    mxb::LogScope scope(m_inventory.config().name().c_str());
    bool log_host_warning = true;

    while (m_running)
    {
        std::string host = "<no host>";
        Error error;
        try
        {
            auto details = get_connection_details();

            {
                std::unique_lock<std::mutex> guard(m_lock);
                if (!details.host.is_valid())
                {
                    if (log_host_warning)
                    {
                        MXB_SWARNING("No (replication) master found. Retrying silently until one is found.");
                        log_host_warning = false;
                    }

                    m_cond.wait_for(guard, std::chrono::seconds(1), [this]() {
                        return !m_running;
                    });

                    continue;
                }
                m_error = Error {};
            }

            mxb::set_thread_name(m_thread, MAKE_STR(details.host.address() << ":Writer"));
            log_host_warning = true;

            FileWriter file(&m_inventory, *this);
            mxq::Connection conn(get_connection_details());
            start_replication(conn);
            std::ostringstream ss;
            ss << conn.host();
            host = ss.str();

            maxbase::Timer timer(1s);   // Check if the master has changed at the most once a second

            while (m_running)
            {
                auto rpl_event = maxsql::RplEvent(conn.get_rpl_msg());
                if (rpl_event.event_type() != HEARTBEAT_LOG_EVENT)
                {
                    MXB_SDEBUG("INCOMING " << rpl_event);
                }

                if (m_inventory.config().select_master() && timer.alarm() && has_master_changed(conn))
                {
                    MXB_INFO("Pinloki switching to new master at '%s'", host.c_str());
                    break;
                }

                m_inventory.set_master_id(rpl_event.server_id());
                m_inventory.set_is_writer_connected(true);
                bool do_save_gtid_list = false;

                switch (rpl_event.event_type())
                {
                case FORMAT_DESCRIPTION_EVENT:
                    if (!rpl_event.format_description().checksum)
                    {
                        MXB_THROW(BinlogWriteError,
                                  "Server at '" << host << "' is configured with binlog_checksum=NONE, "
                                                << "binlogrouter requires binlog_checksum=CRC32.");
                    }
                    break;

                case GTID_EVENT:
                    {
                        maxsql::GtidEvent gtid_event = rpl_event.gtid_event();
                        file.begin_txn();
                        update_gtid_list(gtid_event.gtid);

                        if (gtid_event.flags & mxq::F_STANDALONE)
                        {
                            m_commit_on_query = true;
                        }
                    }
                    break;

                case QUERY_EVENT:
                    if (m_commit_on_query)
                    {
                        do_save_gtid_list = true;
                        m_commit_on_query = false;
                    }
                    else if (rpl_event.is_commit())
                    {
                        do_save_gtid_list = true;
                    }
                    break;

                case XID_EVENT:
                    do_save_gtid_list = true;
                    break;

                default:
                    break;
                }

                file.add_event(rpl_event);
                if (do_save_gtid_list)
                {
                    save_gtid_list(file);
                }

                std::lock_guard<std::mutex> guard(m_lock);
                m_log_pos = rpl_event.next_event_pos();

                if (rpl_event.event_type() == ROTATE_EVENT)
                {
                    m_log_file = rpl_event.rotate().file_name;
                }
            }
        }
        catch (const maxsql::DatabaseError& x)
        {
            error = Error {x.code(), x.what()};
        }
        catch (const std::exception& x)
        {
            error = Error {-1, x.what()};
        }

        m_inventory.set_is_writer_connected(false);

        std::unique_lock<std::mutex> guard(m_lock);
        if (error.code)
        {
            m_error = error;
            if (m_timer.alarm())
            {
                MXS_SERROR("Error received during replication from '" << host << "': " << error.str);
            }

            m_cond.wait_for(guard, std::chrono::seconds(1), [this]() {
                return !m_running;
            });
        }
    }
}

void Writer::save_gtid_list(FileWriter& file_writer)
{
    if (m_current_gtid_list.is_valid())
    {
        file_writer.commit_txn();
        m_inventory.config().save_rpl_state(m_current_gtid_list);
    }
}

std::pair<std::string, uint32_t> Writer::master_log_pos() const
{
    std::lock_guard<std::mutex> guard(m_lock);
    return {m_log_file, m_log_pos};
}
}
