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
