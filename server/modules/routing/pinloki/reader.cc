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

// TODO The reader is single domain. Keep it that way until most other things
// are in place. I have a feeling, that instantiating one reader per domain
// will be the cleanest implementation. See comments in file_reader.cc

// This is setup for a single slave/reader for testing, PinlokiSession will actually instantiate Readers
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
    , m_heartbeat_interval(heartbeat_interval)
    , m_last_event(std::chrono::steady_clock::now())
    , m_ref(std::make_shared<bool>(true))
{
}

void Reader::start()
{
    /* Reader-as-a-seprate process. This and the other spot with
     * a comment "Reader-as-a-separate process", should be configurable
     * to use find_last_gtid_list() instead of config().rpl_state()
     * in order for the Readers to run without a Writer. Some other
     * code would need to change as well. See pinloki/test/main.cc.
     *
     * Alternatively, the Reader could
     * simply reply with an error if the requested gtid does not
     * (yet) exist, like the master does.
     */
    auto gtid_list = m_inventory.config().rpl_state();

    if (gtid_list.is_included(m_start_gtid_list))
    {
        try
        {
            start_reading();
        }
        catch (const mxb::Exception& err)
        {
            MXB_ERROR("Failed to start reading: %s", err.what());
            m_abort_cb();
        }
    }
    else
    {
        MXB_SINFO("ReplSYNC: reader waiting for primary to synchronize "
                  << "primary: " << gtid_list << ", replica: " << m_start_gtid_list);
        m_startup_poll_dcid = dcall(1000ms, &Reader::poll_start_reading, this);
    }
}

void Reader::start_reading()
{
    m_sFile_reader.reset(new FileReader(m_start_gtid_list, &m_inventory));
    m_reader_poll_data.reader = this;
    m_reader_poll_data.fd = m_sFile_reader->fd();
    m_get_worker().add_pollable(EPOLLIN, &m_reader_poll_data);

    send_events();

    if (m_heartbeat_interval.count())
    {
        m_heartbeat_dcid = dcall(1000ms, &Reader::generate_heartbeats, this);
    }
}

bool Reader::poll_start_reading()
{
    // This version waits for ever.
    // Is there reason to timeout and send an error message?

    /* Reader-as-a-seprate process. See comment in Reader::start() */
    bool continue_poll = true;
    auto gtid_list = m_inventory.config().rpl_state();
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
            MXB_ERROR("Failed to start reading: %s", err.what());
            m_abort_cb();
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
        cancel_dcall(m_startup_poll_dcid);
    }

    if (m_heartbeat_dcid)
    {
        cancel_dcall(m_heartbeat_dcid);
    }
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
