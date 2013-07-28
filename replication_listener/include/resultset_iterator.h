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

#ifndef _RESULTSET_ITERATOR_H
#define	_RESULTSET_ITERATOR_H

#include <iostream>

// if error; try #include <boost/iterator.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include <boost/asio.hpp>
#include "value.h"
#include "rowset.h"
#include "row_of_fields.h"

using namespace mysql;

namespace mysql
{

struct Field_packet
{
    std::string catalog;  // Length Coded String
    std::string db;       // Length Coded String
    std::string table;    // Length Coded String
    std::string org_table;// Length Coded String
    std::string name;     // Length Coded String
    std::string org_name; // Length Coded String
    boost::uint8_t marker;       // filler
    boost::uint16_t charsetnr;   // charsetnr
    boost::uint32_t length;      // length
    boost::uint8_t type;         // field type
    boost::uint16_t flags;
    boost::uint8_t decimals;
    boost::uint16_t filler;      // filler, always 0x00
    //boost::uint64_t default_value;  // Length coded binary; only in table descr.
};

typedef std::list<std::string > String_storage;

namespace system {
    void digest_result_header(std::istream &is, boost::uint64_t &field_count, boost::uint64_t extra);
    void digest_field_packet(std::istream &is, Field_packet &field_packet);
    void digest_marker(std::istream &is);
    void digest_row_content(std::istream &is, int field_count, Row_of_fields &row, String_storage &storage, bool &is_eof);
}

template <class T>
class Result_set_iterator;

class Result_set
{
public:
    typedef Result_set_iterator<Row_of_fields > iterator;
    typedef Result_set_iterator<Row_of_fields const > const_iterator;

    Result_set(tcp::socket *socket) { source(socket); }
    void source(tcp::socket *socket) { m_socket= socket; digest_row_set(); }
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;

private:
    void digest_row_set();
    friend class Result_set_iterator<Row_of_fields >;
    friend class Result_set_iterator<Row_of_fields const>;

    std::vector<Field_packet > m_field_types;
    int m_row_count;
    std::vector<Row_of_fields > m_rows;
    String_storage m_storage;
    tcp::socket *m_socket;
    typedef enum { RESULT_HEADER,
                   FIELD_PACKETS,
                   MARKER,
                   ROW_CONTENTS,
                   EOF_PACKET
                 } state_t;
    state_t m_current_state;

    /**
     * The number of fields in the field packets block
     */
    boost::uint64_t m_field_count;
    /**
     * Used for SHOW COLUMNS to return the number of rows in the table
     */
    boost::uint64_t m_extra;
};

template <class Iterator_value_type >
class Result_set_iterator :
  public boost::iterator_facade<Result_set_iterator<Iterator_value_type >,
                                Iterator_value_type,
                                boost::forward_traversal_tag >
{
public:
    Result_set_iterator() : m_feeder(0), m_current_row(-1)
    {}

    explicit Result_set_iterator(Result_set *feeder) : m_feeder(feeder),
      m_current_row(-1)
    {
      increment();
    }

 private:
    friend class boost::iterator_core_access;

    void increment()
    {
      if (++m_current_row >= m_feeder->m_row_count)
        m_current_row= -1;
    }

    bool equal(const Result_set_iterator& other) const
    {
        if (other.m_feeder == 0 && m_feeder == 0)
            return true;
        if (other.m_feeder == 0)
        {
            if (m_current_row == -1)
                return true;
            else
                return false;
        }
        if (m_feeder == 0)
        {
            if (other.m_current_row == -1)
                return true;
            else
                return false;
        }

        if( other.m_feeder->m_field_count != m_feeder->m_field_count)
            return false;

        Iterator_value_type *row1= &m_feeder->m_rows[m_current_row];
        Iterator_value_type *row2= &other.m_feeder->m_rows[m_current_row];
        for (unsigned i=0; i< m_feeder->m_field_count; ++i)
        {
            Value val1= row1->at(i);
            Value val2= row2->at(i);
            if (val1 != val2)
                return false;
        }
        return true;
    }

    Iterator_value_type &dereference() const
    {
        return m_feeder->m_rows[m_current_row];
    }

private:
    Result_set *m_feeder;
    int m_current_row;

};


} // end namespace mysql



#endif	/* _RESULTSET_ITERATOR_H */
