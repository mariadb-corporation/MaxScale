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
#include "value.h"
#include "binlog_event.h"
#include <boost/lexical_cast.hpp>
#include <iomanip>
#include <boost/format.hpp>
#include <mysql.h>

#define DIG_PER_DEC1 9

using namespace mysql;
using namespace mysql::system;
namespace mysql {

static const int dig2bytes[DIG_PER_DEC1 + 1] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 4};

int decimal_bin_size(int precision, int scale)
{
  int intg   = precision - scale;
  int intg0  = intg / DIG_PER_DEC1;
  int frac0  = scale / DIG_PER_DEC1;
  int intg0x = intg - intg0 * DIG_PER_DEC1;
  int frac0x = scale - frac0 * DIG_PER_DEC1;

  return(
    intg0 * sizeof(boost::int32_t) + dig2bytes[intg0x] +
    frac0 * sizeof(boost::int32_t) + dig2bytes[frac0x]
    );
}

int calc_field_size(unsigned char column_type, const unsigned char *field_ptr, boost::uint32_t metadata)
{
  boost::uint32_t length;

  switch (column_type) {
  case MYSQL_TYPE_VAR_STRING:
    /* This type is hijacked for result set types. */
    length= metadata;
    break;
  case MYSQL_TYPE_NEWDECIMAL:
  {
    int precision = (metadata & 0xff);
    int scale = metadata >> 8;
    length = decimal_bin_size(precision, scale);
    break;
  }
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_DOUBLE:
    length= metadata;
    break;
  /*
    The cases for SET and ENUM are include for completeness, however
    both are mapped to type MYSQL_TYPE_STRING and their real types
    are encoded in the field metadata.
  */
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_STRING:
  {
    //unsigned char type= metadata >> 8U;
    unsigned char type = metadata & 0xff;
    if ((type == MYSQL_TYPE_SET) || (type == MYSQL_TYPE_ENUM))
    {
      //length= metadata & 0x00ff;
      length = (metadata & 0xff00) >> 8;
    }
    else
    {
      /*
        We are reading the actual size from the master_data record
        because this field has the actual lengh stored in the first
        byte.
      */
      length= (unsigned int) *field_ptr+1;
      //DBUG_ASSERT(length != 0);
    }
    break;
  }
  case MYSQL_TYPE_YEAR:
  case MYSQL_TYPE_TINY:
    length= 1;
    break;
  case MYSQL_TYPE_SHORT:
    length= 2;
    break;
  case MYSQL_TYPE_INT24:
    length= 3;
    break;
  case MYSQL_TYPE_LONG:
    length= 4;
    break;
  case MYSQL_TYPE_LONGLONG:
    length= 8;
    break;
  case MYSQL_TYPE_NULL:
    length= 0;
    break;
  case MYSQL_TYPE_NEWDATE:
    length= 3;
    break;
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_TIME:
    length= 3;
    break;
  case MYSQL_TYPE_TIMESTAMP:
    length= 4;
    break;
  case MYSQL_TYPE_DATETIME:
    length= 8;
    break;
  case MYSQL_TYPE_BIT:
  {
    /*
      Decode the size of the bit field from the master.
        from_len is the length in bytes from the master
        from_bit_len is the number of extra bits stored in the master record
      If from_bit_len is not 0, add 1 to the length to account for accurate
      number of bytes needed.
    */
	boost::uint32_t from_len= (metadata >> 8U) & 0x00ff;
	boost::uint32_t from_bit_len= metadata & 0x00ff;
    //DBUG_ASSERT(from_bit_len <= 7);
    length= from_len + ((from_bit_len > 0) ? 1 : 0);
    break;
  }
  case MYSQL_TYPE_VARCHAR:
  {
    length= metadata > 255 ? 2 : 1;
    length+= length == 1 ? (boost::uint32_t) *field_ptr : *((boost::uint16_t *)field_ptr);
    break;
  }
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_GEOMETRY:
  {
     switch (metadata)
    {
      case 1:
        length= 1+ (boost::uint32_t) field_ptr[0];
        break;
      case 2:
        length= 2+ (boost::uint32_t) (*(boost::uint16_t *)(field_ptr) & 0xFFFF);
        break;
      case 3:
        // TODO make platform indep.
        length= 3+ (boost::uint32_t) (long) (*((boost::uint32_t *) (field_ptr)) & 0xFFFFFF);
        break;
      case 4:
        // TODO make platform indep.
        length= 4+ (boost::uint32_t) (long) *((boost::uint32_t *) (field_ptr));
        break;
      default:
        length= 0;
        break;
    }
    break;
  }
  default:
    length= ~(boost::uint32_t) 0;
  }
  return length;
}

/*
Value::Value(Value &val)
{
  m_size= val.length();
  m_storage= val.storage();
  m_type= val.type();
  m_metadata= val.metadata();
  m_is_null= val.is_null();
}
*/

Value::Value(const Value& val)
{
  m_size= val.m_size;
  m_storage= val.m_storage;
  m_type= val.m_type;
  m_metadata= val.m_metadata;
  m_is_null= val.m_is_null;
}

Value &Value::operator=(const Value &val)
{
  m_size= val.m_size;
  m_storage= val.m_storage;
  m_type= val.m_type;
  m_metadata= val.m_metadata;
  m_is_null= val.m_is_null;
  return *this;
}

bool Value::operator==(const Value &val) const
{
  return (m_size == val.m_size) &&
         (m_storage == val.m_storage) &&
         (m_type == val.m_type) &&
         (m_metadata == val.m_metadata);
}

bool Value::operator!=(const Value &val) const
{
  return !operator==(val);
}

char *Value::as_c_str(unsigned long &size) const
{
  if (m_is_null || m_size == 0)
  {
    size= 0;
    return 0;
  }
  /*
   Length encoded; First byte is length of string.
  */
  int metadata_length= m_size > 251 ? 2: 1;
  /*
   Size is length of the character string; not of the entire storage
  */
  size= m_size - metadata_length;

  char *str = const_cast<char *>(m_storage + metadata_length);

  if (m_type == MYSQL_TYPE_VARCHAR && m_metadata > 255) {
    str++;
    size--;
  }

  return str;
}

unsigned char *Value::as_blob(unsigned long &size) const
{
  if (m_is_null || m_size == 0)
  {
    size= 0;
    return 0;
  }

  /*
   Size was calculated during construction of the object and only inludes the
   size of the blob data, not the metadata part which also is stored in the
   storage. For blobs this part can be between 1-4 bytes long.
  */
  size= m_size - m_metadata;

  /*
   Adjust the storage pointer with the size of the metadata.
  */
  return (unsigned char *)(m_storage + m_metadata);
}

boost::int32_t Value::as_int32() const
{
  if (m_is_null)
  {
    return 0;
  }
  boost::uint32_t to_int;
  Protocol_chunk<boost::uint32_t> prot_integer(to_int);

  buffer_source buff(m_storage, m_size);
  buff >> prot_integer;
  return to_int;
}

boost::int8_t Value::as_int8() const
{
  if (m_is_null)
  {
    return 0;
  }
  boost::int8_t to_int;
  Protocol_chunk<boost::int8_t> prot_integer(to_int);

  buffer_source buff(m_storage, m_size);
  buff >> prot_integer;
  return to_int;
}

boost::int16_t Value::as_int16() const
{
  if (m_is_null)
  {
    return 0;
  }
  boost::int16_t to_int;
  Protocol_chunk<boost::int16_t> prot_integer(to_int);

  buffer_source buff(m_storage, m_size);
  buff >> prot_integer;
  return to_int;
}

boost::int64_t Value::as_int64() const
{
  if (m_is_null)
  {
    return 0;
  }
  boost::int64_t to_int;
  Protocol_chunk<boost::int64_t> prot_integer(to_int);

  buffer_source buff(m_storage, m_size);
  buff >> prot_integer;
  return to_int;
}

float Value::as_float() const
{
  // TODO
  return *((const float *)storage());
}

double Value::as_double() const
{
  // TODO
  return *((const double *)storage());
}

void Converter::to(std::string &str, const Value &val) const
{
  if (val.is_null())
  {
    str= "(NULL)";
    return;
  }

  switch(val.type())
  {
    case MYSQL_TYPE_DECIMAL:
      str= "not implemented";
      break;
    case MYSQL_TYPE_TINY:
      str= boost::lexical_cast<std::string>(static_cast<int>(val.as_int8()));
      break;
    case MYSQL_TYPE_SHORT:
      str= boost::lexical_cast<std::string>(val.as_int16());
      break;
    case MYSQL_TYPE_LONG:
      str= boost::lexical_cast<std::string>(val.as_int32());
      break;
    case MYSQL_TYPE_FLOAT:
    {
      str= boost::str(boost::format("%d") % val.as_float());
    }
      break;
    case MYSQL_TYPE_DOUBLE:
      str= boost::str(boost::format("%d") % val.as_double());
      break;
    case MYSQL_TYPE_NULL:
      str= "not implemented";
      break;
    case MYSQL_TYPE_TIMESTAMP:
      str= boost::lexical_cast<std::string>((boost::uint32_t)val.as_int32());
      break;

    case MYSQL_TYPE_LONGLONG:
      str= boost::lexical_cast<std::string>(val.as_int64());
      break;
    case MYSQL_TYPE_INT24:
      str= "not implemented";
      break;
    case MYSQL_TYPE_DATE:
    {
      const char* val_storage = val.storage();
      unsigned int date_val = (val_storage[0] & 0xff) + ((val_storage[1] & 0xff) << 8) + ((val_storage[2] & 0xff) << 16);
      unsigned int date_year = date_val >> 9;
      date_val -= (date_year << 9);
      unsigned int date_month = date_val >> 5;
      unsigned int date_day = date_val - (date_month << 5);
      str = boost::str(boost::format("%04d-%02d-%02d") % date_year % date_month % date_day);
      break;
    }
    case MYSQL_TYPE_DATETIME:
    {
      boost::uint64_t timestamp= val.as_int64();
      unsigned long d= timestamp / 1000000;
      unsigned long t= timestamp % 1000000;
      std::ostringstream os;

      os << std::setfill('0') << std::setw(4) << d / 10000
         << std::setw(1) << '-'
         << std::setw(2) << (d % 10000) / 100
         << std::setw(1) << '-'
         << std::setw(2) << d % 100
         << std::setw(1) << ' '
         << std::setw(2) << t / 10000
         << std::setw(1) << ':'
         << std::setw(2) << (t % 10000) / 100
         << std::setw(1) << ':'
         << std::setw(2) << t % 100;

      str= os.str();
    }
      break;
    case MYSQL_TYPE_TIME:
    {
      const char* val_storage = val.storage();
      unsigned int time_val = (val_storage[0] & 0xff) + ((val_storage[1] & 0xff) << 8) + ((val_storage[2] & 0xff) << 16);
      unsigned int time_sec = time_val % 100;
      time_val -= time_sec;
      unsigned int time_min = (time_val % 10000) / 100;
      unsigned int time_hour = (time_val - time_min) / 10000;
      str = boost::str(boost::format("%02d:%02d:%02d") % time_hour % time_min % time_sec);
      break;
    }
    case MYSQL_TYPE_YEAR:
    {
      const char* val_storage = val.storage();
      unsigned int year_val = (val_storage[0] & 0xff);
      year_val = year_val > 0 ? (year_val + 1900) : 0;
      str = boost::str(boost::format("%04d") % year_val);
      break;
    }
    case MYSQL_TYPE_NEWDATE:
      str= "not implemented";
      break;
    case MYSQL_TYPE_VARCHAR:
    {
      unsigned long size;
      char *ptr= val.as_c_str(size);
      str.append(ptr, size);
    }
      break;
    case MYSQL_TYPE_VAR_STRING:
    {
      str.append(val.storage(), val.length());
    }
    break;
    case MYSQL_TYPE_STRING:
    {
      unsigned char str_type = 0;

      if (val.metadata()) {
        str_type = val.metadata() & 0xff;
      }

      if (str_type == MYSQL_TYPE_SET) {
        str = "not implemented";
        break;
      } else if (str_type == MYSQL_TYPE_ENUM) {
        unsigned int val_storage = static_cast<unsigned int>(*val.storage());
        str = boost::str(boost::format("%u") % val_storage);
        break;
      }

      unsigned long size;
      char *ptr= val.as_c_str(size);
      str.append(ptr, size);
    }
      break;
    case MYSQL_TYPE_BIT:
      str= "not implemented";
      break;
    case MYSQL_TYPE_NEWDECIMAL:
      str= "not implemented";
      break;
    case MYSQL_TYPE_ENUM:
      str= "not implemented";
      break;
    case MYSQL_TYPE_SET:
      str= "not implemented";
      break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    {
      unsigned long size;
      unsigned char *ptr= val.as_blob(size);
      str.append((const char *)ptr, size);
    }
      break;
    case MYSQL_TYPE_GEOMETRY:
      str= "not implemented";
      break;
    default:
      str= "not implemented";
      break;
  }
}

void Converter::to(float &out, const Value &val) const
{
  switch(val.type())
  {
  case MYSQL_TYPE_FLOAT:
    out= val.as_float();
    break;
  default:
    out= 0;
  }
}

void Converter::to(long &out, const Value &val) const
{
  switch(val.type())
  {
    case MYSQL_TYPE_DECIMAL:
      // TODO
      out= 0;
      break;
    case MYSQL_TYPE_TINY:
      out= val.as_int8();
      break;
    case MYSQL_TYPE_SHORT:
      out= val.as_int16();
      break;;
    case MYSQL_TYPE_LONG:
      out= (long)val.as_int32();
      break;
    case MYSQL_TYPE_FLOAT:
      out= 0;
      break;
    case MYSQL_TYPE_DOUBLE:
      out= (long)val.as_double();
    case MYSQL_TYPE_NULL:
      out= 0;
      break;
    case MYSQL_TYPE_TIMESTAMP:
      out=(boost::uint32_t)val.as_int32();
      break;

    case MYSQL_TYPE_LONGLONG:
      out= (long)val.as_int64();
      break;
    case MYSQL_TYPE_INT24:
      out= 0;
      break;
    case MYSQL_TYPE_DATE:
      out= 0;
      break;
    case MYSQL_TYPE_TIME:
      out= 0;
      break;
    case MYSQL_TYPE_DATETIME:
      out= (long)val.as_int64();
      break;
    case MYSQL_TYPE_YEAR:
      out= 0;
      break;
    case MYSQL_TYPE_NEWDATE:
      out= 0;
      break;
    case MYSQL_TYPE_VARCHAR:
      out= 0;
      break;
    case MYSQL_TYPE_BIT:
      out= 0;
      break;
    case MYSQL_TYPE_NEWDECIMAL:
      out= 0;
      break;
    case MYSQL_TYPE_ENUM:
      out= 0;
      break;
    case MYSQL_TYPE_SET:
      out= 0;
      break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
      out= 0;
      break;
    case MYSQL_TYPE_VAR_STRING:
    {
      std::string str;
      str.append(val.storage(), val.length());
      out= boost::lexical_cast<long>(str.c_str());
    }
      break;
    case MYSQL_TYPE_STRING:
      out= 0;
      break;
    case MYSQL_TYPE_GEOMETRY:
      out= 0;
      break;
    default:
      out= 0;
      break;
  }
}


} // end namespace mysql
