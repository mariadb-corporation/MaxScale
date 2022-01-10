/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <string.h>
#include <ctype.h>

#include <numeric>

#include <maxbase/alloc.h>
#include <maxscale/resultset.hh>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/mysql_binlog.h>
#include <maxscale/protocol/mysql.hh>

/**
 * Send the field count packet in a response packet sequence.
 *
 * @param dcb           DCB of connection to send result set to
 * @param count         Number of columns in the result set
 * @return              Non-zero on success
 */
static int mysql_send_fieldcount(DCB* dcb, int count)
{
    GWBUF* pkt;
    uint8_t* ptr;

    if ((pkt = gwbuf_alloc(5)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(pkt);
    *ptr++ = 0x01;                  // Payload length
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x01;                  // Sequence number in response
    *ptr++ = count;                 // Length of result string
    return dcb->func.write(dcb, pkt);
}

/**
 * Send the column definition packet in a response packet sequence.
 *
 * @param dcb           The DCB of the connection
 * @param name          Name of the column
 * @param type          Column type
 * @param len           Column length
 * @param seqno         Packet sequence number
 * @return              Non-zero on success
 */
static int mysql_send_columndef(DCB* dcb, const std::string& name, uint8_t seqno)
{
    GWBUF* pkt = gwbuf_alloc(26 + name.length());

    if (pkt == NULL)
    {
        return 0;
    }

    int len = 255;      // Column type length e.g. VARCHAR(255)

    uint8_t* ptr = GWBUF_DATA(pkt);
    int plen = 22 + name.length();
    *ptr++ = plen & 0xff;
    *ptr++ = (plen >> 8) & 0xff;
    *ptr++ = (plen >> 16) & 0xff;
    *ptr++ = seqno;                         // Sequence number in response
    *ptr++ = 3;                             // Catalog is always def
    *ptr++ = 'd';
    *ptr++ = 'e';
    *ptr++ = 'f';
    *ptr++ = 0;                             // Schema name length
    *ptr++ = 0;                             // virtual table name length
    *ptr++ = 0;                             // Table name length
    *ptr++ = name.length();                 // Column name length;
    memcpy(ptr, name.c_str(), name.length());
    ptr += name.length();
    *ptr++ = 0;                             // Original column name
    *ptr++ = 0x0c;                          // Length of next fields always 12
    *ptr++ = 0x3f;                          // Character set
    *ptr++ = 0;
    *ptr++ = len & 0xff;                    // Length of column
    *ptr++ = (len >> 8) & 0xff;
    *ptr++ = (len >> 16) & 0xff;
    *ptr++ = (len >> 24) & 0xff;
    *ptr++ = TABLE_COL_TYPE_VARCHAR;
    *ptr++ = 0x81;                          // Two bytes of flags
    *ptr++ = 0x00;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;
    return dcb->func.write(dcb, pkt);
}

/**
 * Send an EOF packet in a response packet sequence.
 *
 * @param dcb           The client connection
 * @param seqno         The sequence number of the EOF packet
 * @return              Non-zero on success
 */
static int mysql_send_eof(DCB* dcb, int seqno)
{
    GWBUF* pkt = gwbuf_alloc(9);

    if (pkt == NULL)
    {
        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(pkt);
    *ptr++ = 0x05;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = seqno;                         // Sequence number in response
    *ptr++ = 0xfe;                          // Length of result string
    *ptr++ = 0x00;                          // No Errors
    *ptr++ = 0x00;
    *ptr++ = 0x02;                          // Autocommit enabled
    *ptr++ = 0x00;
    return dcb->func.write(dcb, pkt);
}

/**
 * Send a row packet in a response packet sequence.
 *
 * @param dcb           The client connection
 * @param row           The row to send
 * @param seqno         The sequence number of the EOF packet
 * @return              Non-zero on success
 */
static int mysql_send_row(DCB* dcb, const std::vector<std::string>& row, int seqno)
{
    auto acc = [](int l, const std::string& s) {
            return l + s.length() + 1;
        };

    int len = std::accumulate(row.begin(), row.end(), MYSQL_HEADER_LEN, acc);

    GWBUF* pkt = gwbuf_alloc(len);

    if (pkt == NULL)
    {
        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(pkt);
    len -= MYSQL_HEADER_LEN;
    *ptr++ = len & 0xff;
    *ptr++ = (len >> 8) & 0xff;
    *ptr++ = (len >> 16) & 0xff;
    *ptr++ = seqno;

    for (const auto& a : row)
    {
        *ptr++ = a.length();
        memcpy(ptr, a.c_str(), a.length());
        ptr += a.length();
    }

    return dcb->func.write(dcb, pkt);
}

ResultSet::ResultSet(std::initializer_list<std::string> names)
    : m_columns(names)
{
}

std::unique_ptr<ResultSet> ResultSet::create(std::initializer_list<std::string> names)
{
    return std::unique_ptr<ResultSet>(new(std::nothrow) ResultSet(names));
}

void ResultSet::add_row(std::initializer_list<std::string> values)
{
    mxb_assert(values.size() == m_columns.size());
    m_rows.emplace_back(values);
}

void ResultSet::write(DCB* dcb)
{
    mysql_send_fieldcount(dcb, m_columns.size());

    uint8_t seqno = 2;      // The second packet after field count

    for (const auto& c : m_columns)
    {
        mysql_send_columndef(dcb, c, seqno++);
    }

    mysql_send_eof(dcb, seqno++);

    for (const auto& r : m_rows)
    {
        mysql_send_row(dcb, r, seqno++);
    }

    mysql_send_eof(dcb, seqno);
}

json_t* ResultSet::get_json_value(const std::string& s)
{
    json_t* js;
    char* end;
    long l = strtol(s.c_str(), &end, 10);

    if (end != s.c_str() && *end == '\0')
    {
        js = json_integer(l);
    }
    else
    {
        js = json_string(s.c_str());
    }

    return js;
}

void ResultSet::write_as_json(DCB* dcb)
{
    json_t* arr = json_array();

    for (const auto& row : m_rows)
    {
        json_t* obj = json_object();

        for (size_t i = 0; i < row.size(); i++)
        {
            json_object_set_new(obj, m_columns[i].c_str(), get_json_value(row[i]));
        }

        json_array_append_new(arr, obj);
    }

    char* js = json_dumps(arr, JSON_INDENT(4));
    dcb_printf(dcb, "%s", js);
    MXS_FREE(js);

    json_decref(arr);
}
