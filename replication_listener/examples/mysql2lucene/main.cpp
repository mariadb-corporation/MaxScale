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
 * File:   main.cpp
 * Author: thek
 *
 * Created on den 12 maj 2010, 14:47
 */

#include <stdlib.h>
#include <boost/foreach.hpp>
#include "binlog_api.h"

#include "table_update.h"
#include "table_delete.h"
#include "table_insert.h"

#include "table_index.h"

using mysql::system::create_transport;
using mysql::Binary_log;

std::string cl_index_file;

class Incident_handler : public mysql::Content_handler
{
public:
 Incident_handler() : mysql::Content_handler() {}

 Binary_log_event *process_event(mysql::Incident_event *incident)
 {
   std::cout << "Event type: "
             << mysql::system::get_event_type_str(incident->get_event_type())
             << " length: " << incident->header()->event_length
             << " next pos: " << incident->header()->next_position
             << std::endl;
   std::cout << "type= "
             << (unsigned)incident->type
             << " message= "
             << incident->message
             <<  std::endl
             <<  std::endl;
   /* Consume the event */
   delete incident;
   return 0;
 }
};

class Applier : public mysql::Content_handler
{
public:
 Applier(Table_index *index)
 {
   m_table_index= index;
 }

 mysql::Binary_log_event *process_event(mysql::Row_event *rev)
 {
   boost::uint64_t table_id= rev->table_id;
   Int2event_map::iterator ti_it= m_table_index->find(table_id);
   if (ti_it == m_table_index->end ())
   {
     std::cout << "Table id "
               << table_id
               << " was not registered by any preceding table map event."
               << std::endl;
     return rev;
   }
   /*
    Each row event contains multiple rows and fields. The Row_iterator
    allows us to iterate one row at a time.
   */
   mysql::Row_event_set rows(rev, ti_it->second);
   /*
    Create a fuly qualified table name
   */
   std::ostringstream os;
   os << ti_it->second->db_name << '.' << ti_it->second->table_name;
   mysql::Row_event_set::iterator it= rows.begin();
   do {
     mysql::Row_of_fields fields= *it;
     if (rev->get_event_type() == mysql::WRITE_ROWS_EVENT)
       table_insert(os.str(),fields);
     if (rev->get_event_type() == mysql::UPDATE_ROWS_EVENT)
     {
       ++it;
       mysql::Row_of_fields fields2= *it;
       table_update(os.str(),fields,fields2);
     }
     if (rev->get_event_type() == mysql::DELETE_ROWS_EVENT)
       table_delete(os.str(),fields);
     } while (++it != rows.end());

     /* Consume the event */
     delete rev;
     return 0;
  }
private:
  Table_index *m_table_index;

};

/*
 *
 */
int main(int argc, char** argv)
{
  if (argc != 3)
  {
    fprintf(stderr,"Usage:\n\nmysql2lucene URL\n\nExample:\n\nmysql2lucene mysql://root@127.0.0.1:3306 myindexfile\n\n");
    return (EXIT_FAILURE);
  }

  Binary_log binlog(create_transport(argv[1]));


  cl_index_file.append (argv[2]);

  /*
    Attach a custom event content handlers
  */
  Incident_handler incident_hndlr;
  Table_index table_event_hdlr;
  Applier replay_hndlr(&table_event_hdlr);

  binlog.content_handler_pipeline()->push_back(&table_event_hdlr);
  binlog.content_handler_pipeline()->push_back(&incident_hndlr);
  binlog.content_handler_pipeline()->push_back(&replay_hndlr);

  if (binlog.connect())
  {
    fprintf(stderr,"Can't connect to the master.\n");
    return (EXIT_FAILURE);
  }

  binlog.set_position("searchbin.000001", 4);

  bool quit= false;
  while(!quit)
  {
    /*
     Pull events from the master. This is the heart beat of the event listener.
    */
    Binary_log_event  *event;
    binlog.wait_for_next_event(&event);

    /*
     Print the event
    */
    std::cout << "Event type: "
              << mysql::system::get_event_type_str(event->get_event_type())
              << " length: " << event->header()->event_length
              << " next pos: " << event->header()->next_position
              << std::endl;

    /*
     Perform a special action based on event type
    */

    switch(event->header()->type_code)
    {
    case mysql::QUERY_EVENT:
      {
        const mysql::Query_event *qev= static_cast<const mysql::Query_event *>(event);
        std::cout << "query= "
                  << qev->query
                  << " db= "
                  << qev->db_name
                  <<  std::endl
                  <<  std::endl;
        if (qev->query.find("DROP TABLE REPLICATION_LISTENER") != std::string::npos)
        {
          quit= true;
        }
      }
      break;

    case mysql::ROTATE_EVENT:
      {
        mysql::Rotate_event *rot= static_cast<mysql::Rotate_event *>(event);
        std::cout << "filename= "
                  << rot->binlog_file.c_str()
                  << " pos= "
                  << rot->binlog_pos
                  << std::endl
                  << std::endl;
      }
      break;

    } // end switch
    delete event;
  } // end loop
  return (EXIT_SUCCESS);
}
