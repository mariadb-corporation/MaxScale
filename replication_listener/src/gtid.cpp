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

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "gtid.h"
#include "listener_exception.h"

namespace mysql
{

  Gtid::Gtid(const boost::uint32_t domain_id,
	     const boost::uint32_t server_id,
	     const boost::uint64_t sequence_number)
    : m_real_gtid(true),
      m_domain_id(domain_id),
      m_server_id(server_id),
      m_sequence_number(sequence_number),
      m_mysql_gtid(""),
      m_server_type(MYSQL_SERVER_TYPE_MARIADB)
  {
  }

  Gtid::Gtid(const std::string& mysql_gtid,
	     const boost::uint64_t gno)
    :m_real_gtid(true),
     m_domain_id(0),
     m_server_id(0),
     m_sequence_number(gno),
     m_mysql_gtid(mysql_gtid),
     m_server_type(MYSQL_SERVER_TYPE_MYSQL)
  {
  }

  std::string Gtid::get_string() const
  {
	  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB) {
		  return (to_string(m_domain_id) + std::string("-") + to_string(m_server_id) + std::string("-") + to_string(m_sequence_number));
	  } else {
		  std::string hexs;
		  unsigned char *sid = (unsigned char *)m_mysql_gtid.c_str();
		  unsigned char tmp[5];

		  // Dump the encoded SID using hexadesimal representation
		  // Making it little bit more usefull
		  for(size_t i=0;i < 16;i++) {
			  sprintf((char *)tmp, "%x", (unsigned char)sid[i]);
			  hexs.append(std::string((const char *)tmp));
		  }
		  return(hexs + std::string(":") + to_string(m_sequence_number));
	  }
  }

}
