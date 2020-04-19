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

#include <mysql.h>
#include <mariadb_rpl.h>
#include <iostream>
#include <iomanip>
#include <assert.h>

/** Notes.
 * 1.  For the very first file, the server sends only one artifical rotate to end the file,
 *     while it sends both an artificial and real rotate for all other files.
 * 1b. Will this work when the first file has been deleted on the server, and pinloki tries
 *     to bootstrap from a gtid in the new first file?
 * 2.  The m_sync_with_server flag is used to know to open an existing file for read/write when
 *     pinloki starts from a known gtid. And further, to avoid writing duplicate events to
 *     that file.
 * 3.  Everything works, but it does not hurt to add some more verification to the file (e.g.,
 *     that the events pinloki avoids to write match what is already in the file.
 * 4. Is it possible that the file to open is the one before the last.
 *
 *    TODO Houston, we have a problem. There were 4 binlogs, I changed the domain on the master and did
 *    a few transaction. After that the server sent binlog 3 only as a an artificial event, and
 *    the next event was rotate to 4. The events that used to be in binlog 3 were missing.
 *    An exception is thrown when that happens.
 *    The cure is just to adjust the flags, or some kind of logic to know if the intention is to
 *    create a new file, open an existing one or ignore the rotate.
 */
namespace pinloki
{
FileWriter::FileWriter(bool have_files, Inventory* inv)
    : m_sync_with_server(have_files)
    , m_inventory(*inv)
{
}

void FileWriter::add_event(const maxsql::MariaRplEvent& rpl_event)
{
    bool is_artificial = rpl_event.event().flags & LOG_EVENT_ARTIFICIAL_F;      // MariaRplEvent::is_artificial
    if (rpl_event.event().event_type == ROTATE_EVENT)
    {
        rotate_event(rpl_event);
    }
    else if (is_artificial)
    {
        m_current_pos.write_pos = rpl_event.event().next_event_pos;
        if (m_sync_with_server && rpl_event.event().event_type == GTID_LIST_EVENT)
        {
            m_sync_with_server = false;     // sync done
        }
    }
    else if (!m_sync_with_server)
    {
        write_to_file(m_current_pos, rpl_event);
    }
}

void FileWriter::rotate_event(const maxsql::MariaRplEvent& rpl_event)
{
    bool is_artificial = rpl_event.event().flags & LOG_EVENT_ARTIFICIAL_F;
    auto& rotate = rpl_event.event().event.rotate;

    auto name = get_rotate_name(rpl_event.raw_data(), rpl_event.raw_data_size());
    std::string file_name = m_inventory.config().path(name);

    if (m_sync_with_server)
    {
        open_existing_file(file_name);
        // m_sync_with_server stays true until we get the artificial GTD_LIST
    }
    else if ((is_artificial && m_inventory.count() <= 1) || !is_artificial)
    {

        if (m_inventory.is_listed(file_name))
        {
            MXB_THROW(BinlogWriteError, file_name << " already listed in index!");
        }

        m_previous_pos = std::move(m_current_pos);

        m_current_pos.name = file_name;
        m_current_pos.file.open(m_current_pos.name, std::ios_base::out | std::ios_base::binary);
        m_current_pos.file.write(PINLOKI_MAGIC.data(), PINLOKI_MAGIC.size());
        m_current_pos.write_pos = PINLOKI_MAGIC.size();
        m_current_pos.file.flush();

        m_inventory.add(m_current_pos.name);

        if (m_previous_pos.file.is_open())
        {
            write_to_file(m_previous_pos, rpl_event);
            m_previous_pos.file.close();
        }
    }
}

void FileWriter::open_existing_file(const std::string& file_name)
{
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
    fn.file.seekp(fn.write_pos);
    fn.file.write(rpl_event.raw_data(), rpl_event.raw_data_size());
    fn.write_pos = rpl_event.event().next_event_pos;

    if (!fn.file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not write event to " << fn.name);
    }

    fn.file.flush();
}
}
