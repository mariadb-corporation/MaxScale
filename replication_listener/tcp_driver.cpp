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
- Added support for GTID event handling for both MySQL and MariaDB
- Added support for starting binlog dump from GTID position
- Added error handling using exceptions

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/
#include "binlog_api.h"
#include <iostream>
#include "tcp_driver.h"

#include <fstream>
#include <time.h>
#include <boost/cstdint.hpp>
#include <streambuf>
#include <stdio.h>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <exception>
#include <boost/foreach.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <boost/lexical_cast.hpp>

#include "protocol.h"
#include "binlog_event.h"
#include "listener_exception.h"
#include "rowset.h"
#include "field_iterator.h"

using boost::asio::ip::tcp;
using namespace mysql::system;
using namespace mysql;

typedef unsigned char uchar;

namespace mysql { namespace system {

#include <mysql.h>

boost::mutex mysql_mutex;

static int encrypt_password(boost::uint8_t *reply,   /* buffer at least EVP_MAX_MD_SIZE */
                            const boost::uint8_t *scramble_buff,
                            const char *pass);
static int hash_sha1(boost::uint8_t *output, ...);

/*
In this function we announce for MariaDB server the client capabilities and use
the MARIA_SLAVE_CAPABILITY_GTID = 4. It would be better to use some real include
file, but currently define is located at log_event.h and that file is not part of
the make install rules and we would not find it from MySQL server anyway.

*/
bool Binlog_tcp_driver::send_client_capabilites(tcp::socket *socket)
{
  boost::asio::streambuf server_messages;
  unsigned long packet_length;
  unsigned char packet_no;

  std::ostream command_request_stream(&server_messages);

  /* Form a query event */
  static boost::uint8_t com_query = COM_QUERY;
  Protocol_chunk<boost::uint8_t> prot_command(com_query);

  command_request_stream << prot_command
          << "SET @mariadb_slave_capability=4";

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  // Send the request.
  boost::asio::write(*socket, boost::asio::buffer(command_packet_header, 4), boost::asio::transfer_at_least(4));
  boost::asio::write(*socket, server_messages, boost::asio::transfer_at_least(size));

  // Get Ok-package
  packet_length=proto_get_one_package(socket, server_messages, &packet_no);

  std::istream cmd_response_stream(&server_messages);

  boost::uint8_t result_type;
  Protocol_chunk<boost::uint8_t> prot_result_type(result_type);

  cmd_response_stream >> prot_result_type;

  if (result_type == 0)
  {
    struct st_ok_package ok_package;
    prot_parse_ok_message(cmd_response_stream, ok_package, packet_length);
  } else
  {
    struct st_error_package error_package;
    prot_parse_error_message(cmd_response_stream, error_package, packet_length);

    throw(ListenerException(std::string("Sending client capabilities failed: ") + std::string(error_package.message), __FILE__, __LINE__ ));
  }

  return false;
}

/*
In this function we set slave connection state based on global transaction
identifier. That value is later used by MariaDB server as a position where
binlog reading is started.
*/
bool Binlog_tcp_driver::send_slave_connect_state(tcp::socket *socket,Gtid gtid)
{
  boost::asio::streambuf server_messages;
  unsigned long packet_length;
  unsigned char packet_no;

  std::ostream command_request_stream(&server_messages);

  /* Form a query event */
  static boost::uint8_t com_query = COM_QUERY;
  Protocol_chunk<boost::uint8_t> prot_command(com_query);

  command_request_stream << prot_command
			 << "SET @slave_connect_state='" << gtid.get_domain_id()
			 << "-" << gtid.get_server_id()
			 << "-" << gtid.get_sequence_number()
			 << "'";

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  // Send the request.
  boost::asio::write(*socket, boost::asio::buffer(command_packet_header, 4), boost::asio::transfer_at_least(4));
  boost::asio::write(*socket, server_messages, boost::asio::transfer_at_least(size));

  // Get Ok-package
  packet_length=proto_get_one_package(socket, server_messages, &packet_no);

  std::istream cmd_response_stream(&server_messages);

  boost::uint8_t result_type;
  Protocol_chunk<boost::uint8_t> prot_result_type(result_type);

  cmd_response_stream >> prot_result_type;

  if (result_type == 0)
  {
    struct st_ok_package ok_package;
    prot_parse_ok_message(cmd_response_stream, ok_package, packet_length);
  } else
  {
    struct st_error_package error_package;
    prot_parse_error_message(cmd_response_stream, error_package, packet_length);
    throw(ListenerException(std::string("Send slave connect state failed: ") + std::string(error_package.message), __FILE__, __LINE__ ));
  }

  return false;
}

/*
In this function we fetch @@global.binlog_checksum variable value from the
master and based on that set @master_binlog_checksum variable to this slave.
*/
bool Binlog_tcp_driver::get_master_binlog_checksum(tcp::socket *socket)
{
  boost::asio::streambuf server_messages;
  unsigned long packet_length;
  unsigned char packet_no;

  std::ostream command_request_stream(&server_messages);

  /* Form a query event */
  static boost::uint8_t com_query = COM_QUERY;
  Protocol_chunk<boost::uint8_t> prot_command(com_query);

  command_request_stream << prot_command
			 << "SET @master_binlog_checksum= @@global.binlog_checksum";

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  // Send the request.
  boost::asio::write(*socket, boost::asio::buffer(command_packet_header, 4), boost::asio::transfer_at_least(4));
  boost::asio::write(*socket, server_messages, boost::asio::transfer_at_least(size));

  // Get Ok-package
  packet_length=proto_get_one_package(socket, server_messages, &packet_no);

  std::istream cmd_response_stream(&server_messages);

  boost::uint8_t result_type;
  Protocol_chunk<boost::uint8_t> prot_result_type(result_type);

  cmd_response_stream >> prot_result_type;

  if (result_type == 0)
  {
    struct st_ok_package ok_package;
    prot_parse_ok_message(cmd_response_stream, ok_package, packet_length);
  } else
  {
    struct st_error_package error_package;
    prot_parse_error_message(cmd_response_stream, error_package, packet_length);
    throw(ListenerException(std::string("Send slave connect state failed: ") + std::string(error_package.message), __FILE__, __LINE__ ));
  }

  return false;
}

/* In this function we create temporal connection to the server and find out the server
version. Don't know if this is really the only way to do this, but it seems to work.

Currently we support MariaDB and MySQL servers.
*/
int Binlog_tcp_driver::fetch_server_version(const std::string& user,
	                                    const std::string& passwd,
					    const std::string& host,
					    long port)
{
  /* Need to serialize access to MySQL options */
  boost::mutex::scoped_lock lock(mysql_mutex);
  MYSQL *mysql=NULL;
  my_bool reconnect= true;
  char *server_name=NULL;

  mysql= mysql_init(NULL);

  if (!mysql)
  {
    throw(ListenerException(std::string("mysql_init() failed"), __FILE__, __LINE__));
  }


  mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
  mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
  mysql_options(mysql, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

  if ( !mysql_real_connect(mysql, host.c_str(), user.c_str(),
                           passwd.c_str(), NULL, port,
                           NULL, 0) )
  {
    throw(ListenerException(std::string("mysql_real_connect() failed"), __FILE__, __LINE__));
  }

  // std::cerr << " Server " << mysql->server_version << std::endl;

  if ( strstr(mysql->server_version, "Maria") ||
       strstr(mysql->server_version, "maria"))
  {
    m_server_type = MYSQL_SERVER_TYPE_MARIADB;
  } else {
    // Currently assuming MySQL
    m_server_type = MYSQL_SERVER_TYPE_MYSQL;
  }

  mysql_close(mysql);

  return false;

}

int Binlog_tcp_driver::connect(const std::string& user, const std::string& passwd,
			       const std::string& host, long port,
			       const Gtid gtid,
			       const std::string& binlog_filename, size_t offset)
{
  m_user=user;
  m_passwd=passwd;
  m_host=host;
  m_port=port;

  if (!m_socket)
  {
    if ((m_socket=sync_connect_and_authenticate(m_io_service, user, passwd, host, port)) == 0)
      return 1;
  }

  if(fetch_server_version(user, passwd, host, port))
    return 1;

  /* Need to get master status if we do not know global transaction ID */
  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB
      && !gtid.is_real_gtid())
  {
    /**
     * Get the master status if we don't know the name of the file.
     */
    if (binlog_filename == "")
    {
      if (fetch_master_status(m_socket, &m_binlog_file_name, &m_binlog_offset))
	return 1;
    } else {
      m_binlog_file_name=binlog_filename;
      m_binlog_offset=offset;
    }
  }

  /* Send client capabilities to master, done only for MariaDB server */
  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB) {
    send_client_capabilites(m_socket);
  }
  /* Not yet sure if something similar is needed for MySQL */

  /* Set up the client binlog checksum variable based on master. This is
  needed at least on MySQL servers with >=5.6.6. */
  get_master_binlog_checksum(m_socket);

  /* Send slave connect state to master, done only for MariaDB server
     when GTID is used.
  */

  if (m_server_type == MYSQL_SERVER_TYPE_MARIADB
      && gtid.is_real_gtid() == true) {

    send_slave_connect_state(m_socket, gtid);
  }

  /* We're ready to start the io service and request the binlog dump. 
     For MySQL if we use GTID for binlog positioning, we need to send
     a special COM_BINLOG_DUMP_GTID command. For MariaDB we have
     already set up all the necessary information and we can
     use COM_BINLOG_DUMP.
  */

  if (gtid.is_real_gtid() && 
      m_server_type == MYSQL_SERVER_TYPE_MYSQL) {
    start_binlog_dump(gtid);
  } else {
    start_binlog_dump(m_binlog_file_name, m_binlog_offset);
  }

  return 0;
}

tcp::socket *Binlog_tcp_driver::sync_connect_and_authenticate(boost::asio::io_service &io_service, const std::string &user, const std::string &passwd, const std::string &host, long port)
{

  tcp::resolver resolver(io_service);
  tcp::resolver::query query(host.c_str(), "0");

  boost::system::error_code error=boost::asio::error::host_not_found;

  if (port == 0)
    port= 3306;

  tcp::socket *socket=new tcp::socket(io_service);
  /*
    Try each endpoint until we successfully establish a connection.
   */
  try {
  tcp::resolver::iterator endpoint_iterator=resolver.resolve(query);
  tcp::resolver::iterator end;

  while (error && endpoint_iterator != end)
  {
    /*
      Hack to set port number from a long int instead of a service.
     */
    tcp::endpoint endpoint=endpoint_iterator->endpoint();
    endpoint.port(port);

    socket->close();
    socket->connect(endpoint, error);
    endpoint_iterator++;
  }
  } catch(...)
  {
    throw(ListenerException(std::string("Connection to host: ") + host + std::string(" failed: ") + error.message(), __FILE__, __LINE__));
  }

  if (error)
  {
    throw(ListenerException(std::string("Connection to host: ") + host + std::string(" failed: ") + error.message(), __FILE__, __LINE__));
  }


  /*
   * Successfully connected to the master.
   * 1. Accept handshake from server
   * 2. Send authentication package to the server
   * 3. Accept OK server package (or error in case of failure)
   * 4. Send COM_REGISTER_SLAVE command to server
   * 5. Accept OK package from server
   */

  boost::asio::streambuf server_messages;

  /*
   * Get package header
   */
  unsigned long packet_length;
  unsigned char packet_no;

  // Below will thow a exception if fails
  proto_read_package_header(socket, server_messages, &packet_length, &packet_no);

  /*
   * Get server handshake package
   */
  std::streamsize inbuffer=server_messages.in_avail();
  if (inbuffer < 0)
    inbuffer=0;
  boost::asio::read(*socket, server_messages, boost::asio::transfer_at_least(packet_length - inbuffer));
  std::istream server_stream(&server_messages);

  struct st_handshake_package handshake_package;

  proto_get_handshake_package(server_stream, handshake_package, packet_length);

  if (authenticate(socket, user, passwd, handshake_package))
    return 0;

  /*
   * Register slave to master
   */
  std::ostream command_request_stream(&server_messages);


  
  static boost::uint8_t com_register_slave = COM_REGISTER_SLAVE;
  boost::uint32_t server_id = 5;
  boost::uint32_t rpl_recovery_rank = 0;
  
  Protocol_chunk<boost::uint8_t> prot_command(com_register_slave);
  Protocol_chunk<boost::uint16_t> prot_connection_port(port);
  Protocol_chunk<boost::uint32_t> prot_rpl_recovery_rank(rpl_recovery_rank);
  Protocol_chunk<boost::uint32_t> prot_server_id(server_id);

  const char* env_libreplication_server_id = std::getenv("LIBREPLICATION_SERVER_ID");

  if (env_libreplication_server_id != 0) {
    try {
      boost::uint32_t libreplication_server_id = boost::lexical_cast<boost::uint32_t>(env_libreplication_server_id);
      prot_server_id = libreplication_server_id;
    } catch (boost::bad_lexical_cast const& e) {
      throw(ListenerException(std::string("Lexical cast failed: ") + e.what(), __FILE__, __LINE__));
    }
  }

  boost::uint32_t master_server_id = 0;
  boost::uint8_t host_size = host.size();
  boost::uint8_t user_size = user.size();
  boost::uint8_t passwd_size = passwd.size();
  
  Protocol_chunk<boost::uint32_t> prot_master_server_id(master_server_id);
  Protocol_chunk<boost::uint8_t> prot_report_host_strlen(host_size);
  Protocol_chunk<boost::uint8_t> prot_user_strlen(user_size);
  Protocol_chunk<boost::uint8_t> prot_passwd_strlen(passwd_size);

  command_request_stream << prot_command
          << prot_server_id
          << prot_report_host_strlen
          << host
          << prot_user_strlen
          << user
          << prot_passwd_strlen
          << passwd
          << prot_connection_port
          << prot_rpl_recovery_rank
          << prot_master_server_id;

  int size=server_messages.size();
  char command_packet_header[4];
  try {
    write_packet_header(command_packet_header, size, 0);

    // Send the request.
    boost::asio::write(*socket,
                       boost::asio::buffer(command_packet_header, 4),
                       boost::asio::transfer_at_least(4));
    boost::asio::write(*socket, server_messages,
                       boost::asio::transfer_at_least(size));
  } catch( boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Slave registration failed: ") + e.message(), __FILE__, __LINE__));
  }

  // Get Ok-package
  packet_length=proto_get_one_package(socket, server_messages, &packet_no);

  std::istream cmd_response_stream(&server_messages);

  boost::uint8_t result_type;
  Protocol_chunk<boost::uint8_t> prot_result_type(result_type);

  cmd_response_stream >> prot_result_type;

  if (result_type == 0)
  {
    struct st_ok_package ok_package;
    prot_parse_ok_message(cmd_response_stream, ok_package, packet_length);
  } else
  {
    struct st_error_package error_package;
    prot_parse_error_message(cmd_response_stream, error_package, packet_length);
    throw(ListenerException(std::string("Slave registration failed: ") + std::string(error_package.message), __FILE__, __LINE__));
  }

  return socket;
}

void Binlog_tcp_driver::start_binlog_dump(const std::string &binlog_file_name, size_t offset)
{
  boost::asio::streambuf server_messages;

  std::ostream command_request_stream(&server_messages);

  static boost::uint8_t com_binlog_dump = COM_BINLOG_DUMP;
  static boost::uint16_t binlog_flags = 0;
  static boost::uint32_t server_id = 1;
  Protocol_chunk<boost::uint8_t>  prot_command(com_binlog_dump);
  Protocol_chunk<boost::uint32_t> prot_binlog_offset(offset); // binlog position to start at
  Protocol_chunk<boost::uint16_t> prot_binlog_flags(binlog_flags); // not used
  Protocol_chunk<boost::uint32_t> prot_server_id(server_id); // must not be 0; see handshake package

  command_request_stream
          << prot_command
          << prot_binlog_offset
          << prot_binlog_flags
          << prot_server_id
          << binlog_file_name;

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  // Send the request.
  try {
    boost::asio::write(*m_socket,
		       boost::asio::buffer(command_packet_header, 4),
		       boost::asio::transfer_at_least(4));
    boost::asio::write(*m_socket, server_messages,
		       boost::asio::transfer_at_least(size));
  }
  catch(boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Binlog dump command failed: ") + e.message(), __FILE__, __LINE__));
  }

  /*
   Start receiving binlog events.
   */
  if (!m_shutdown)
    GET_NEXT_PACKET_HEADER;

  /*
   Start the event loop in a new thread
   */
  if (!m_event_loop)
    m_event_loop= new boost::thread(boost::bind(&Binlog_tcp_driver::start_event_loop, this));

}

/*

In MySQL server we need to start binlog dump with a new command
COM_BINLOG_DUMP_GTID if we want to use GTID positioning.
*/
void Binlog_tcp_driver::start_binlog_dump(const Gtid gtid)
{
  boost::asio::streambuf server_messages;
  size_t offset=0;

  std::ostream command_request_stream(&server_messages);

  static boost::uint8_t com_binlog_dump = COM_BINLOG_DUMP_GTID;
  static boost::uint16_t binlog_flags = 0;
  static boost::uint32_t server_id = 5;
  const std::string binlog_file_name="\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
  static boost::uint64_t pos = 4;
  static boost::uint32_t binlog_name_size = 0;
  static boost::uint32_t gtid_size = 0;

  Protocol_chunk<boost::uint8_t>  prot_command(com_binlog_dump);
  Protocol_chunk<boost::uint16_t> prot_binlog_flags(binlog_flags); // not used
  Protocol_chunk<boost::uint32_t> prot_server_id(server_id); // must not be
							     // 0; see
							     // handshake
							     // package
  Protocol_chunk<boost::uint64_t> prot_pos(pos);
  Protocol_chunk<boost::uint32_t> prot_binlog_name_size(binlog_name_size);
  gtid_size = MYSQL_GTID_ENCODED_SIZE;
  Protocol_chunk<boost::uint32_t> prot_gtid_size(gtid_size);

  command_request_stream
          << prot_command
          << prot_binlog_flags
          << prot_server_id
	  << prot_binlog_name_size
          << binlog_file_name
	  << prot_pos
          << prot_gtid_size;

  // Need to do special handling because GTTID is encoded and can
  // contain \0 characters.
  command_request_stream.write((const char *)gtid.get_mysql_gtid(), MYSQL_GTID_ENCODED_SIZE);

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  try {
    // Send the request.
    boost::asio::write(*m_socket,
		       boost::asio::buffer(command_packet_header, 4),
		       boost::asio::transfer_at_least(4));
    boost::asio::write(*m_socket, server_messages,
		       boost::asio::transfer_at_least(size));
  }
  catch(boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Binlog dump with gtid command failed: ") + e.message(), __FILE__, __LINE__));
  }

  /*
   Start receiving binlog events.
   */
  if (!m_shutdown)
    GET_NEXT_PACKET_HEADER;

  /*
   Start the event loop in a new thread
   */
  if (!m_event_loop)
    m_event_loop= new boost::thread(boost::bind(&Binlog_tcp_driver::start_event_loop, this));

}

/**
 Helper function used to extract the event header from a memory block
 */
static void proto_event_packet_header(boost::asio::streambuf &event_src, Log_event_header *h)
{
  std::istream is(&event_src);

  Protocol_chunk<boost::uint8_t> prot_marker(h->marker);
  Protocol_chunk<boost::uint32_t> prot_timestamp(h->timestamp);
  Protocol_chunk<boost::uint8_t> prot_type_code(h->type_code);
  Protocol_chunk<boost::uint32_t> prot_server_id(h->server_id);
  Protocol_chunk<boost::uint32_t> prot_event_length(h->event_length);
  Protocol_chunk<boost::uint32_t> prot_next_position(h->next_position);
  Protocol_chunk<boost::uint16_t> prot_flags(h->flags);

  is >> prot_marker
          >> prot_timestamp
          >> prot_type_code
          >> prot_server_id
          >> prot_event_length
          >> prot_next_position
          >> prot_flags;

}

void Binlog_tcp_driver::handle_net_packet(const boost::system::error_code& err, std::size_t bytes_transferred)
{
  if (err)
  {
    Binary_log_event * ev= create_incident_event(175, err.message().c_str(), m_binlog_offset);
    m_event_queue->push_front(ev);
    return;
  }

  if (bytes_transferred > MAX_PACKAGE_SIZE || bytes_transferred == 0)
  {
    std::ostringstream os;
    os << "Expected byte size to be between 0 and "
       << MAX_PACKAGE_SIZE
       << " number of bytes; got "
       << bytes_transferred
       << " instead.";
    Binary_log_event * ev= create_incident_event(175, os.str().c_str(), m_binlog_offset);
    m_event_queue->push_front(ev);
    return;
  }

  //assert(m_waiting_event != 0);
  //std::cerr << "Committing '"<< bytes_transferred << "' bytes to the event stream." << std::endl;
  //std::cerr << m_event_stream_buffer << std::endl;

  m_event_stream_buffer.commit(bytes_transferred);
  /*
    If the event object doesn't have an event length it means that the header
    hasn't been parsed. If the event stream also contains enough bytes
    we make the assumption that the next bytes waiting in the stream is
    the event header and attempt to parse it.
   */
  if (m_waiting_event->event_length == 0 && m_event_stream_buffer.size() >= 19)
  {
    /*
      Copy and remove from the event stream, the remaining bytes might be
      dynamic payload.
     */
    //std::cerr << "Consuming event stream for header. Size before: " << m_event_stream_buffer.size() << std::endl;
    proto_event_packet_header(m_event_stream_buffer, m_waiting_event);
    //std::cerr << " Size after: " << m_event_stream_buffer.size() << std::endl;
  }

  //std::cerr << "Event length: and available payload size is " << m_event_stream_buffer.size()+LOG_EVENT_HEADER_SIZE-1 <<  std::endl;
  if (m_waiting_event->event_length == m_event_stream_buffer.size() + LOG_EVENT_HEADER_SIZE - 1)
  {
    /*
     If the header length equals the size of the payload plus the
     size of the header, the event object is complete.
     Next we need to parse the payload buffer
     */
    std::istream is(&m_event_stream_buffer);
    Binary_log_event * event= parse_event(is, m_waiting_event);

    m_event_stream_buffer.consume(m_event_stream_buffer.size());

    m_event_queue->push_front(event);

    /*
      Note on memory management: The pushed Binary_log_event will be
      deleted in user land.
    */
    delete m_waiting_event;
    m_waiting_event= 0;
  }

  if (!m_shutdown)
    GET_NEXT_PACKET_HEADER;

}

void Binlog_tcp_driver::handle_net_packet_header(const boost::system::error_code& err, std::size_t bytes_transferred)
{
  if (err)
  {
    Binary_log_event * ev= create_incident_event(175, err.message().c_str(), m_binlog_offset);
    m_event_queue->push_front(ev);
    return;
  }

  if (bytes_transferred != 4)
  {
    std::ostringstream os;
    os << "Expected byte size to be between 0 and "
       << MAX_PACKAGE_SIZE
       << " number of bytes; got "
       << bytes_transferred
       << " instead.";
    Binary_log_event * ev= create_incident_event(175, os.str().c_str(), m_binlog_offset);
    m_event_queue->push_front(ev);
    return;
  }

  int packet_length=(unsigned long) (m_net_header[0] &0xFF);
  packet_length+=(unsigned long) ((m_net_header[1] &0xFF) << 8);
  packet_length+=(unsigned long) ((m_net_header[2] &0xFF) << 16);

  // TODO validate packet sequence numbers
  //int packet_no=(unsigned char) m_net_header[3];

  if (m_waiting_event == 0)
  {
    //std::cerr << "event_stream_buffer.size= " << m_event_stream_buffer.size() << std::endl;
    m_waiting_event= new Log_event_header();
    m_event_packet=  boost::asio::buffer_cast<char *>(m_event_stream_buffer.prepare(packet_length));
    //assert(m_event_stream_buffer.size() == 0);
  }

  boost::asio::async_read(*m_socket,
                          boost::asio::buffer(m_event_packet, packet_length),
                          boost::bind(&Binlog_tcp_driver::handle_net_packet,
                                      this,
                                      boost::asio::placeholders::error,
                                      boost::asio::placeholders::bytes_transferred));

}

int Binlog_tcp_driver::authenticate(tcp::socket *socket, const std::string& user, const std::string& passwd,
                     const st_handshake_package &handshake_package)
{
  try
  {
    /*
     * Send authentication package
     */
    boost::asio::streambuf auth_request_header;
    boost::asio::streambuf auth_request;
    std::string database("mysql"); // 0 terminated


    std::ostream request_stream(&auth_request);

    boost::uint8_t filler_buffer[23];
    memset((char *) filler_buffer, '\0', 23);

    boost::uint8_t reply[EVP_MAX_MD_SIZE];
    memset(reply, '\0', EVP_MAX_MD_SIZE);
    boost::uint8_t scramble_buff[21];
    memcpy(scramble_buff, handshake_package.scramble_buff, 8);
    memcpy(scramble_buff+8, handshake_package.scramble_buff2, 13);
    int passwd_length= 0;
    if (passwd.size() > 0)
      passwd_length= encrypt_password(reply, scramble_buff, passwd.c_str());

    // Turn off CLIENT_CONNECT_ATTRS (1UL << 20) found from MySQL 5.6.x
    // Turn off CLIENT_PLUGIN_AUTH  (1UL << 19)
    // Turn off CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA (1UL << 21) found from
    // MySQL 5.6.x
    static boost::uint32_t client_basic_flags = (CLIENT_BASIC_FLAGS & ~(1UL << 20)
	    & ~(1UL << 19) & ~(1UL << 21));

    static boost::uint32_t max_packet_size = MAX_PACKAGE_SIZE;

    Protocol_chunk<boost::uint32_t> prot_client_flags(client_basic_flags);
    Protocol_chunk<boost::uint32_t> prot_max_packet_size(max_packet_size);
    Protocol_chunk<boost::uint8_t>  prot_charset_number(handshake_package.server_language);
    Protocol_chunk<boost::uint8_t>  prot_filler_buffer(filler_buffer, 23);
    Protocol_chunk<boost::uint8_t>  prot_scramble_buffer_size((boost::uint8_t) passwd_length);
    Protocol_chunk<boost::uint8_t>  prot_scamble_buffer((boost::uint8_t *)reply, passwd_length);

    request_stream << prot_client_flags
                   << prot_max_packet_size
                   << prot_charset_number
                   << prot_filler_buffer
                   << user << '\0'
                   << prot_scramble_buffer_size
                   << prot_scamble_buffer
                   << database << '\0';


    int size=auth_request.size();
    char auth_packet_header[4];

    write_packet_header(auth_packet_header, size, 1);

    /*
     *  Send the request.
     */
    boost::asio::write(*socket, boost::asio::buffer(auth_packet_header, 4),
                       boost::asio::transfer_at_least(4));
    boost::asio::write(*socket, auth_request,
                       boost::asio::transfer_at_least(size));

    /*
     * Get server authentication response
     */
    unsigned long packet_length;
    unsigned char packet_no=1;
    packet_length=proto_get_one_package(socket, auth_request, &packet_no);

    std::istream auth_response_stream(&auth_request);

    boost::uint8_t result_type;
    Protocol_chunk<boost::uint8_t> prot_result_type(result_type);


    auth_response_stream >> prot_result_type;

    if (result_type == 0)
    {
      struct st_ok_package ok_package;
      prot_parse_ok_message(auth_response_stream, ok_package, packet_length);
    } else
    {
      struct st_error_package error_package;
      prot_parse_error_message(auth_response_stream, error_package, packet_length);
      throw(ListenerException(std::string("Authentication failed: ") + std::string(error_package.message), __FILE__, __LINE__));
    }

    return 0;
  } 
  catch (boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Authentication failed: ") + e.message(), __FILE__, __LINE__));
  }
  catch (boost::system::system_error const& e)
  {
    throw(ListenerException(std::string("Authentication failed: ") + std::string(e.what()), __FILE__, __LINE__));
  }
  catch (ListenerException const& e)
  {
    throw; // Forward
  }
}

int Binlog_tcp_driver::wait_for_next_event(mysql::Binary_log_event **event_ptr)
{
  // poll for new event until one event is found.
  // return the event
  if (event_ptr)
    *event_ptr= 0;
  m_event_queue->pop_back(event_ptr);
  return 0;
}

void Binlog_tcp_driver::start_event_loop()
{
  while (true)
  {
    boost::system::error_code err;
    int executed_jobs=m_io_service.run(err);
    if (err)
    {
      throw(ListenerException(std::string("Even loop io_service-run failed: ") + err.message(), __FILE__, __LINE__));
    }

    /*
      This function must be called prior to any second or later set of
      invocations of the run(), run_one(), poll() or poll_one() functions when
      a previous invocation of these functions returned due to the io_service
      being stopped or running out of work. This function allows the io_service
      to reset any internal state, such as a "stopped" flag.
    */
    m_io_service.reset();

    /*
      Don't shutdown until the io service has reset!
    */
    if (m_shutdown)
    {
      m_shutdown= false;
      break;
    }

    reconnect();
  }

}

int Binlog_tcp_driver::connect(const Gtid gtid)
{
  return connect(m_user, m_passwd, m_host, m_port, gtid);
}

int Binlog_tcp_driver::connect()
{
  Gtid gtid = Gtid();
  return connect(m_user, m_passwd, m_host, m_port, gtid);
}

int Binlog_tcp_driver::connect(boost::uint64_t binlog_pos)
{
  Gtid gtid = Gtid();
  std::string bf = std::string("");
  return connect(m_user, m_passwd, m_host, m_port, gtid, bf, (size_t)binlog_pos);
}

/**
 * Make synchronous reconnect.
 */
void Binlog_tcp_driver::reconnect(const Gtid gtid)
{
  disconnect();
  connect(m_user, m_passwd, m_host, m_port, gtid);
}

void Binlog_tcp_driver::disconnect()
{
  Binary_log_event * event;
  m_waiting_event= 0;
  m_event_stream_buffer.consume(m_event_stream_buffer.in_avail());
  while(m_event_queue->has_unread())
  {
    m_event_queue->pop_back(&event);
    delete(event);
  }
  if (m_socket)
    m_socket->close();
  m_socket= 0;
}


void Binlog_tcp_driver::shutdown(void)
{
  m_shutdown= true;
  m_io_service.stop();
}

int Binlog_tcp_driver::set_position(const std::string &str, unsigned long position)
{
  /*
    Validate the new position before we attempt to set. Once we set the
    position we won't know if it succeded because the binlog dump is
    running in another thread asynchronously.
  */
  boost::asio::io_service io_service;
  tcp::socket *socket;
  Gtid gtid = Gtid();

  if ((socket= sync_connect_and_authenticate(io_service, m_user, m_passwd, m_host, m_port)) == 0)
    return ERR_FAIL;

  std::map<std::string, unsigned long > binlog_map;
  fetch_binlogs_name_and_size(socket, binlog_map);
  socket->close();
  delete socket;

  std::map<std::string, unsigned long >::iterator binlog_itr= binlog_map.find(str);

  /*
    If the file name isn't listed on the server we will fail here.
  */
  if (binlog_itr == binlog_map.end())
    return ERR_FAIL;

  /*
    If the requested position is greater than the file size we will fail
    here.
  */
  if (position > binlog_itr->second)
    return ERR_FAIL;


  /*
    By posting to the io service we guarantee that the operations are
    executed in the same thread as the io_service is running in.
  */
  if (m_event_loop)
  {
    m_io_service.post(boost::bind(&Binlog_tcp_driver::shutdown, this));
    m_event_loop->join();
    delete(m_event_loop);
  }
  m_event_loop= 0;
  disconnect();
  /*
    Uppon return of connect we only know if we succesfully authenticated
    against the server. The binlog dump command is executed asynchronously
    in another thread.
  */
  if (connect(m_user, m_passwd, m_host, m_port, gtid, str, (size_t)position) == 0)
    return ERR_OK;
  else
    return ERR_FAIL;
}

int Binlog_tcp_driver::get_position(std::string *filename_ptr, unsigned long *position_ptr)
{
  boost::asio::io_service io_service;

  tcp::socket *socket;

  if ((socket=sync_connect_and_authenticate(io_service, m_user, m_passwd, m_host, m_port)) == 0)
    return ERR_FAIL;

  if (fetch_master_status(socket, &m_binlog_file_name, &m_binlog_offset))
    return ERR_FAIL;

  socket->close();
  delete socket;
  if (filename_ptr)
    *filename_ptr= m_binlog_file_name;
  if (position_ptr)
    *position_ptr= m_binlog_offset;
  return ERR_OK;
}

bool Binlog_tcp_driver::fetch_master_status(tcp::socket *socket, std::string *filename, unsigned long *position)
{
  boost::asio::streambuf server_messages;

  std::ostream command_request_stream(&server_messages);

  static boost::uint8_t com_query = COM_QUERY;
  Protocol_chunk<boost::uint8_t> prot_command(com_query);

  command_request_stream << prot_command
          << "SHOW MASTER STATUS";

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  try {
    // Send the request.
    boost::asio::write(*socket, boost::asio::buffer(command_packet_header, 4), boost::asio::transfer_at_least(4));
    boost::asio::write(*socket, server_messages, boost::asio::transfer_at_least(size));
  }
  catch(boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Show master status failed: ") + e.message(), __FILE__, __LINE__));
  }

  Result_set result_set(socket);

  Converter conv;
  BOOST_FOREACH(Row_of_fields row, result_set)
  {
    *filename= "";
    conv.to(*filename, row[0]);
    long pos;
    conv.to(pos, row[1]);
    *position= (unsigned long)pos;
  }
  return false;
}

bool Binlog_tcp_driver::fetch_binlogs_name_and_size(tcp::socket *socket, std::map<std::string, unsigned long> &binlog_map)
{
  boost::asio::streambuf server_messages;

  std::ostream command_request_stream(&server_messages);

  static boost::uint8_t com_query = COM_QUERY;
  Protocol_chunk<boost::uint8_t> prot_command(com_query);

  command_request_stream << prot_command
          << "SHOW BINARY LOGS";

  int size=server_messages.size();
  char command_packet_header[4];
  write_packet_header(command_packet_header, size, 0);

  try {
    // Send the request.
    boost::asio::write(*socket, boost::asio::buffer(command_packet_header, 4), boost::asio::transfer_at_least(4));
    boost::asio::write(*socket, server_messages, boost::asio::transfer_at_least(size));
  }
  catch(boost::system::error_code const& e)
  {
    throw(ListenerException(std::string("Show binary logs failed: ") + e.message(), __FILE__, __LINE__));
  }

  Result_set result_set(socket);

  Converter conv;
  BOOST_FOREACH(Row_of_fields row, result_set)
  {
    std::string filename;
    long position;
    conv.to(filename, row[0]);
    conv.to(position, row[1]);
    binlog_map.insert(std::make_pair<std::string, unsigned long>(filename, (unsigned long)position));
  }
  return false;
}


#define SCRAMBLE_BUFF_SIZE 20

int hash_sha1(boost::uint8_t *output, ...)
{
  /* size at least EVP_MAX_MD_SIZE */
  va_list ap;
  size_t result;
  EVP_MD_CTX *hash_context = EVP_MD_CTX_create();

  va_start(ap, output);
  EVP_DigestInit_ex(hash_context, EVP_sha1(), NULL);
  while ( 1 )
  {
    const boost::uint8_t *data = va_arg(ap, const boost::uint8_t *);
    int length = va_arg(ap, int);
    if ( length < 0 )
      break;
    EVP_DigestUpdate(hash_context, data, length);
  }
  EVP_DigestFinal_ex(hash_context, (unsigned char *)output, (unsigned int *)&result);
  va_end(ap);
  return result;
}


int encrypt_password(boost::uint8_t *reply,   /* buffer at least EVP_MAX_MD_SIZE */
	                   const boost::uint8_t *scramble_buff,
		                 const char *pass)
{
  boost::uint8_t hash_stage1[EVP_MAX_MD_SIZE], hash_stage2[EVP_MAX_MD_SIZE];
  //EVP_MD_CTX *hash_context = EVP_MD_CTX_create();

  /* Hash password into hash_stage1 */
  int length_stage1 = hash_sha1(hash_stage1,
                                pass, strlen(pass),
                                NULL, -1);

  /* Hash hash_stage1 into hash_stage2 */
  int length_stage2 = hash_sha1(hash_stage2,
                                hash_stage1, length_stage1,
                                NULL, -1);

  int length_reply = hash_sha1(reply,
                               scramble_buff, SCRAMBLE_BUFF_SIZE,
                               hash_stage2, length_stage2,
                               NULL, -1);

  //assert(length_reply <= EVP_MAX_MD_SIZE);
  //assert(length_reply == length_stage1);

  int i;
  for ( i=0 ; i<length_reply ; ++i )
    reply[i] = hash_stage1[i] ^ reply[i];
  return length_reply;
}

int Binlog_tcp_driver::set_position_gtid(const Gtid gtid)
{
  /*
    Validate the new position before we attempt to set. Once we set the
    position we won't know if it succeded because the binlog dump is
    running in another thread asynchronously.
  */
  boost::asio::io_service io_service;
  tcp::socket *socket;

  if ((socket= sync_connect_and_authenticate(io_service, m_user, m_passwd, m_host, m_port)) == 0)
    return ERR_FAIL;

  /*
    By posting to the io service we guarantee that the operations are
    executed in the same thread as the io_service is running in.
  */
  if (m_event_loop)
  {
    m_io_service.post(boost::bind(&Binlog_tcp_driver::shutdown, this));
    m_event_loop->join();
    delete(m_event_loop);
  }
  m_event_loop= 0;
  disconnect();
  /*
    Uppon return of connect we only know if we succesfully authenticated
    against the server. The binlog dump command is executed asynchronously
    in another thread.
  */
  if (connect(m_user, m_passwd, m_host, m_port, gtid) == 0)
    return ERR_OK;
  else
    return ERR_FAIL;
}

}} // end namespace mysql::system
