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

Reader::Reader(const maxsql::Gtid& gtid)
    : m_reader_poll_data {epoll_update, this}
    , m_file_reader(gtid)
{
    add_fd(m_file_reader.fd(), EPOLLIN, &m_reader_poll_data);

    handle_messages();
}

uint32_t Reader::epoll_update(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    std::cout << "epoll_update\n";
    Reader* self = static_cast<Reader*>(worker);
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
