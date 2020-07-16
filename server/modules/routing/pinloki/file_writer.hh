/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
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

class Writer;

/**
 * @brief FileWriter - This is a pretty straightforward writer of binlog events to files.
 *                     The class represents a series of files.
 */
class FileWriter
{
public:
    FileWriter(Inventory* inv, const Writer& writer);

    void begin_txn();
    void add_event(const maxsql::MariaRplEvent& maria_event);
    void commit_txn();
private:
    struct WritePosition
    {
        std::string   name;
        std::ofstream file;
        int           write_pos;
    };

    void rotate_event(const maxsql::MariaRplEvent& rpl_event);
    void write_to_file(WritePosition& fn, const maxsql::RplEvent& rpl_event);
    void write_stop(const std::string& file_name);
    void write_rotate(WritePosition& fn, const std::string& to_file_name);
    void write_gtid_list(WritePosition& fn);

    Inventory&    m_inventory;
    const Writer& m_writer;
    WritePosition m_current_pos;

    bool               m_in_transaction = false;
    std::ostringstream m_tx_buffer;
};
}
