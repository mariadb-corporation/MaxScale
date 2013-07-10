/*
Copyright (C) 2013, SkySQL Ab


This file is distributed as part of the SkySQL Gateway. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Author: Jan Lindström jan.lindstrom@skysql.com

*/

#ifndef REPLICATION_LISTENER_MYSQL_GTID_H
#define REPLICATION_LISTENER_MYSQL_GTID_H

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

namespace mysql
{

template <class T>
inline std::string gno_to_string (const T& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

enum mysql_server_types {
  MYSQL_SERVER_TYPE_NA = 0,
  MYSQL_SERVER_TYPE_MARIADB = 1,
  MYSQL_SERVER_TYPE_MYSQL = 2
};

class Gtid
{
 public:

  Gtid() 
    : m_real_gtid(false), m_domain_id(0), m_server_id(0), m_sequence_number(0), m_mysql_gtid(""), m_server_type(MYSQL_SERVER_TYPE_NA)
  {}

  Gtid(const boost::uint32_t domain_id,
       const boost::uint32_t server_id,
       const boost::uint64_t sequence_number);

  Gtid(const std::string &mysql_gtid,
       const boost::uint64_t gno);

  Gtid(const std::string &mysql_gtid);

  ~Gtid() {}

  bool is_real_gtid() const { return m_real_gtid;}

  const std::string& get_mysql_gtid() const { return m_mysql_gtid; }

  std::string get_string() const;

  boost::uint32_t get_domain_id() const { return m_domain_id; }
  boost::uint32_t get_server_id() const { return m_server_id; }
  boost::uint32_t get_sequence_number() const { return m_sequence_number; }

 private:

  bool m_real_gtid;
  mysql_server_types m_server_type;

  boost::uint32_t m_domain_id;
  boost::uint32_t m_server_id;
  boost::uint64_t m_sequence_number;

  std::string m_mysql_gtid;
};

}

#endif
