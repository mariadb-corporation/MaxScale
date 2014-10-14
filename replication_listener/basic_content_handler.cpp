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
- Added GTID event handler

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/

#include "basic_content_handler.h"
#include <boost/bind.hpp>

namespace mysql {

Content_handler::Content_handler () {}
Content_handler::~Content_handler () {}
mysql::Binary_log_event *Content_handler::process_event(mysql::Query_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Row_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Table_map_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Xid *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::User_var_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Incident_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Rotate_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Int_var_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Binary_log_event *ev) { return ev; }
mysql::Binary_log_event *Content_handler::process_event(mysql::Gtid_event *ev) {return ev; }

Injection_queue *Content_handler::get_injection_queue(void)
{
  return m_reinject_queue;
}

void Content_handler::set_injection_queue(Injection_queue *queue)
{
  m_reinject_queue= queue;
}

mysql::Binary_log_event*
  Content_handler::internal_process_event(mysql::Binary_log_event *ev)
{
 mysql::Binary_log_event *processed_event= 0;
 switch(ev->header ()->type_code) {
 case mysql::QUERY_EVENT:
   processed_event= process_event(static_cast<mysql::Query_event*>(ev));
   break;
 case mysql::GTID_EVENT_MARIADB:
 case mysql::GTID_EVENT_MYSQL:
   processed_event= process_event(static_cast<mysql::Gtid_event*>(ev));
   break;
 case mysql::WRITE_ROWS_EVENT:
 case mysql::UPDATE_ROWS_EVENT:
 case mysql::DELETE_ROWS_EVENT:
   processed_event= process_event(static_cast<mysql::Row_event*>(ev));
   break;
 case mysql::USER_VAR_EVENT:
   processed_event= process_event(static_cast<mysql::User_var_event *>(ev));
   break;
 case mysql::ROTATE_EVENT:
   processed_event= process_event(static_cast<mysql::Rotate_event *>(ev));
   break;
 case mysql::INCIDENT_EVENT:
   processed_event= process_event(static_cast<mysql::Incident_event *>(ev));
   break;
 case mysql::XID_EVENT:
   processed_event= process_event(static_cast<mysql::Xid *>(ev));
   break;
 case mysql::TABLE_MAP_EVENT:
   processed_event= process_event(static_cast<mysql::Table_map_event *>(ev));
   break;
 /* TODO ********************************************************************/
 case mysql::FORMAT_DESCRIPTION_EVENT:
   processed_event= process_event(ev);
   break;
 case mysql::BEGIN_LOAD_QUERY_EVENT:
   processed_event= process_event(ev);
   break;
 case mysql::EXECUTE_LOAD_QUERY_EVENT:
   processed_event= process_event(ev);
   break;
 case mysql::INTVAR_EVENT:
   processed_event= process_event(ev);
   break;
 case mysql::STOP_EVENT:
   processed_event= process_event(ev);
   break;
 case mysql::RAND_EVENT:
   processed_event= process_event(ev);
   break;
 /****************************************************************************/
 default:
   processed_event= process_event(ev);
   break;
 }
 return processed_event;
}

} // end namespace
