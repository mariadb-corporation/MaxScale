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

#include "gtid.hh"
#include <maxbase/exception.hh>
#include "worker.hh"
#include "rpl_event.hh"

#include <string>
#include <fstream>
#include <vector>
#include <array>
#include <functional>

namespace pinloki
{
/**
 * @brief FileReader - Provide events from files starting at a given Gtid. Once all events
 *                     have been read, FileReader sets up inotify-epoll notifications for
 *                     changes to the last (active) file.
 */
class FileReader    // : public Storage
{
public:
    FileReader(const maxsql::Gtid& gtid);
    maxsql::RplEvent fetch_event();

    /**
     * @brief fd - file descriptor that this reader want's to epoll
     * @return file descriptor
     */
    int fd();

    /**
     * @brief fd_notify - the Worker calls this when the file descriptor fd()
     *                    has events (what events? TODO need to provide that as well).
     * @param events
     */
    void fd_notify(uint32_t events);
private:
    struct ReadPosition
    {
        std::string   name;
        std::ifstream file;
        int           next_pos;
    };

    void open(const std::string& file_name);
    void set_inotify_fd();

    int          m_inotify_fd;
    int          m_inotify_descriptor = -1;
    ReadPosition m_read_pos;
    std::string  m_inotify_file;
    uint32_t     m_server_id;
};
}
