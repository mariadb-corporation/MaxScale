/*
Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights
reserved.
Copyright (c) 2013-2014, MariaDB Corporation Ab

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
- Added support for MariaDB server

Author: Jan Lindström (jan.lindstrom@mariadb.com

*/

#ifndef _TCP_DRIVER_H
#define	_TCP_DRIVER_H
#include "binlog_driver.h"
#include "protocol.h"
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "bounded_buffer.h"
#include "gtid.h"
#include <mysql.h>


#define MAX_PACKAGE_SIZE 0xffffff

#define GET_NEXT_PACKET_HEADER   \
   boost::asio::async_read(*m_socket, boost::asio::buffer(m_net_header, 4), \
     boost::bind(&Binlog_tcp_driver::handle_net_packet_header, this, \
     boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)) \

using boost::asio::ip::tcp;

namespace mysql { namespace system {

class Binlog_tcp_driver : public Binary_log_driver
{
public:

    Binlog_tcp_driver(const std::string& user, const std::string& passwd,
                      const std::string& host, unsigned long port)
      : Binary_log_driver("", 4), m_host(host), m_user(user), m_passwd(passwd),
        m_port(port), m_socket(NULL), m_waiting_event(0), m_event_loop(0),
    m_total_bytes_transferred(0), m_shutdown(false), m_packet_no(0),
        m_event_queue(new bounded_buffer<Binary_log_event*>(50))
    {
    }

    ~Binlog_tcp_driver()
    {
        delete m_event_queue;
        delete m_socket;
    }

    /**
     * Connect using previously declared connection parameters.
     */
    int connect();
    int connect(const Gtid gtid);
    int connect(const boost::uint64_t binlog_pos);

    /**
     * Blocking wait for the next binary log event to reach the client
     */
    int wait_for_next_event(mysql::Binary_log_event **event);

    /**
     * Reconnects to the master with a new binlog dump request.
     */
    int set_position(const std::string &str, unsigned long position);

    /**
     * Reconnects to the master with a new binlog dump request.
     */
    int set_position_gtid(const Gtid gtid);


    int get_position(std::string *str, unsigned long *position);

    const std::string& user() const { return m_user; }
    const std::string& password() const { return m_passwd; }
    const std::string& host() const { return m_host; }
    unsigned long port() const { return m_port; }

    int fetch_server_version(const std::string& user,
			     const std::string& passwd,
			     const std::string& host,
			     long port);

protected:
    /**
     * Connects to a mysql server, authenticates and initiates the event
     * request loop.
     *
     * @param user The user account on the server side
     * @param passwd The password used to authenticate the user
     * @param host The DNS host name or IP of the server
     * @param port The service port number to connect to
     *
     *
     * @return Success or failure code
     *   @retval 0 Successfully established a connection
     *   @retval >1 An error occurred.
     */
    int connect(const std::string& user, const std::string& passwd,
                const std::string& host, long port,
		const Gtid gtid = Gtid(),
                const std::string& binlog_filename="", size_t offset=4);

    bool send_client_capabilites(tcp::socket *socket);

    bool send_slave_connect_state(tcp::socket *socket,Gtid gtid);

    bool get_master_binlog_checksum(tcp::socket *socket);

    tcp::socket *sync_connect_and_authenticate(boost::asio::io_service &io_service, 
					      const std::string &user, 
					      const std::string &passwd, 
					      const std::string &host, 
					       long port);
    int authenticate(tcp::socket *socket, 
		     const std::string& user, 
		     const std::string& passwd,
		     const st_handshake_package &handshake_package);

    bool fetch_master_status(tcp::socket *socket, 
			     std::string *filename, 
			     unsigned long *position);

    bool fetch_binlogs_name_and_size(tcp::socket *socket, 
				     std::map<std::string, 
				     unsigned long> &binlog_map);
      
private:

    /**
     * Request a binlog dump and starts the event loop in a new thread
     * @param binlog_file_name The base name of the binlog files to query
     *
     */
    void start_binlog_dump(const std::string &binlog_file_name, size_t offset);
    void start_binlog_dump(const Gtid gtid);

    /**
     * Handles a completed mysql server package header and put a
     * request for the body in the job queue.
     */
    void handle_net_packet_header(const boost::system::error_code& err, std::size_t bytes_transferred);

    /**
     * Handles a completed network package with the assumption that it contains
     * a binlog event.
     *
     * TODO rename to handle_event_log_packet?
     */
    void handle_net_packet(const boost::system::error_code& err, std::size_t bytes_transferred);

    /**
     * Called from handle_net_packet(). The function handle a stream of bytes
     * representing event packets which may or may not be complete.
     * It uses m_waiting_event and the size of the stream as parameters
     * in a state machine. If there is no m_waiting_event then the event
     * header must be parsed for the event packet length. This can only
     * be done if the accumulated stream of bytes are more than 19.
     * Next, if there is a m_waiting_event, it can only be completed if
     * event_length bytes are waiting on the stream.
     *
     * If none of these conditions are fullfilled, the function exits without
     * any action.
     *
     * @param err Not used
     * @param bytes_transferred The number of bytes waiting in the event stream
     *
     */
    void handle_event_packet(const boost::system::error_code& err, std::size_t bytes_transferred);

    /**
     * Executes io_service in a loop.
     * TODO Checks for connection errors and reconnects to the server
     * if necessary.
     */
    void start_event_loop(void);

    /**
     * Reconnect to the server by first calling disconnect and then connect.
     */
    void reconnect(Gtid gtid = Gtid());

    /**
     * Disconnet from the server. The io service must have been stopped before
     * this function is called.
     * The event queue is emptied.
     */
    void disconnect(void);

    /**
     * Terminates the io service and sets the shudown flag.
     * this causes the event loop to terminate.
     */
    void shutdown(void);

    boost::thread *m_event_loop;
    boost::asio::io_service m_io_service;
    tcp::socket *m_socket;
    bool m_shutdown;

    /**
     * Temporary storage for a handshake package
     */
    st_handshake_package m_handshake_package;

    /**
     * Temporary storage for an OK package
     */
    st_ok_package m_ok_package;

    /**
     * Temporary storage for an error package
     */
    st_error_package m_error_package;

    /**
     * each bin log event starts with a 19 byte long header
     * We use this sturcture every time we initiate an async
     * read.
     */
    boost::uint8_t m_event_header[19];

    /**
     *
     */
    boost::uint8_t m_net_header[4];

    /**
     *
     */
    boost::uint8_t m_net_packet[MAX_PACKAGE_SIZE];
    boost::asio::streambuf m_event_stream_buffer;
    char * m_event_packet;

    /**
     * This pointer points to an object constructed from event
     * stream during async communication with
     * server. If it is 0 it means that no event has been
     * constructed yet.
     */
    Log_event_header *m_waiting_event;
    Log_event_header m_log_event_header;
    /**
     * A ring buffer used to dispatch aggregated events to the user application
     */
    bounded_buffer<Binary_log_event *> *m_event_queue;

    std::string m_user;
    std::string m_host;
    std::string m_passwd;
    long m_port;
    boost::uint32_t m_packet_no;

    boost::uint64_t m_total_bytes_transferred;


};

/**
 * Sends a SHOW MASTER STATUS command to the server and retrieve the
 * current binlog position.
 *
 * @return False if the operation succeeded, true if it failed.
 */
bool fetch_master_status(tcp::socket *socket, std::string *filename, unsigned long *position);
/**
 * Sends a SHOW BINARY LOGS command to the server and stores the file
 * names and sizes in a map.
 */
bool fetch_binlogs_name_and_size(tcp::socket *socket, std::map<std::string, unsigned long> &binlog_map);

int authenticate(tcp::socket *socket, const std::string& user,
                 const std::string& passwd,
                 const st_handshake_package &handshake_package);

tcp::socket *
sync_connect_and_authenticate(boost::asio::io_service &io_service, const std::string &user,
                              const std::string &passwd, const std::string &host, long port);


} }

#endif	/* _TCP_DRIVER_H */
