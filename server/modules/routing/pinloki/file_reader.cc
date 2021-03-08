/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "config.hh"
#include "file_reader.hh"
#include "inventory.hh"
#include "find_gtid.hh"
#include "rpl_event.hh"

#include <mysql.h>
#include <mariadb_rpl.h>
#include <array>
#include <iostream>
#include <iomanip>
#include <thread>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>

#include <maxscale/protocol/mariadb/mysql.hh>

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

FileReader::FileReader(const maxsql::GtidList& gtid_list, const InventoryReader* inv)
    : m_inotify_fd{inotify_init1(IN_NONBLOCK)}
    , m_inventory(*inv)
{
    if (m_inotify_fd == -1)
    {
        MXB_THROW(BinlogReadError, "inotify_init failed: " << errno << ", " << mxb_strerror(errno));
    }

    if (gtid_list.gtids().size() > 0)
    {
        // Get a sorted list of GtidPositions
        m_catchup = find_gtid_position(gtid_list.gtids(), m_inventory);

        // The first one is the position from which to start reading.
        const auto& gtid_pos = m_catchup.front();

        if (gtid_pos.file_name.empty())
        {
            MXB_THROW(GtidNotFoundError,
                      "Could not find '" << gtid_pos.gtid << "' in any of the binlogs");
        }

        open(gtid_pos.file_name);

        // Generate initial rotate and read format description, gtid list and any
        // binlog checkpoints from the file before jumping to the gtid.
        m_generate_rotate_to = gtid_pos.file_name;
        m_read_pos.next_pos = PINLOKI_MAGIC.size();

        // Once the preamble is done, jump to this file position. If the position is
        // at the beginning of the file, this does the same as the 'else' below.
        if (gtid_pos.file_pos != long(PINLOKI_MAGIC.size()))
        {
            m_initial_gtid_file_pos = gtid_pos.file_pos;
        }
    }
    else
    {
        auto first = first_string(m_inventory.file_names());
        open(first);
        // Preamble just means send the initial rotate and then the whole file
        m_generate_rotate_to = first;
        m_read_pos.next_pos = PINLOKI_MAGIC.size();
    }
}

FileReader::~FileReader()
{
    close(m_inotify_fd);
}

void FileReader::open(const std::string& file_name)
{
    auto previous_pos = std::move(m_read_pos);
    m_read_pos.file.open(file_name, std::ios_base::in | std::ios_base::binary);
    if (!m_read_pos.file.good())
    {
        MXB_THROW(BinlogReadError,
                  "Could not open " << file_name << " for reading: " << errno << ", " << mxb_strerror(errno));
    }

    // Close the previous file after the new one has been opened.
    // Ensures that PinlokiSession::purge_logs() stops when needed.
    if (previous_pos.file.is_open())
    {
        previous_pos.file.close();
    }

    m_read_pos.next_pos = PINLOKI_MAGIC.size();     // TODO should check that it really is PINLOKI_MAGIC
    m_read_pos.name = file_name;

    set_inotify_fd();   // Always set inotify. Avoids all race conditions, extra notifications are fine.
}

void FileReader::fd_notify(uint32_t events)
{
    /* Read, and discard, the inotify events */
    const size_t SZ = 8 * 1024;
    char buf[SZ];

    ssize_t len = read(m_inotify_fd, buf, SZ);

#ifdef SS_DEBUG
    inotify_event* event = nullptr;
    for (auto ptr = buf; ptr < buf + len; ptr += sizeof(inotify_event) + event->len)
    {
        event = reinterpret_cast<inotify_event*>(ptr);
        // We only expect the file to be modified. The IN_IGNORED event is sent when we close the previous
        // file and open a new one.
        mxb_assert(event->mask & (IN_MODIFY | IN_IGNORED));
    }
#endif

    if (len == -1 && errno != EAGAIN)
    {
        MXB_THROW(BinlogReadError, "Failed to read inotify fd: " << errno << ", " << mxb_strerror(errno));
    }
}

std::vector<char> FileReader::fetch_raw()
{
    std::vector<char> raw(HEADER_LEN);

    m_read_pos.file.clear();
    m_read_pos.file.seekg(m_read_pos.next_pos);
    m_read_pos.file.read(raw.data(), HEADER_LEN);

    if (m_read_pos.file.tellg() != m_read_pos.next_pos + HEADER_LEN)
    {
        // Partial, or no header. Wait for more via inotify.
        return std::vector<char>();
    }

    auto event_length = maxsql::RplEvent::get_event_length(raw);

    raw.resize(event_length);
    m_read_pos.file.read(raw.data() + HEADER_LEN, event_length - HEADER_LEN);

    if (m_read_pos.file.tellg() != m_read_pos.next_pos + event_length)
    {
        // Wait for more via inotify.
        return std::vector<char>();
    }

    return raw;
}

maxsql::RplEvent FileReader::fetch_event()
{
    maxsql::RplEvent event;
    do
    {
        event = fetch_event_internal();
        if (event.is_empty())
        {
            return event;
        }

        if (event.event_type() == GTID_EVENT)
        {
            auto gtid_event = event.gtid_event();

            // Is this domain being streamed yet?
            if (m_active_domains.count(gtid_event.gtid.domain_id()) != 0)
            {
                m_skip_gtid = false;    // yes, we are already streaming this domain
            }
            else
            {
                auto ite = std::find_if(begin(m_catchup), end(m_catchup),
                                        [&](const GtidPosition& gp) {
                                            return gtid_event.gtid.domain_id() == gp.gtid.domain_id();
                                        });

                if (ite == end(m_catchup))
                {   // This domain was not in the client's initial state. It could be a new
                    // domain or could be a mistake. Start streaming it.
                    m_active_domains.insert(gtid_event.gtid.domain_id());
                    m_skip_gtid = false;
                }
                else if (gtid_event.gtid.sequence_nr() > ite->gtid.sequence_nr())
                {   // The replica had a start gtid for this domain. The start gtid
                    // is the one it already has, so starting stream from the next
                    // gtid in this domain.
                    m_active_domains.insert(gtid_event.gtid.domain_id());
                    m_catchup.erase(ite);
                    m_skip_gtid = false;
                }
                else
                {   // This gtid is before the replicas start gtid for this domain
                    m_skip_gtid = true;
                }
            }
        }
        else if (event.event_type() == STOP_EVENT || event.event_type() == ROTATE_EVENT)
        {
            m_skip_gtid = false;
        }
    }
    while(m_skip_gtid);

    return event;
}

maxsql::RplEvent FileReader::fetch_event_internal()
{
    if (!m_generate_rotate_to.empty())
    {
        auto tmp = m_generate_rotate_to;
        m_generate_rotate_to.clear();
        // Next position is the current next_pos value (weird)
        auto vec = mxq::create_rotate_event(basename(tmp.c_str()),
                                            m_inventory.config().server_id(),
                                            m_read_pos.next_pos, mxq::Kind::Artificial);

        return mxq::RplEvent(std::move(vec));
    }

    auto raw = fetch_raw();

    if (raw.empty())
    {
        return maxsql::RplEvent();
    }

    maxsql::RplEvent rpl(std::move(raw));

    if (m_generating_preamble)
    {
        if (rpl.event_type() != GTID_LIST_EVENT
            && rpl.event_type() != FORMAT_DESCRIPTION_EVENT
            && rpl.event_type() != BINLOG_CHECKPOINT_EVENT)
        {
            m_generating_preamble = false;
            if (m_initial_gtid_file_pos)
            {
                m_read_pos.next_pos = m_initial_gtid_file_pos;

                auto raw = fetch_raw();

                if (raw.empty())
                {
                    return maxsql::RplEvent();
                }

                rpl = maxsql::RplEvent(std::move(raw));
            }
        }
    }

    if (rpl.event_type() == ROTATE_EVENT)
    {
        auto file_name = m_inventory.config().path(rpl.rotate().file_name);
        open(file_name);
    }
    else if (rpl.event_type() == STOP_EVENT)
    {
        m_generate_rotate_to = next_string(m_inventory.file_names(), m_read_pos.name);
        if (!m_generate_rotate_to.empty())
        {
            MXB_SINFO("STOP_EVENT in file " << m_read_pos.name
                                            << ".  The next event will be a generated, artificial ROTATE_EVENT to "
                                            << m_generate_rotate_to);
            open(m_generate_rotate_to);
        }
        else
        {
            MXB_THROW(BinlogReadError,
                      "Sequence error,  binlog file " << m_read_pos.name << " has a STOP_EVENT"
                                                      << " but the Inventory has no successor for it");
        }
    }
    else
    {
        m_read_pos.next_pos = rpl.next_event_pos();
    }

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

    m_inotify_descriptor = inotify_add_watch(m_inotify_fd, m_read_pos.name.c_str(), IN_MODIFY);

    if (m_inotify_descriptor == -1)
    {
        MXB_THROW(BinlogReadError, "inotify_add_watch failed:" << errno << ", " << mxb_strerror(errno));
    }
}

mxq::RplEvent FileReader::create_heartbeat_event() const
{
    auto pos = m_read_pos.name.find_last_of('/');
    mxb_assert(pos != std::string::npos);
    auto filename = m_read_pos.name.substr(pos + 1);
    std::vector<char> data(HEADER_LEN + filename.size() + 4);
    uint8_t* ptr = (uint8_t*)&data[0];

    // Timestamp, always zero
    mariadb::set_byte4(ptr, 0);
    ptr += 4;

    // This is a heartbeat type event
    *ptr++ = HEARTBEAT_LOG_EVENT;

    // server_id
    mariadb::set_byte4(ptr, m_inventory.config().server_id());
    ptr += 4;

    // Event length
    mariadb::set_byte4(ptr, data.size());
    ptr += 4;

    // Next position is the current next_pos value
    mariadb::set_byte4(ptr, -1);
    ptr += 4;

    // This is an artificial event
    mariadb::set_byte2(ptr, LOG_EVENT_ARTIFICIAL_F);
    ptr += 2;

    // The binlog name as the payload (not null-terminated)
    memcpy(ptr, filename.c_str(), filename.size());
    ptr += filename.size();

    // Checksum of the whole event
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    return mxq::RplEvent(std::move(data));
}
}
