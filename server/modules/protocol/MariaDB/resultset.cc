/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <string.h>
#include <ctype.h>

#include <numeric>

#include <maxbase/alloc.h>
#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/mysql_binlog.h>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/modutil.hh>

namespace
{
using Data = std::vector<uint8_t>;

Data create_leint(size_t value)
{
    if (value < 251)
    {
        return {(uint8_t)value};
    }
    else if (value <= 0xffff)
    {
        return {0xfc, (uint8_t)value, (uint8_t)(value >> 8)};
    }
    else if (value <= 0xffffff)
    {
        return {0xfd, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16)};
    }
    else
    {
        Data data(9);
        data[0] = 0xfe;
        mariadb::set_byte8(&data[1], value);
        return data;
    }
}

Data create_lestr(const std::string& str)
{
    Data data = create_leint(str.size());
    data.insert(data.end(), str.begin(), str.end());
    return data;
}

Data create_header(size_t size, uint8_t seqno)
{
    Data data(4);
    mariadb::set_byte3(&data[0], size);
    data[3] = seqno;
    return data;
}

Data create_fieldcount(size_t count)
{
    auto i = create_leint(count);
    auto data = create_header(i.size(), 1);
    data.insert(data.end(), i.begin(), i.end());
    return data;
}

Data create_columndef(const std::string& name, uint8_t seqno)
{
    size_t len = 22 + name.length();
    auto data = create_header(len, seqno);
    data.resize(len + data.size());

    uint8_t* ptr = &data[4];
    *ptr++ = 3;     // Catalog is always def
    *ptr++ = 'd';
    *ptr++ = 'e';
    *ptr++ = 'f';
    *ptr++ = 0;             // Schema name length
    *ptr++ = 0;             // virtual table name length
    *ptr++ = 0;             // Table name length
    *ptr++ = name.length(); // Column name length;
    memcpy(ptr, name.c_str(), name.length());
    ptr += name.length();
    *ptr++ = 0;     // Original column name
    *ptr++ = 0x0c;  // Length of next fields always 12
    *ptr++ = 0x3f;  // Character set
    *ptr++ = 0;
    mariadb::set_byte4(ptr, 255);   // Length of column
    ptr += 4;
    *ptr++ = TABLE_COL_TYPE_VARCHAR;
    *ptr++ = 0x81;      // Two bytes of flags
    *ptr++ = 0x00;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;

    return data;
}

Data create_eof(uint8_t seqno)
{
    uint8_t eof[] = {0x5, 0x0, 0x0, seqno, 0xfe, 0x0, 0x0, 0x0, 0x0};
    return {std::begin(eof), std::end(eof)};
}

Data create_row(const std::vector<std::string>& row, uint8_t seqno)
{
    int len = std::accumulate(row.begin(), row.end(), 0, [](auto l, const auto& s) {
                                  return l + s.length() + 1;
                              });

    auto data = create_header(len, seqno);

    for (const auto& a : row)
    {
        auto r = create_lestr(a);
        data.insert(data.end(), r.begin(), r.end());
    }

    return data;
}
}

ResultSet::ResultSet(const std::vector<std::string>& names)
    : m_columns(names)
{
}

std::unique_ptr<ResultSet> ResultSet::create(const std::vector<std::string>& names)
{
    return std::unique_ptr<ResultSet>(new(std::nothrow) ResultSet(names));
}

void ResultSet::add_row(const std::vector<std::string>& values)
{
    mxb_assert(values.size() == m_columns.size());
    m_rows.emplace_back(values);
}

void ResultSet::add_column(const std::string& name, const std::string& value)
{
    m_columns.push_back(name);

    for (auto& a : m_rows)
    {
        a.push_back(value);
        mxb_assert(a.size() == m_columns.size());
    }
}

mxs::Buffer ResultSet::as_buffer() const
{
    mxs::Buffer buf;
    buf.append(create_fieldcount(m_columns.size()));

    uint8_t seqno = 2;      // The second packet after field count

    for (const auto& c : m_columns)
    {
        buf.append(create_columndef(c, seqno++));
    }

    buf.append(create_eof(seqno++));

    for (const auto& r : m_rows)
    {
        buf.append(create_row(r, seqno++));
    }

    buf.append(create_eof(seqno));

    // This allows the data to be sent in one write call
    buf.make_contiguous();

    return buf;
}
