/*
Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights
reserved.

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

#include "table_index.h"

mysql::Binary_log_event *Table_index::process_event(mysql::Table_map_event *tm)
{
 if (find(tm->table_id) == end())
   insert(Event_index_element(tm->table_id,tm));

 /* Consume this event so it won't be deallocated beneith our feet */
 return 0;
}

Table_index::~Table_index ()
{
  Int2event_map::iterator it= begin();
  do
  {
    delete it->second;
  } while( ++it != end());
}

int Table_index::get_table_name(int table_id, std::string out)
{
  iterator it;
  if ((it= find(table_id)) == end())
  {
    std::stringstream os;
    os << "unknown_table_" << table_id;
    out.append(os.str());
    return 1;
  }

  out.append(it->second->table_name);
  return 0;
}
