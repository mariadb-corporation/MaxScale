/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "dbconnection.hh"
#include "inventory.hh"
#include "gtid.hh"
#include "rpl_event.hh"

#include <string>
#include <fstream>

namespace pinloki
{

class Writer;

/**
 * @brief FileWriter - This is a pretty straightforward writer of binlog events to files.
 *                     The class represents a series of files.
 */
class FileWriter
{
public:
    FileWriter(InventoryWriter* inv, const Writer& writer);

    void begin_txn();
    void add_event(maxsql::RplEvent& rpl_event);
    void commit_txn();
private:
    struct WritePosition
    {
        std::string   name;
        std::ofstream file;
        int64_t       write_pos;
    };


    bool open_for_appending(const maxsql::RplEvent& fmt_event);
    bool open_binlog(const std::string& file_name);
    void perform_rotate(const maxsql::Rotate& rotate, const maxsql::RplEvent& fmt_event);
    void create_binlog(const std::string& file_name, const maxsql::RplEvent& fmt_event);
    void write_rpl_event(const maxsql::RplEvent& rpl_event);
    void write_rotate(WritePosition& pos, const std::string& to_file_name);
    void write_gtid_list(WritePosition& pos);

    bool             m_newborn = true;
    bool             m_ignore_preamble = false;
    InventoryWriter& m_inventory;
    const Writer&    m_writer;
    WritePosition    m_current_pos;
    maxsql::Rotate   m_rotate;

    bool              m_in_transaction = false;
    std::vector<char> m_tx_buffer;
};
}
