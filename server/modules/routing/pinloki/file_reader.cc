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

#include "pinloki.hh"
#include "config.hh"
#include "file_reader.hh"
#include "inventory.hh"
#include "find_gtid.hh"

#include <mysql.h>
#include <mariadb_rpl.h>
#include <array>
#include <iostream>
#include <iomanip>
#include <thread>
#include <sys/inotify.h>
#include <unistd.h>

using namespace std::literals::chrono_literals;

// TODO: case with no files. Can't setup inotify because the file name is not
//       known yet. Don't know if it can happen in a real system. It would mean
//       maxscale and slaves are brought up before the master is ever connected to.
//       FileReader's constructor could do nothing, and fetch would look for the file
//       and return an empty event if the file is not there yet. Meanwhile, Reader
//       would have to poll FileReader.

// Searching for read-position based on a gtid, not gtid-list. Each domain inside a binary log is an
// independent stream.

// Events. Search for gtid 1-1-1000, which is in the binlog file 4:
// 1. Artificial rotate to binlog 4
// 2. Format desc from the file
// 3. Gtid list from the file
// 4. Binlog checkpoint, this needs to be generated
// 5. Artificial gtid list. Simple for the single domain case, need to check what the multidomain case needs
// 6. Start replaying from gtid event 1-1-1000

namespace pinloki
{
constexpr int HEADER_LEN = 19;

FileReader::FileReader(const maxsql::Gtid& gtid, const Inventory* inv)
    : m_inotify_fd{inotify_init1(IN_NONBLOCK)}
    , m_inventory(*inv)
{
    if (m_inotify_fd == -1)
    {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    if (gtid.is_valid())
    {
        auto gtid_pos = find_gtid_position(gtid, inv);
        if (gtid_pos.file_name.empty())
        {
            MXB_THROW(BinlogReadError, "Could not find " << gtid << " in binlogs");
        }

        // This is where the initial events need to be generated. I deque of events will be fine.

        open(gtid_pos.file_name);
        m_read_pos.next_pos = gtid_pos.file_pos;
    }
    else
    {
        open(m_inventory.file_names().front());
    }
}

void FileReader::open(const std::string& file_name)
{
    m_read_pos.file.close();
    m_read_pos.file.open(file_name, std::ios_base::in | std::ios_base::binary);
    if (!m_read_pos.file.good())
    {
        MXB_THROW(BinlogReadError, "Could not open " << file_name << " for reading");
    }
    m_read_pos.next_pos = PINLOKI_MAGIC.size();     // should check that it really is PINLOKI_MAGIC
    m_read_pos.name = file_name;
}

void FileReader::fd_notify(uint32_t events)
{
    /* Read, and discard, the inotify events */
    const size_t SZ = 8 * 1024;
    char buf[SZ];

    ssize_t len = read(m_inotify_fd, buf, SZ);

    inotify_event* event = nullptr;
    for (auto ptr = buf; ptr < buf + len; ptr += sizeof(inotify_event) + event->len)
    {
        event = reinterpret_cast<inotify_event*>(ptr);
        if (!(event->mask & IN_MODIFY))
        {
            std::cerr << "Unexpected inotify event. event->mask = "
                      << "0x" << std::hex << event->mask << '\n';
        }
    }

    if (len == -1)
    {
        if (errno != EAGAIN)
        {
            perror("read");
            exit(EXIT_FAILURE);
        }
        return;
    }
}

maxsql::RplEvent FileReader::fetch_event()
{
    std::vector<char> raw(HEADER_LEN);

    m_read_pos.file.clear();
    m_read_pos.file.seekg(m_read_pos.next_pos);
    m_read_pos.file.read(raw.data(), HEADER_LEN);

    if (m_read_pos.file.tellg() != m_read_pos.next_pos + HEADER_LEN)
    {
        // What! Why am I throwing here, isn't this just an end of file condition, where
        // the writer has written only the header (hm, maybe the write is atomic).
        return maxsql::RplEvent();
        MXB_THROW(BinlogReadError, "fetch_raw: failed to read event header, file " << m_read_pos.name);
    }

    auto event_length = maxsql::RplEvent::get_event_length(raw);

    raw.resize(event_length);
    m_read_pos.file.read(raw.data() + HEADER_LEN, event_length - HEADER_LEN);

    if (m_read_pos.file.tellg() != m_read_pos.next_pos + event_length)
    {
        if (m_inotify_file != m_read_pos.name)
        {
            set_inotify_fd();
            // TODO. I think there is a race condition. One notification should happen immediately after
            //       this, or just do fetch_event again. Could not make the race happen.
        }
        return std::vector<char>();
    }

    maxsql::RplEvent rpl(std::move(raw));

    if (rpl.event_type() == ROTATE_EVENT)
    {
        auto file_name = m_inventory.config().path(rpl.rotate().file_name);
        open(file_name);
    }
    else
    {
        m_read_pos.next_pos = rpl.next_event_pos();
    }

    std::cout.flush();

    return rpl;
}

int FileReader::fd()
{
    return m_inotify_fd;
}

void FileReader::set_inotify_fd()
{
    if (m_inotify_descriptor != -1)
    {
        inotify_rm_watch(m_inotify_fd, m_inotify_descriptor);
    }

    std::cout << "Set inotify file " << m_read_pos.name << std::endl;

    m_inotify_file = m_read_pos.name;
    m_inotify_descriptor = inotify_add_watch(m_inotify_fd, m_read_pos.name.c_str(), IN_MODIFY);
    if (m_inotify_descriptor == -1)
    {
        perror("inotify_add_watch");
        exit(EXIT_FAILURE);
    }
}
}
