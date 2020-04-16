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
    FileWriter(bool have_files);

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

    bool          m_sync_with_server;
    WritePosition m_previous_pos;       // This does not really need to be a member.
    WritePosition m_current_pos;

    Inventory m_inventory;
};
}
