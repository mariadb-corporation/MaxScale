/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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

    void add_event(maxsql::RplEvent& rpl_event);
private:
    struct WritePosition
    {
        std::string   name;
        std::ofstream file;
        int64_t       write_pos;
    };

    bool open_binlog(const std::string& file_name, const maxsql::RplEvent* ev = nullptr);
    bool open_for_appending(const maxsql::Rotate& rotate, const maxsql::RplEvent& fmt_event);
    void perform_rotate(const maxsql::Rotate& rotate);
    void write_rpl_event(maxsql::RplEvent& rpl_event);
    void write_stop(const std::string& file_name);
    void write_rotate(const std::string& to_file_name);
    void write_gtid_list();

    void write_to_file(const char* ptr, size_t bytes);

    bool             m_newborn = true;
    bool             m_ignore_preamble = false;
    InventoryWriter& m_inventory;
    const Writer&    m_writer;
    WritePosition    m_current_pos;
    maxsql::Rotate   m_rotate;

    std::unique_ptr<mxq::EncryptCtx> m_encrypt;
};
}
