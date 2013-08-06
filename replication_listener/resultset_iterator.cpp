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

#include "resultset_iterator.h"
#include "protocol.h"
#include "row_of_fields.h"

using namespace mysql;

namespace mysql {

Result_set::iterator Result_set::begin() { return iterator(this); }
Result_set::iterator Result_set::end() { return iterator(); }
Result_set::const_iterator Result_set::begin() const { return const_iterator(const_cast<Result_set *>(this)); }
Result_set::const_iterator Result_set::end() const { return const_iterator(); }

void Result_set::digest_row_set()
{
  unsigned long packet_length;
  unsigned char packet_no= 1;
  m_current_state= RESULT_HEADER;
  boost::asio::streambuf resultbuff;
  std::istream response_stream(&resultbuff);
  unsigned field_count= 0;
  try {
  do
  {
    /*
     * Get server response
     */
    packet_length= system::proto_get_one_package(m_socket, resultbuff, &packet_no);

    switch(m_current_state)
    {
      case RESULT_HEADER:
        system::digest_result_header(response_stream, m_field_count, m_extra);
        m_row_count= 0;
        m_current_state= FIELD_PACKETS;
        break;
      case FIELD_PACKETS:
      {
        Field_packet field;
        system::digest_field_packet(response_stream, field);
        m_field_types.assign(field_count,field);

        if (++field_count == m_field_count)
          m_current_state= MARKER;
      }
      break;
      case MARKER:
      {
         char marker;
         response_stream >> marker;
         //assert(marker == 0xfe);
         system::digest_marker(response_stream);
         m_current_state= ROW_CONTENTS;
       }
       break;
       case ROW_CONTENTS:
       {
         bool is_eof= false;
         Row_of_fields row(0);
         system::digest_row_content(response_stream, m_field_count, row, m_storage, is_eof);
         if (is_eof)
           m_current_state= EOF_PACKET;
         else
         {
           m_rows.push_back(row);
           ++m_row_count;
         }
       }
       break;
       default:
         continue;
    }
  } while (m_current_state != EOF_PACKET);
  } catch(boost::system::system_error e)
  {
    // TODO log error
    m_field_count= 0;
    m_row_count= 0;
  }
}

namespace system {

void digest_result_header(std::istream &is, boost::uint64_t &field_count, boost::uint64_t extra)
{
  Protocol_chunk<boost::uint64_t> proto_field_count(field_count);
  //Protocol_chunk<boost::uint64_t> proto_extra(extra);

  proto_field_count.set_length_encoded_binary(true);
  //proto_extra.set_length_encoded_binary(true);

  is >> proto_field_count;
     //>> proto_extra;
}

void digest_field_packet(std::istream &is, Field_packet &field_packet)
{
  Protocol_chunk_string_len proto_catalog(field_packet.catalog);
  Protocol_chunk_string_len proto_db(field_packet.db);
  Protocol_chunk_string_len proto_table(field_packet.table);
  Protocol_chunk_string_len proto_org_table(field_packet.org_table);
  Protocol_chunk_string_len proto_name(field_packet.name);
  Protocol_chunk_string_len proto_org_name(field_packet.org_name);
  Protocol_chunk<boost::uint8_t>   proto_marker(field_packet.marker);
  Protocol_chunk<boost::uint16_t>  proto_charsetnr(field_packet.charsetnr);
  Protocol_chunk<boost::uint32_t>  proto_length(field_packet.length);
  Protocol_chunk<boost::uint8_t>   proto_type(field_packet.type);
  Protocol_chunk<boost::uint16_t>  proto_flags(field_packet.flags);
  Protocol_chunk<boost::uint8_t>   proto_decimals(field_packet.decimals);
  Protocol_chunk<boost::uint16_t>  proto_filler(field_packet.filler);
  //Protocol_chunk<boost::uint64_t>  proto_default_value(field_packet.default_value);

  is >> proto_catalog
     >> proto_db
     >> proto_table
     >> proto_org_table
     >> proto_name
     >> proto_org_name
     >> proto_marker
     >> proto_charsetnr
     >> proto_length
     >> proto_type
     >> proto_flags
     >> proto_decimals
     >> proto_filler;
}

void digest_marker(std::istream &is)
{
  struct st_eof_package eof;
  prot_parse_eof_message(is,eof);
}

void digest_row_content(std::istream &is, int field_count, Row_of_fields &row, String_storage &storage, bool &is_eof)
{
  boost::uint8_t size;
  Protocol_chunk<boost::uint8_t> proto_size(size);
  is >> proto_size;
  if (size == 0xfe)
  {
    /* EOF packet is detected and there are no more rows to be expeced. */
    is_eof= true;
    struct st_eof_package eof;
    prot_parse_eof_message(is, eof);
    return;
  }
  is.putback((char)size);
  for(int field_no=0; field_no < field_count; ++field_no)
  {
    std::string *storage= new std::string;

    Protocol_chunk_string_len proto_value(*storage);
    is >> proto_value;

    Value value(MYSQL_TYPE_VAR_STRING, storage->length(), storage->c_str());
    row.push_back(value);
  }
}

}} // end namespace system, mysql
