/*
Copyright (C) 2013-2014, MariaDB Corporation Ab

This file is distributed as part of the MariaDB Corporation MaxScale. It is free
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

Author: Jan Lindström jan.lindstrom@mariadb.com

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

#define MYSQL_GTID_ENCODED_SIZE 24

class Gtid
{
 public:

  Gtid()
	  : m_real_gtid(false), m_domain_id(0), m_server_id(0), m_sequence_number(0),
		m_server_type(MYSQL_SERVER_TYPE_NA), m_gtid_length(0), m_mariadb_gtid(std::string(""))
  {
	  memset(m_mysql_gtid, 0, MYSQL_GTID_ENCODED_SIZE);
  }

  Gtid(const boost::uint32_t domain_id,
       const boost::uint32_t server_id,
       const boost::uint64_t sequence_number);

  Gtid(const unsigned char *mysql_gtid,
       const boost::uint64_t gno);

  Gtid(const unsigned char *mysql_gtid);

  ~Gtid() {}
 
  bool is_real_gtid() const { return m_real_gtid;}

  const unsigned char* get_mysql_gtid() const { return m_mysql_gtid; }

  const unsigned char* get_gtid() const;

  size_t get_gtid_length() const { return m_gtid_length; }


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
  boost::uint32_t m_gtid_length;

  unsigned char m_mysql_gtid[MYSQL_GTID_ENCODED_SIZE];
  std::string m_mariadb_gtid;
};

}

#endif
