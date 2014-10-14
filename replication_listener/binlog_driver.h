/*
Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights
reserved.
Copyright (c) 2013-2014, MariaDB Corporation Ab

Portions of this file contain modifications contributed and copyrighted by
MariaDB Corporation, Ab. Those modifications are gratefully acknowledged and are described
briefly in the source code.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of
the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
02110-1301  USA
*/
/*
MariaDB Corporation change details:
- Added support for GTID event handling for both MySQL and MariaDB
- Added support for setting binlog position based on GTID

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/

#ifndef _BINLOG_DRIVER_H
#define	_BINLOG_DRIVER_H
#include "binlog_event.h"
#include "protocol.h"
#include "gtid.h"

namespace mysql {
namespace system {

class Binary_log_driver
{
public:
  template <class FilenameT>
  Binary_log_driver(const FilenameT& filename = FilenameT(), unsigned int offset = 0)
    : m_binlog_file_name(filename), m_binlog_offset(offset), m_server_type(MYSQL_SERVER_TYPE_NA)
  {
  }

  ~Binary_log_driver() {}

  /**
   * Connect to the binary log using previously declared connection parameters
   * @return Success or error code
   * @retval 0 Success
   * @retval >0 Error code (to be specified)
   */

  virtual int connect(Gtid gtid)= 0;
  virtual int connect() = 0;
  virtual int connect(const boost::uint64_t binlog_pos) = 0;

  /**
   * Blocking attempt to get the next binlog event from the stream
   * @param event [out] Pointer to a binary log event to be fetched.
   */
  virtual int wait_for_next_event(mysql::Binary_log_event **event)= 0;

  /**
   * Set the reader position
   * @param str The file name
   * @param position The file position
   *
   * @return False on success and True if an error occurred.
   */
  virtual int set_position(const std::string &str, unsigned long position)= 0;

  virtual int set_position_gtid(const Gtid gtid) = 0;

  /**
   * Get the read position.
   *
   * @param[out] string_ptr Pointer to location where the filename will be stored.
   * @param[out] position_ptr Pointer to location where the position will be stored.
   *
   * @retval 0 Success
   * @retval >0 Error code
   */
  virtual int get_position(std::string *filename_ptr, unsigned long *position_ptr) = 0;

  virtual int fetch_server_version(const std::string& user,
				   const std::string& passwd,
				   const std::string& host,
				   long port) = 0;

  virtual void shutdown() = 0;

  Binary_log_event* parse_event(std::istream &sbuff, Log_event_header *header);

  mysql_server_types get_mysql_server_type() const 
  {
    return m_server_type;
  }
 
protected:
  /**
   * Used each time the client reconnects to the server to specify an
   * offset position.
   */
  unsigned long m_binlog_offset;
  std::string m_binlog_file_name;
  mysql_server_types m_server_type;
};

} // namespace mysql::system
} // namespace mysql
#endif	/* _BINLOG_DRIVER_H */
