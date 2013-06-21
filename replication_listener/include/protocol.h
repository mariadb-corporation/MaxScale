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
#ifndef _PROTOCOL_H
#define	_PROTOCOL_H

#include <boost/asio.hpp>
#include <list>
#include "binlog_event.h"

using boost::asio::ip::tcp;
namespace mysql {
namespace system {

/**
  Storage structure for the handshake package sent from the server to
  the client.
*/
struct st_handshake_package
{
  boost::uint8_t     protocol_version;
  std::string server_version_str;
  boost::uint32_t    thread_id;
  boost::uint8_t     scramble_buff[8];
  boost::uint16_t    server_capabilities;
  boost::uint8_t     server_language;
  boost::uint16_t    server_status;
  boost::uint8_t     scramble_buff2[13];
};

/**
  Storage structure for the OK package sent from the server to
  the client.
*/
struct st_ok_package
{
  boost::uint8_t  result_type;
  boost::uint64_t affected_rows;
  boost::uint64_t insert_id;
  boost::uint16_t server_status;
  boost::uint16_t warning_count;
  std::string  message;
};

struct st_eof_package
{
  boost::uint16_t warning_count;
  boost::uint16_t status_flags;
};

/**
  Storage structure for the Error package sent from the server to
  the client.
*/
struct st_error_package
{
  boost::uint16_t error_code;
  boost::uint8_t  sql_state[5];
  std::string  message;
};

#define CLIENT_LONG_PASSWORD	1	/* new more secure passwords */
#define CLIENT_FOUND_ROWS	2	/* Found instead of affected rows */
#define CLIENT_LONG_FLAG	4	/* Get all column flags */
#define CLIENT_CONNECT_WITH_DB	8	/* One can specify db on connect */
#define CLIENT_NO_SCHEMA	16	/* Don't allow database.table.column */
#define CLIENT_COMPRESS		32	/* Can use compression protocol */
#define CLIENT_ODBC		64	/* Odbc client */
#define CLIENT_LOCAL_FILES	128	/* Can use LOAD DATA LOCAL */
#define CLIENT_IGNORE_SPACE	256	/* Ignore spaces before '(' */
#define CLIENT_PROTOCOL_41	512	/* New 4.1 protocol */
#define CLIENT_INTERACTIVE	1024	/* This is an interactive client */
#define CLIENT_SSL              2048	/* Switch to SSL after handshake */
#define CLIENT_IGNORE_SIGPIPE   4096    /* IGNORE sigpipes */
#define CLIENT_TRANSACTIONS	8192	/* Client knows about transactions */
#define CLIENT_RESERVED         16384   /* Old flag for 4.1 protocol  */
#define CLIENT_SECURE_CONNECTION 32768  /* New 4.1 authentication */
#define CLIENT_MULTI_STATEMENTS (1UL << 16) /* Enable/disable multi-stmt support */
#define CLIENT_MULTI_RESULTS    (1UL << 17) /* Enable/disable multi-results */

#define CLIENT_SSL_VERIFY_SERVER_CERT (1UL << 30)
#define CLIENT_REMEMBER_OPTIONS (1UL << 31)

/* Gather all possible capabilites (flags) supported by the server */
#define CLIENT_ALL_FLAGS  (CLIENT_LONG_PASSWORD | \
                           CLIENT_FOUND_ROWS | \
                           CLIENT_LONG_FLAG | \
                           CLIENT_CONNECT_WITH_DB | \
                           CLIENT_NO_SCHEMA | \
                           CLIENT_COMPRESS | \
                           CLIENT_ODBC | \
                           CLIENT_LOCAL_FILES | \
                           CLIENT_IGNORE_SPACE | \
                           CLIENT_PROTOCOL_41 | \
                           CLIENT_INTERACTIVE | \
                           CLIENT_SSL | \
                           CLIENT_IGNORE_SIGPIPE | \
                           CLIENT_TRANSACTIONS | \
                           CLIENT_RESERVED | \
                           CLIENT_SECURE_CONNECTION | \
                           CLIENT_MULTI_STATEMENTS | \
                           CLIENT_MULTI_RESULTS | \
                           CLIENT_SSL_VERIFY_SERVER_CERT | \
                           CLIENT_REMEMBER_OPTIONS)

/*
  Switch off the flags that are optional and depending on build flags
  If any of the optional flags is supported by the build it will be switched
  on before sending to the client during the connection handshake.
*/
#define CLIENT_BASIC_FLAGS (((CLIENT_ALL_FLAGS & ~CLIENT_SSL) \
                                               & ~CLIENT_COMPRESS) \
                                               & ~CLIENT_SSL_VERIFY_SERVER_CERT)
enum enum_server_command
{
  COM_SLEEP, COM_QUIT, COM_INIT_DB, COM_QUERY, COM_FIELD_LIST,
  COM_CREATE_DB, COM_DROP_DB, COM_REFRESH, COM_SHUTDOWN, COM_STATISTICS,
  COM_PROCESS_INFO, COM_CONNECT, COM_PROCESS_KILL, COM_DEBUG, COM_PING,
  COM_TIME, COM_DELAYED_INSERT, COM_CHANGE_USER, COM_BINLOG_DUMP,
  COM_TABLE_DUMP, COM_CONNECT_OUT, COM_REGISTER_SLAVE,
  COM_STMT_PREPARE, COM_STMT_EXECUTE, COM_STMT_SEND_LONG_DATA, COM_STMT_CLOSE,
  COM_STMT_RESET, COM_SET_OPTION, COM_STMT_FETCH, COM_DAEMON,
  COM_BINLOG_DUMP_GTID,
  /* don't forget to update const char *command_name[] in sql_parse.cc */

  /* Must be last */
  COM_END
};

enum enum_field_types { MYSQL_TYPE_DECIMAL, MYSQL_TYPE_TINY,
			MYSQL_TYPE_SHORT,  MYSQL_TYPE_LONG,
			MYSQL_TYPE_FLOAT,  MYSQL_TYPE_DOUBLE,
			MYSQL_TYPE_NULL,   MYSQL_TYPE_TIMESTAMP,
			MYSQL_TYPE_LONGLONG,MYSQL_TYPE_INT24,
			MYSQL_TYPE_DATE,   MYSQL_TYPE_TIME,
			MYSQL_TYPE_DATETIME, MYSQL_TYPE_YEAR,
			MYSQL_TYPE_NEWDATE, MYSQL_TYPE_VARCHAR,
			MYSQL_TYPE_BIT,
                        MYSQL_TYPE_NEWDECIMAL=246,
			MYSQL_TYPE_ENUM=247,
			MYSQL_TYPE_SET=248,
			MYSQL_TYPE_TINY_BLOB=249,
			MYSQL_TYPE_MEDIUM_BLOB=250,
			MYSQL_TYPE_LONG_BLOB=251,
			MYSQL_TYPE_BLOB=252,
			MYSQL_TYPE_VAR_STRING=253,
			MYSQL_TYPE_STRING=254,
			MYSQL_TYPE_GEOMETRY=255

};


#define int3store(T,A)  do { *(T)=  (unsigned char) ((A));\
                            *(T+1)=(unsigned char) (((unsigned int) (A) >> 8));\
                            *(T+2)=(unsigned char) (((A) >> 16)); } while (0)

/*
 * Helper functions
 *
static void proto_append_int_len(boost::asio::streambuf &buf, unsigned long long num, int len)
{
    std::ostream os(&buf);
    for (int i= 0; i< len; i++)
    {
      os << (char) (num & 0xFF);
      num= num >> 8;
    }
}

static void proto_append_int_len(std::ostream &os, unsigned long long num, int len)
{
    for (int i= 0; i< len; i++)
    {
      unsigned char shift= (num >> i) & 0xFF;
      os << shift;
    }
}

static void proto_append_int_len(char *buff, unsigned long long num, int len)
{
    for (int i= 0; i< len; i++)
    {
      buff[i]= (unsigned char) (num & 0xFF);
      num= num >> 8;
    }
}
*/

void write_packet_header(char *buff, boost::uint16_t size, boost::uint8_t packet_no);

class Protocol_validator;
class buffer_source;

/**
 * The Protocol interface is used to describe a grammar consisting of
 * chunks of bytes that are read or written in consequtive order.
 * Example:
 * class Protocol_impl : public Protocol;
 * Protocol_impl chunk1(chunk_datastore1);
 * Protocol_impl chunk2(chunk_datastore2);
 * input_stream >> chunk1
 *              >> chunk2;
 */
class Protocol
{
public:
  Protocol() { m_length_encoded_binary= false; }
  /**
    Return the number of bytes which is read or written by this protocol chunk.
    The default size is equal to the underlying storage data type.
  */
  virtual unsigned int size()=0;

  /** Return a pointer to the first byte in a linear storage buffer */
  virtual const char *data()=0;

  /**
    Change the number of bytes which will be read or written to the storage.
    The default size is euqal to the storage type size. This can change if the
    protocol is reading a length encoded binary.
    The new size must always be less than the size of the underlying storage
    type.
  */
  virtual void collapse_size(unsigned int new_size)=0;

  /**
    The first byte will have a special interpretation which will indicate
    how many bytes should be read next.
  */
  void set_length_encoded_binary(bool bswitch) { m_length_encoded_binary= bswitch; }
  bool is_length_encoded_binary(void) { return m_length_encoded_binary; }

private:
    bool m_length_encoded_binary;

    friend std::istream &operator<<(std::istream &is, Protocol &chunk);
    friend std::istream &operator>>(std::istream &is, Protocol &chunk);
    friend buffer_source &operator>>(buffer_source &src, Protocol &chunk);
    friend std::istream &operator>>(std::istream &is, std::string &str);
};

template<typename T>
class Protocol_chunk : public Protocol
{
public:

  Protocol_chunk() : Protocol()
  {
    m_size= 0;
    m_data= 0;
  }

  Protocol_chunk(T &chunk) : Protocol()
  {
    m_data= (const char *)&chunk;
    m_size= sizeof(T);
  }

  Protocol_chunk(const T &chunk) : Protocol ()
  {
     m_data= (const char *) &chunk;
     m_size= sizeof(T);
  }

  /**
   * @param buffer A pointer to the storage
   * @param size The size of the storage
   *
   * @note If size == 0 then the chunk is a
   * length coded binary.
   */
  Protocol_chunk(T *buffer, unsigned long size) : Protocol ()
  {
      m_data= (const char *)buffer;
      m_size= size;
  }

  virtual unsigned int size() { return m_size; }
  virtual const char *data() { return m_data; }
  virtual void collapse_size(unsigned int new_size)
  {
    //assert(new_size <= m_size);
    memset((char *)m_data+new_size,'\0', m_size-new_size);
    m_size= new_size;
  }
private:
  const char *m_data;
  unsigned long m_size;
};

std::ostream &operator<<(std::ostream &os, Protocol &chunk);

typedef Protocol_chunk<boost::uint8_t> Protocol_chunk_uint8;

class Protocol_chunk_string //: public Protocol_chunk_uint8
{
public:
    Protocol_chunk_string(std::string &chunk, unsigned long size) //: Protocol_chunk_uint8()
    {
        m_str= &chunk;
        m_str->assign(size,'*');
    }

    virtual unsigned int size() const { return m_str->size(); }
    virtual const char *data() const { return m_str->data(); }
    virtual void collapse_size(unsigned int new_size)
    {
        m_str->resize(new_size);
    }
private:
    friend std::istream &operator>>(std::istream &is, Protocol_chunk_string &str);
    std::string *m_str;
};

class Protocol_chunk_vector : public Protocol_chunk_uint8
{
public:
    Protocol_chunk_vector(std::vector<boost::uint8_t> &chunk, unsigned long size)
      : Protocol_chunk_uint8()
    {
        m_vec= &chunk;
        m_vec->reserve(size);
        m_size= size;
    }

    virtual unsigned int size() { return m_vec->size(); }
    virtual const char *data() { return reinterpret_cast<const char *>(&*m_vec->begin()); }
    virtual void collapse_size(unsigned int new_size)
    {
        m_vec->resize(new_size);
    }
private:
    friend std::istream &operator>>(std::istream &is, Protocol_chunk_vector &chunk);
    std::vector<boost::uint8_t> *m_vec;
    unsigned long m_size;
};


std::istream &operator>>(std::istream &is, Protocol_chunk_vector &chunk);

class buffer_source
{
public:

    buffer_source(const char *src, int sz)
    {
        m_src= src;
        m_size= sz;
        m_ptr= 0;
    }

    friend buffer_source &operator>>(buffer_source &src, Protocol &chunk);
private:
    const char *m_src;
    int m_size;
    int m_ptr;

};

class Protocol_chunk_string_len
{
public:
    Protocol_chunk_string_len(std::string &str)
    {
        m_storage= &str;
    }

private:
    friend std::istream &operator>>(std::istream &is, Protocol_chunk_string_len &lenstr);
    std::string *m_storage;
};

buffer_source &operator>>(buffer_source &src, Protocol &chunk);
/** TODO assert that the correct endianess is used */
std::istream &operator>>(std::istream &is, Protocol &chunk);
std::istream &operator>>(std::istream &is, std::string &str);
std::istream &operator>>(std::istream &is, Protocol_chunk_string_len &lenstr);
std::istream &operator>>(std::istream &is, Protocol_chunk_string &str);

int proto_read_package_header(tcp::socket *socket, unsigned long *packet_length, unsigned char *packet_no);

/**
 * Read a server package header from a stream buffer
 *
 * @retval 0 Success
 * @retval >0 An error occurred
 */
int proto_read_package_header(tcp::socket *socket, boost::asio::streambuf &buff, unsigned long *packet_length, unsigned char *packet_no);

/**
 * Get one complete packet from the server
 *
 * @param socket Pointer to the active tcp-socket
 * @param buff A reference to a stream buffer
 * @param packet_no [out] The number of the packet as given by the server
 *
 * @return the size of the packet or 0 to indicate an error
 */
int proto_get_one_package(tcp::socket *socket, boost::asio::streambuf &buff, boost::uint8_t *packet_no);
void prot_parse_error_message(std::istream &is, struct st_error_package &err, int packet_length);
void prot_parse_ok_message(std::istream &is, struct st_ok_package &ok, int packet_length);
void prot_parse_eof_message(std::istream &is, struct st_eof_package &eof);
void proto_get_handshake_package(std::istream &is, struct st_handshake_package &p, int packet_length);

/**
  Allocates a new event and copy the header. The caller must be responsible for
  releasing the allocated memory.
*/
Query_event *proto_query_event(std::istream &is, Log_event_header *header);
Rotate_event *proto_rotate_event(std::istream &is, Log_event_header *header);
Incident_event *proto_incident_event(std::istream &is, Log_event_header *header);
Row_event *proto_rows_event(std::istream &is, Log_event_header *header);
Table_map_event *proto_table_map_event(std::istream &is, Log_event_header *header);
Int_var_event *proto_intvar_event(std::istream &is, Log_event_header *header);
User_var_event *proto_uservar_event(std::istream &is, Log_event_header *header);
Gtid_event *proto_gtid_event(std::istream &is, Log_event_header *header);

} // end namespace system
} // end namespace mysql

#endif	/* _PROTOCOL_H */
