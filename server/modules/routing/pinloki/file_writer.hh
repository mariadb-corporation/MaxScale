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

#include "dbconnection.hh"
#include <maxbase/exception.hh>
#include "inventory.hh"
#include "gtid.hh"
#include "rpl_event.hh"

#include <string>
#include <fstream>

namespace pinloki
{
DEFINE_EXCEPTION(BinlogWriteError);

/**
 * @brief FileWriter - This is a pretty straightforward writer of binlog events to files.
 *                     The class represents a series of files.
 */
class FileWriter    // : public Storage
{
public:
    FileWriter(Inventory* inv);

    void add_event(const maxsql::MariaRplEvent& rpl_event);
private:
    struct WritePosition
    {
        std::string   name;
        std::ofstream file;
        int           write_pos;
    };

    void rotate_event(const maxsql::MariaRplEvent& rpl_event);
    void write_to_file(WritePosition& fn, const maxsql::MariaRplEvent& rpl_event);
    void open_existing_file(const std::string& file_name);

    WritePosition m_previous_pos;       // This does not really need to be a member.
    WritePosition m_current_pos;

    // Set to the end-of-file position whenever an existing file is opened. This prevents events in an
    // existing binlog from being overwritten due to reconnections.
    int64_t m_sync_pos = 0;

    Inventory& m_inventory;
};
}
