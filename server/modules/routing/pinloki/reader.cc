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

Reader::Reader(const Inventory* inv, mxb::Worker* worker, const maxsql::Gtid& gtid)
    : m_reader_poll_data(this, worker)
    , m_file_reader(gtid, inv)
    , m_worker(worker)
{
    m_worker->add_fd(m_file_reader.fd(), EPOLLIN, &m_reader_poll_data);
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
    maxsql::RplEvent ev;
    for (;;)
    {
        auto ev = m_file_reader.fetch_event();
        if (!ev.is_empty())
        {
            maxbase::hexdump(std::cout, ev.pHeader(), ev.pEnd() - ev.pHeader());

            std::cout << maxsql::dump_rpl_msg(ev, maxsql::Verbosity::All) << '\n';
        }
    }
}
}
