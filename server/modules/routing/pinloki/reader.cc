/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "reader.hh"

#include <maxbase/hexdump.hh>
#include <maxbase/log.hh>

#include <iostream>
#include <iomanip>

// TODO The reader is single domain. Keep it that way until most other things
// are in place. I have a feeling, that instantiating one reader per domain
// will be the cleanest implementation. See comments in file_reader.cc

// This is setup for a single slave/reader for testing, PinlokiSession will actually instantiate Readers
namespace pinloki
{

Reader::PollData::PollData(Reader* reader, mxb::Worker* worker)
    : MXB_POLL_DATA{Reader::epoll_update, worker}
    , reader(reader)
{
}

Reader::Reader(Callback cb, const Config& conf, mxb::Worker* worker, const maxsql::GtidList& start_gl,
               const std::chrono::seconds& heartbeat_interval)
    : m_cb(cb)
    , m_inventory(conf)
    , m_reader_poll_data(this, worker)
    , m_worker(worker)
    , m_start_gtid_list(start_gl)
    , m_heartbeat_interval(heartbeat_interval)
    , m_last_event(std::chrono::steady_clock::now())
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
        m_startup_poll_dcid = m_worker->delayed_call(1000, &Reader::poll_start_reading, this);
    }
}

void Reader::start_reading()
{
    m_sFile_reader.reset(new FileReader(m_start_gtid_list, &m_inventory));
    m_worker->add_fd(m_sFile_reader->fd(), EPOLLIN, &m_reader_poll_data);
    handle_messages();

    if (m_heartbeat_interval.count())
    {
        m_heartbeat_dcid = m_worker->delayed_call(1000, &Reader::generate_heartbeats, this);
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
            start_reading();
            continue_poll = false;
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
    if (m_dcid)
    {
        m_worker->cancel_delayed_call(m_dcid);
    }

    if (m_startup_poll_dcid)
    {
        m_worker->cancel_delayed_call(m_startup_poll_dcid);
    }

    if (m_heartbeat_dcid)
    {
        m_worker->cancel_delayed_call(m_heartbeat_dcid);
    }
}

uint32_t Reader::epoll_update(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    Reader* self = static_cast<PollData*>(data)->reader;
    self->notify_concrete_reader(events);

    return 0;
}

void Reader::notify_concrete_reader(uint32_t events)
{
    m_sFile_reader->fd_notify(events);
    handle_messages();
}

void Reader::handle_messages()
{
    if (m_dcid == 0)
    {
        while ((m_event = m_sFile_reader->fetch_event()))
        {
            if (!m_cb(m_event))
            {
                // Note: This is a very crude, albeit simple, form of flow control. Installing event handlers
                // that deal with the outbound network buffer being full would be far more efficient.
                m_dcid = m_worker->delayed_call(10, &Reader::resend_event, this);
                break;
            }

            m_last_event = std::chrono::steady_clock::now();
        }
    }
}

bool Reader::resend_event(mxb::Worker::Call::action_t action)
{
    bool call_again = false;

    if (action == mxb::Worker::Call::EXECUTE)
    {
        mxb_assert(m_event);

        // Try to process the event we failed to process earlier
        if (m_cb(m_event))
        {
            // Event successfully processed, try to continue event processing. Clearing out m_dcid before the
            // call allows handle_messages to install a new delayed call in case we have more data than we can
            // send in one go.
            m_dcid = 0;
            handle_messages();
        }
        else
        {
            // Event still cannot be processed, keep retrying.
            call_again = true;
        }
    }

    return call_again;
}

bool Reader::generate_heartbeats(mxb::Worker::Call::action_t action)
{
    auto now = std::chrono::steady_clock::now();

    // Only send heartbeats if the connection is idle and no data is buffered.
    if (action == mxb::Worker::Call::EXECUTE
        && now - m_last_event >= m_heartbeat_interval && m_dcid == 0)
    {
        m_cb(m_sFile_reader->create_heartbeat_event());
        m_last_event = now;
    }

    return true;
}
}
