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
#include "file_writer.hh"
#include "file_reader.hh"
#include "config.hh"

#include <maxscale/protocol/mariadb/mysql.hh>
#include <mariadb_rpl.h>
#include <iostream>
#include <iomanip>
#include <assert.h>

namespace pinloki
{
FileWriter::FileWriter(Inventory* inv)
    : m_inventory(*inv)
{
}

void FileWriter::add_event(const maxsql::MariaRplEvent& rpl_event)
{
    bool is_artificial = rpl_event.event().flags & LOG_EVENT_ARTIFICIAL_F;      // MariaRplEvent::is_artificial
    if (rpl_event.event().event_type == HEARTBEAT_LOG_EVENT)
    {
        // Heartbeat event, don't process it
    }
    else if (is_artificial)
    {
        if (rpl_event.event().event_type == ROTATE_EVENT)
        {
            rotate_event(rpl_event);
        }
        else
        {
            m_current_pos.write_pos = rpl_event.event().next_event_pos;
        }
    }
    else
    {
        write_to_file(m_current_pos, rpl_event);
    }
}

void FileWriter::rotate_event(const maxsql::MariaRplEvent& rpl_event)
{
    auto& rotate = rpl_event.event().event.rotate;
    auto name = get_rotate_name(rpl_event.raw_data(), rpl_event.raw_data_size());
    std::string file_name = m_inventory.config().path(name);

    if (m_inventory.is_listed(file_name))
    {
        open_existing_file(file_name);
    }
    else
    {
        m_previous_pos = std::move(m_current_pos);
        auto last_file = m_inventory.last();

        m_current_pos.name = file_name;
        m_current_pos.file.open(m_current_pos.name, std::ios_base::out | std::ios_base::binary);
        m_current_pos.file.write(PINLOKI_MAGIC.data(), PINLOKI_MAGIC.size());
        m_current_pos.write_pos = PINLOKI_MAGIC.size();
        m_current_pos.file.flush();

        m_inventory.add(m_current_pos.name);

        if (m_previous_pos.file.is_open())
        {
            m_previous_pos.file.close();
            if (!m_previous_pos.file.good())
            {
                MXB_THROW(BinlogWriteError,
                          "File " << m_previous_pos.name
                                  << " did not close (flush) properly during rotate: "
                                  << errno << ", " << mxb_strerror(errno));
            }
        }
        else
        {
            if (!last_file.empty())
            {
                write_stop(last_file);
            }
        }
    }
}

void FileWriter::open_existing_file(const std::string& file_name)
{
    if (m_current_pos.file.is_open())
    {
        m_current_pos.file.close();
        if (!m_current_pos.file.good())
        {
            MXB_THROW(BinlogWriteError,
                      "File " << m_current_pos.name << " did not close (flush) properly: "
                              << errno << ", " << mxb_strerror(errno));
        }
    }

    m_current_pos.name = file_name;
    m_current_pos.file.open(m_current_pos.name, std::ios_base::in
                            | std::ios_base::out
                            | std::ios_base::binary);
    m_current_pos.write_pos = PINLOKI_MAGIC.size();

    if (!m_current_pos.file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not open " << m_current_pos.name << " for read/write");
    }
}

void FileWriter::write_to_file(WritePosition& fn, const maxsql::MariaRplEvent& rpl_event)
{
    fn.file.seekp(0, std::ios_base::end);
    auto end_pos = fn.file.tellp();

    if (fn.write_pos >= end_pos)
    {
        fn.file.seekp(fn.write_pos);
        fn.file.write(rpl_event.raw_data(), rpl_event.raw_data_size());
        fn.file.flush();
    }

    fn.write_pos = rpl_event.event().next_event_pos;

    if (!fn.file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not write event to " << fn.name);
    }
}

void FileWriter::write_stop(const std::string& file_name)
{
    MXB_SINFO("write stop to " << file_name);

    auto file = std::fstream(file_name, std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    if (!file.good())
    {
        MXB_THROW(BinlogWriteError,
                  "Could not open " << file_name << " for  STOP_EVENT addition");
    }

    constexpr int HEADER_LEN = 19;
    const size_t EVENT_LEN = HEADER_LEN + 4;        // header plus crc

    file.seekp(0, std::ios_base::end);
    const size_t end_pos = file.tellp();

    std::vector<char> data(EVENT_LEN);
    uint8_t* ptr = (uint8_t*)&data[0];

    // Zero timestamp
    mariadb::set_byte4(ptr, 0);
    ptr += 4;

    // A stop event
    *ptr++ = STOP_EVENT;

    // server id
    mariadb::set_byte4(ptr, m_inventory.config().server_id());
    ptr += 4;

    // Event length
    mariadb::set_byte4(ptr, EVENT_LEN);
    ptr += 4;

    // Next position
    mariadb::set_byte4(ptr, end_pos + EVENT_LEN);
    ptr += 4;

    // No flags (this is a real event)
    mariadb::set_byte2(ptr, 0);
    ptr += 2;

    // Checksum
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    file.write(data.data(), data.size());
    file.flush();

    if (!file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not write STOP_EVENT to " << file_name);
    }
}
}
