/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "reader.hh"

#include <sys/epoll.h>
#include <maxbase/hexdump.hh>
#include <maxbase/log.hh>
#include <maxscale/routingworker.hh>

#include <iostream>
#include <iomanip>

namespace pinloki
{

Reader::Pollable::Pollable(Reader* reader, int fd)
    : reader(reader)
    , fd(fd)
{
}

Reader::Reader(SendCallback cb, WorkerCallback worker_cb,
               AbortCallback abort_cb,
               const Config& conf,
               const maxsql::GtidList& start_gl,
               const std::chrono::seconds& heartbeat_interval)
    : mxb::Worker::Callable(&worker_cb())
    , m_send_callback(cb)
    , m_get_worker(worker_cb)
    , m_abort_cb(abort_cb)
    , m_inventory(conf)
    , m_start_gtid_list(start_gl)
    , m_find_gtid_fut(std::async(std::launch::async,
                                 find_gtid_position,
                                 std::ref(m_start_gtid_list.gtids()),
                                 std::ref(m_inventory.config())))
    , m_heartbeat_interval(heartbeat_interval)
    , m_last_event(std::chrono::steady_clock::now())
    , m_ref(std::make_shared<bool>(true))
{
}

bool Reader::start()
{
    bool continue_poll = true;

    if (m_find_gtid_fut.wait_for(10ms) == std::future_status::ready)
    {
        continue_poll = false;
        try
        {
            m_catch_up = m_find_gtid_fut.get();
            for (const auto& gpos : m_catch_up)
            {
                if (gpos.file_name.empty())
                {
                    MXB_SWARNING("Domain of requested gtid "
                                 << gpos.gtid << " not in binlog file. Assuming domain"
                                                 " will start in the current file ("
                                 << m_catch_up.front().file_name << ") or a later file");
                }
                else
                {
                    break;
                }
            }
            sync_to_primary();
        }
        catch (const GtidSearchTimeout& ex)
        {
            MXB_ERROR("%s", ex.what());
            m_abort_cb();
        }
        catch (const mxb::Exception& err)
        {
            MXB_ERROR("Failed in startup: %s", err.what());
            m_abort_cb();
        }
    }

    if (continue_poll)
    {
        if (!m_start_poll_dcid)
        {
            m_start_poll_dcid = dcall(100ms, &Reader::start, this);
        }
    }
    else
    {
        m_start_poll_dcid = 0;
    }

    return continue_poll;
}

bool Reader::sync_to_primary()
{
    bool continue_poll = true;
    auto gtid_list = m_inventory.config().rpl_state();

    if (gtid_list.is_included(m_start_gtid_list))
    {
        if (m_sync_poll_dcid)
        {
            MXB_SINFO("ReplSYNC: Primary synchronized, start file_reader at " << m_start_gtid_list);
        }

        try
        {
            start_file_reader();
            continue_poll = false;
        }
        catch (const mxb::Exception& err)
        {
            MXB_ERROR("Failed to start reading: %s", err.what());
            m_abort_cb();
        }
    }
    else if (m_timer.alarm())
    {
        MXB_SINFO("ReplSYNC: Reader waiting for primary to sync. "
                  << "primary: " << gtid_list << ", replica: " << m_start_gtid_list);
    }

    if (continue_poll)
    {
        if (!m_sync_poll_dcid)
        {
            m_sync_poll_dcid = dcall(1000ms, &Reader::sync_to_primary, this);
        }
    }
    else
    {
        m_sync_poll_dcid = 0;
    }

    return continue_poll;
}

void Reader::start_file_reader()
{
    m_sFile_reader.reset(new FileReader(m_catch_up, &m_inventory));
    m_reader_poll_data.reader = this;
    m_reader_poll_data.fd = m_sFile_reader->fd();
    m_get_worker().add_pollable(EPOLLIN, &m_reader_poll_data);

    send_events();

    if (m_heartbeat_interval.count())
    {
        m_heartbeat_dcid = dcall(1000ms, &Reader::generate_heartbeats, this);
    }
}

Reader::~Reader()
{
    cancel_dcalls(false);
}

void Reader::set_in_high_water(bool in_high_water)
{
    m_in_high_water = in_high_water;
}

uint32_t Reader::epoll_update(class mxb::Pollable* data, mxb::Worker* worker, uint32_t events)
{
    Reader* self = static_cast<Pollable*>(data)->reader;
    self->notify_concrete_reader(events);

    return 0;
}

void Reader::notify_concrete_reader(uint32_t events)
{
    m_sFile_reader->fd_notify(events);
    send_events();
}

void Reader::send_events()
{
    try
    {
        maxsql::RplEvent event;
        maxbase::Timer timer(1ms);
        while (!m_in_high_water
               && timer.until_alarm() != mxb::Duration::zero()
               && (event = m_sFile_reader->fetch_event(timer)))
        {
            m_send_callback(event);
            m_last_event = maxbase::Clock::now();
        }

        if (timer.alarm())
        {
            auto callback = [this, ref = get_ref()]() {
                if (auto r = ref.lock())
                {
                    send_events();
                }
            };

            m_get_worker().execute(callback, mxs::RoutingWorker::EXECUTE_QUEUED);
        }
    }
    catch (const std::exception& err)
    {
        MXB_ERROR("Binlog error: %s", err.what());
        m_abort_cb();
    }
}

bool Reader::generate_heartbeats()
{
    try
    {
        m_sFile_reader->check_status();

        auto now = maxbase::Clock::now();

        // Only send heartbeats if the connection is idle
        if (!m_in_high_water
            && now - m_last_event >= m_heartbeat_interval)
        {
            m_send_callback(m_sFile_reader->create_heartbeat_event());
            m_last_event = now;
        }

        return true;
    }
    catch (const std::exception& err)
    {
        MXB_ERROR("Binlog error: %s", err.what());
        m_abort_cb();
    }

    return false;
}
}
