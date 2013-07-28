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

/*
 * File:   table_index.h
 * Author: thek
 *
 * Created on den 8 september 2010, 13:47
 */

#ifndef TABLE_INDEX_H
#define	TABLE_INDEX_H
#include "binlog_event.h"
#include <map>
#include "basic_content_handler.h"

typedef std::pair<boost::uint64_t, mysql::Table_map_event *> Event_index_element;
typedef std::map<boost::uint64_t, mysql::Table_map_event *> Int2event_map;

class Table_index : public mysql::Content_handler, public Int2event_map
{
public:
 mysql::Binary_log_event *process_event(mysql::Table_map_event *tm);

 ~Table_index();

 int get_table_name(int table_id, std::string out);

};


#endif	/* TABLE_INDEX_H */
