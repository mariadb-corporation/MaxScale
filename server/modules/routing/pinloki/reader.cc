/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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

// TODO The reader is single domain. Keep it that way until most other things
// are in place. I have a feeling, that instantiating one reader per domain
// will be the cleanest implementation. See comments in file_reader.cc

// This is setup for a single slave/reader for testing, PinlokiSession will actually instantiate Readers
namespace pinloki
{

Reader::PollData::PollData(Reader* reader, mxb::Worker* worker)
    : mxb::POLL_DATA{Reader::epoll_update, worker}
    , reader(reader)
{
}

Reader::Reader(SendCallback cb, WorkerCallback worker_cb,
               const Config& conf,
               const maxsql::GtidList& start_gl,
               const std::chrono::seconds& heartbeat_interval)
    : m_send_callback(cb)
    , m_get_worker(worker_cb)
    , m_inventory(conf)
    , m_start_gtid_list(start_gl)
    , m_heartbeat_interval(heartbeat_interval)
    , m_last_event(std::chrono::steady_clock::now())
{
}

void Reader::start()
{
    auto gtid_list = m_inventory.rpl_state();

    if (gtid_list.is_included(m_start_gtid_list))
    {
        start_reading();
    }
    else
    {
        MXB_SINFO("ReplSYNC: reader waiting for primary to synchronize "
                  << "primary: " << gtid_list << ", replica: " << m_start_gtid_list);
        m_startup_poll_dcid = m_get_worker().delayed_call(1000, &Reader::poll_start_reading, this);
    }
}

void Reader::start_reading()
{
    m_sFile_reader.reset(new FileReader(m_start_gtid_list, &m_inventory));
    m_reader_poll_data = PollData(this, &m_get_worker());
    m_get_worker().add_fd(m_sFile_reader->fd(), EPOLLIN, &m_reader_poll_data);

    send_events();

    if (m_heartbeat_interval.count())
    {
        m_heartbeat_dcid = m_get_worker().delayed_call(1000, &Reader::generate_heartbeats, this);
    }
}

bool Reader::poll_start_reading(mxb::Worker::Call::action_t action)
{
    // This version waits for ever.
    // Is there reason to timeout and send an error message?
    bool continue_poll = true;
    if (action == mxb::Worker::Call::EXECUTE)
    {
        auto gtid_list = m_inventory.rpl_state();
        if (gtid_list.is_included(maxsql::GtidList({m_start_gtid_list})))
        {
            MXB_SINFO("ReplSYNC: Primary synchronized, start file_reader");

            try
            {
                start_reading();
                continue_poll = false;
            }
            catch (const mxb::Exception& err)
            {
                MXS_ERROR("Failed to start reading: %s", err.what());
            }
        }
        else
        {
            if (m_timer.alarm())
            {
                MXB_SINFO("ReplSYNC: Reader waiting for primary to sync. "
                          << "primary: " << gtid_list << ", replica: " << m_start_gtid_list);
            }
        }
    }

    if (!continue_poll)
    {
        m_startup_poll_dcid = 0;
    }

    return continue_poll;
}

Reader::~Reader()
{
    if (m_startup_poll_dcid)
    {
        m_get_worker().cancel_delayed_call(m_startup_poll_dcid);
    }

    if (m_heartbeat_dcid)
    {
        m_get_worker().cancel_delayed_call(m_heartbeat_dcid);
    }
}

void Reader::set_in_high_water(bool in_high_water)
{
    m_in_high_water = in_high_water;
}

uint32_t Reader::epoll_update(struct mxb::POLL_DATA* data, mxb::WORKER* worker, uint32_t events)
{
    Reader* self = static_cast<PollData*>(data)->reader;
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
    maxsql::RplEvent event;
    maxbase::Timer timer(1ms);
    bool timer_alarm = false;
    while (!m_in_high_water && (event = m_sFile_reader->fetch_event()) && !(timer_alarm = timer.alarm()))
    {
        m_send_callback(event);
        m_last_event = maxbase::Clock::now();
    }

    if (timer_alarm)
    {
        auto callback = [this]() {
                send_events();
            };

        m_get_worker().execute(callback, mxs::RoutingWorker::EXECUTE_QUEUED);
    }
}

bool Reader::generate_heartbeats(mxb::Worker::Call::action_t action)
{
    auto now = maxbase::Clock::now();

    // Only send heartbeats if the connection is idle
    if (action == mxb::Worker::Call::EXECUTE
        && !m_in_high_water
        && now - m_last_event >= m_heartbeat_interval)
    {
        m_send_callback(m_sFile_reader->create_heartbeat_event());
        m_last_event = now;
    }

    return true;
}
}
