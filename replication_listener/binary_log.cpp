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
- Added support for setting binlog position based on GTID
- Added support for MySQL and MariDB server types

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/

#include <list>

#include "binlog_api.h"
#include <boost/foreach.hpp>

using namespace mysql;
using namespace mysql::system;
namespace mysql
{

/*
Return server type string.
*/

const char *mysql_server_type_str(mysql_server_types server_type)
{
  switch(server_type) {
    case MYSQL_SERVER_TYPE_MARIADB:  return "MariaDB";
    case MYSQL_SERVER_TYPE_MYSQL:    return "MySQL";
    default:                         return "Unknown";
  }
}

Binary_log::Binary_log(Binary_log_driver *drv) : m_binlog_position(4), m_binlog_file(""), m_uri("")
{
  if (drv == NULL)
  {
    m_driver= &m_dummy_driver;
  }
  else
   m_driver= drv;
}

Binary_log::Binary_log(Binary_log_driver *drv, std::string uri) : m_binlog_position(4), m_binlog_file(""), m_uri(uri)
{
  if (drv == NULL)
  {
    m_driver= &m_dummy_driver;
  }
  else
   m_driver= drv;
}

Content_handler_pipeline *Binary_log::content_handler_pipeline(void)
{
  return &m_content_handlers;
}

int Binary_log::wait_for_next_event(mysql::Binary_log_event **event_ptr)
{
  int rc;
  bool handler_code;
  mysql::Binary_log_event *event;

  mysql::Injection_queue reinjection_queue;

  do {
    handler_code= false;
    if (!reinjection_queue.empty())
    {
      event= reinjection_queue.front();
      reinjection_queue.pop_front();
    }
    else
    {
      // Return in case of non-ERR_OK.
      if(rc= m_driver->wait_for_next_event(&event))
        return rc;
    }
    m_binlog_position= event->header()->next_position;
    mysql::Content_handler *handler;

    BOOST_FOREACH(handler, m_content_handlers)
    {
      if (event)
      {
        handler->set_injection_queue(&reinjection_queue);
        event= handler->internal_process_event(event);
      }
    }
  } while(event == 0 || !reinjection_queue.empty());

  if (event_ptr)
    *event_ptr= event;

  return 0;
}

int Binary_log::set_position(const std::string &filename, unsigned long position)
{
  int status= m_driver->set_position(filename, position);
  if (status == ERR_OK)
  {
    m_binlog_file= filename;
    m_binlog_position= position;
  }
  return status;
}

int Binary_log::set_position(unsigned long position)
{
  std::string filename;
  m_driver->get_position(&filename, NULL);
  return this->set_position(filename, position);
}

int Binary_log::set_position_gtid(const Gtid gtid)
{
  return this->set_position_gtid(gtid);
}

unsigned long Binary_log::get_position(void)
{
  return m_binlog_position;
}

unsigned long Binary_log::get_position(std::string &filename)
{
  m_driver->get_position(&m_binlog_file, &m_binlog_position);
  filename= m_binlog_file;
  return m_binlog_position;
}

int Binary_log::connect()
{
  return m_driver->connect();
}

int Binary_log::connect(const boost::uint64_t binlog_pos)
{
  return m_driver->connect(binlog_pos);
}

int Binary_log::connect(const Gtid gtid)
{
  return m_driver->connect(gtid);
}

mysql_server_types Binary_log::get_mysql_server_type() const
{
  return m_driver->get_mysql_server_type();
}

const char *Binary_log::get_mysql_server_type_str() const
{
  return mysql_server_type_str(get_mysql_server_type());
}

void Binary_log::shutdown()
{
  m_driver->shutdown();
}

}
