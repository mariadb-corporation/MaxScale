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

#include "reader.hh"

#include <maxbase/hexdump.hh>

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

Reader::Reader(Callback cb, const Inventory* inv, mxb::Worker* worker, const maxsql::Gtid& gtid)
    : m_cb(cb)
    , m_reader_poll_data(this, worker)
    , m_file_reader(gtid, inv)
    , m_worker(worker)
{
    m_worker->add_fd(m_file_reader.fd(), EPOLLIN, &m_reader_poll_data);
    handle_messages();
}

Reader::~Reader()
{
    if (m_dcid)
    {
        m_worker->cancel_delayed_call(m_dcid);
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
    m_file_reader.fd_notify(events);
    handle_messages();
}

void Reader::handle_messages()
{
    if (m_dcid == 0)
    {
        while ((m_event = m_file_reader.fetch_event()))
        {
            if (!m_cb(m_event))
            {
                // Note: This is a very crude, albeit simple, form of flow control. Installing event handlers
                // that deal with the outbound network buffer being full would be far more efficient.
                m_dcid = m_worker->delayed_call(10, &Reader::resend_event, this);
                break;
            }
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
}
