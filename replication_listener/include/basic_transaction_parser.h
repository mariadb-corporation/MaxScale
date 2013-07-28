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
#ifndef _BASIC_TRANSACTION_PARSER_H
#define	_BASIC_TRANSACTION_PARSER_H

/*
  TODO The Transaction_log_event and Basic_transaction_parser will be removed
  from this library and replaced  with a table map indexer instead which can be
  used to retrive table names.
*/

#include <list>
#include <boost/cstdint.hpp>
#include "binlog_event.h"
#include "basic_content_handler.h"

#include <iostream>

namespace mysql {
typedef std::pair<boost::uint64_t, Binary_log_event *> Event_index_element;
typedef std::map<boost::uint64_t, Binary_log_event *> Int_to_Event_map;
class Transaction_log_event : public Binary_log_event
{
public:
    Transaction_log_event() : Binary_log_event() {}
    Transaction_log_event(Log_event_header *header) : Binary_log_event(header) {}
    virtual ~Transaction_log_event();

    Int_to_Event_map &table_map() { return m_table_map; }
    /**
     * Index for easier table name look up
     */
    Int_to_Event_map m_table_map;

    std::list<Binary_log_event *> m_events;
};

Transaction_log_event *create_transaction_log_event(void);

class Basic_transaction_parser : public mysql::Content_handler
{
public:
  Basic_transaction_parser() : mysql::Content_handler()
  {
      m_transaction_state= NOT_IN_PROGRESS;
  }

  mysql::Binary_log_event *process_event(mysql::Query_event *ev);
  mysql::Binary_log_event *process_event(mysql::Row_event *ev);
  mysql::Binary_log_event *process_event(mysql::Table_map_event *ev);
  mysql::Binary_log_event *process_event(mysql::Xid *ev);
  mysql::Binary_log_event *process_event(mysql::Binary_log_event *ev) {return ev; }
  mysql::Binary_log_event *process_event(mysql::Gtid_event *ev);

private:
  boost::uint32_t m_start_time;
  enum Transaction_states { STARTING, IN_PROGRESS, COMMITTING, NOT_IN_PROGRESS } ;
  enum Transaction_states m_transaction_state;
  std::list <mysql::Binary_log_event *> m_event_stack;
  mysql::Binary_log_event *process_transaction_state(mysql::Binary_log_event *ev);
};

} // end namespace

#endif	/* _BASIC_TRANSACTION_PARSER_H */

