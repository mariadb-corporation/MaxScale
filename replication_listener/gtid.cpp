/*
Copyright (C) 2013, MariaDB Corporation Ab


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

#include <boost/asio.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "gtid.h"
#include "listener_exception.h"
#include <mysql.h>
#include <my_global.h>
#include <my_byteorder.h>

namespace mysql
{

  Gtid::Gtid(const boost::uint32_t domain_id,
	     const boost::uint32_t server_id,
	     const boost::uint64_t sequence_number)
    : m_real_gtid(true),
      m_domain_id(domain_id),
      m_server_id(server_id),
      m_sequence_number(sequence_number),
      m_server_type(MYSQL_SERVER_TYPE_MARIADB)
  {
	  memset(m_mysql_gtid, 0, MYSQL_GTID_ENCODED_SIZE);

	  m_mariadb_gtid = to_string(m_domain_id) + std::string("-") + to_string(m_server_id) + std::string("-") + to_string(m_sequence_number);
	  m_gtid_length = m_mariadb_gtid.length();
  }

  Gtid::Gtid(const unsigned char *mysql_gtid,
	     const boost::uint64_t gno)
    :m_real_gtid(true),
     m_domain_id(0),
     m_server_id(0),
     m_sequence_number(gno),
     m_server_type(MYSQL_SERVER_TYPE_MYSQL),
     m_gtid_length(MYSQL_GTID_ENCODED_SIZE)
  {
	  memcpy(m_mysql_gtid, mysql_gtid, MYSQL_GTID_ENCODED_SIZE);
  }

  Gtid::Gtid(const unsigned char* mysql_gtid)
    :m_real_gtid(true),
     m_domain_id(0),
     m_server_id(0),
     m_sequence_number(0),
     m_server_type(MYSQL_SERVER_TYPE_MYSQL),
     m_gtid_length(MYSQL_GTID_ENCODED_SIZE)
  {
	  int i,k;
	  char tmp[2];
	  char *sid = (char *)mysql_gtid;

	  for(i=0,k=0; i < 16*2; i+=2,k++) {
		  unsigned int c;
		  tmp[0] = sid[i];
		  tmp[1] = sid[i+1];
		  sscanf((const char *)tmp, "%02x", &c);
		  m_mysql_gtid[k]=(unsigned char)c;
	  }
	  i++;
	  k++;
	  sscanf((const char *)&(sid[i]), "%lu", &m_sequence_number);
	  int8store(&(m_mysql_gtid[k]), m_sequence_number);

	  std::cout << "GTID:: " << m_mysql_gtid << " " << std::endl;
  }

  std::string Gtid::get_string() const
  {
	  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB) {
		  return (m_mariadb_gtid);
	  } else {
		  std::string hexs;
		  unsigned char *sid = (unsigned char *)m_mysql_gtid;
		  char tmp[2];

		  // Dump the encoded SID using hexadesimal representation
		  // Making it little bit more usefull
		  for(size_t i=0;i < 16;i++) {
			  sprintf((char *)tmp, "%02x", (unsigned char)sid[i]);
			  hexs.append(std::string((const char *)tmp));
		  }
		  return(hexs + std::string(":") + to_string(m_sequence_number));
	  }
   }

   const unsigned char* Gtid::get_gtid() const
   {
	  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB) {
		  return ((const unsigned char *)m_mariadb_gtid.c_str());
	  } else {
		  return (m_mysql_gtid);
	  }
   }
}


