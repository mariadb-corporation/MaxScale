/*
  Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights
  reserved.
  Copyright (c) 2013, MariaDB Corporation Ab

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

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/

#include "binlog_driver.h"

namespace mysql { namespace system {


Binary_log_event* Binary_log_driver::parse_event(std::istream &is,
                                                 Log_event_header *header)
{
  Binary_log_event *parsed_event= 0;

  switch (header->type_code) {
    case TABLE_MAP_EVENT:
      parsed_event= proto_table_map_event(is, header);
      break;
    case QUERY_EVENT:
      parsed_event= proto_query_event(is, header);
      break;
    case GTID_EVENT_MARIADB:
    case GTID_EVENT_MYSQL:
      parsed_event= proto_gtid_event(is, header);
      break;
    case INCIDENT_EVENT:
      parsed_event= proto_incident_event(is, header);
      break;
    case WRITE_ROWS_EVENT:
    case UPDATE_ROWS_EVENT:
    case DELETE_ROWS_EVENT:
      parsed_event= proto_rows_event(is, header);
      break;
    case ROTATE_EVENT:
      {
        Rotate_event *rot= proto_rotate_event(is, header);
        m_binlog_file_name= rot->binlog_file;
        m_binlog_offset= (unsigned long)rot->binlog_pos;
        parsed_event= rot;
      }
      break;
    case INTVAR_EVENT:
      parsed_event= proto_intvar_event(is, header);
      break;
    case USER_VAR_EVENT:
      parsed_event= proto_uservar_event(is, header);
      break;
    default:
      {
        // Create a dummy driver.
        parsed_event= new Binary_log_event(header);
      }
  }

  return parsed_event;
}

}
}
