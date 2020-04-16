#pragma once

#include <maxbase/exception.hh>
#include "worker.hh"
#include "file_reader.hh"

namespace pinloki
{

class Reader : public Worker
{
public:
    Reader(const maxsql::Gtid& gtid);
private:
    static uint32_t epoll_update(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
    void            notify_concrete_reader(uint32_t events);
    void            handle_messages();

    MXB_POLL_DATA m_reader_poll_data;
    FileReader    m_file_reader;
};
}
