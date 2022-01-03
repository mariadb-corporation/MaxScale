/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file modutil.hh A set of useful routines for module writers
 */

#include <maxscale/ccdefs.hh>
#include <string>
#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#define PTR_IS_EOF(b) (b[0] == 0x05 && b[1] == 0x0 && b[2] == 0x0 && b[4] == 0xfe)
#define PTR_IS_ERR(b) (b[4] == 0xff)

/**
 * Check if a GWBUF structure is a MySQL COM_QUERY packet
 *
 * @param       buf     Buffer to check
 * @return      True if GWBUF is a COM_QUERY packet
 */
inline bool modutil_is_SQL(GWBUF* buf)
{
    unsigned char* ptr;

    if (gwbuf_link_length(buf) < 5)
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    return ptr[4] == 0x03;          // COM_QUERY
}

/**
 * Check if a GWBUF structure is a MySQL COM_STMT_PREPARE packet
 *
 * @param       buf     Buffer to check
 * @return      True if GWBUF is a COM_STMT_PREPARE packet
 */
inline bool modutil_is_SQL_prepare(GWBUF* buf)
{
    unsigned char* ptr;

    if (gwbuf_link_length(buf) < 5)
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    return ptr[4] == 0x16;          // COM_STMT_PREPARE
}

extern char*  modutil_get_SQL(GWBUF*);
extern GWBUF* modutil_replace_SQL(GWBUF*, const char*);

GWBUF* modutil_get_next_MySQL_packet(GWBUF** p_readbuf);
GWBUF* modutil_get_complete_packets(GWBUF** p_readbuf);

int modutil_count_statements(GWBUF* buffer);
int modutil_count_packets(GWBUF* buffer);

GWBUF* modutil_create_query(const char* query);
GWBUF* modutil_create_mysql_err_msg(int packet_number, int affected_rows, int merrno,
                                    const char* statemsg, const char* msg);
GWBUF* modutil_create_ok();
GWBUF* modutil_create_eof(uint8_t sequence);

/**
 * Given a buffer containing a MySQL statement, this function will return
 * a pointer to the first character that is not whitespace. In this context,
 * comments are also counted as whitespace. For instance:
 *
 *    "SELECT"                    => "SELECT"
 *    "  SELECT                   => "SELECT"
 *    " / * A comment * / SELECT" => "SELECT"
 *    "-- comment\nSELECT"        => "SELECT"
 *
 *  @param sql  Pointer to buffer containing a MySQL statement
 *  @param len  Length of sql.
 *
 *  @return The first non whitespace (including comments) character. If the
 *          entire buffer is only whitespace, the returned pointer will point
 *          to the character following the buffer (i.e. sql + len).
 */
char* modutil_MySQL_bypass_whitespace(char* sql, size_t len);

// TODO: Move modutil out of the core
const char* STRPACKETTYPE(int p);

namespace maxscale
{

/**
 * Extract error messages from buffers
 *
 * @param buffer Buffer containing an error
 *
 * @return String representation of the error
 */
std::string extract_error(GWBUF* buffer);

/**
 * Truncate buffers at packet boundaries
 *
 * @param b   Buffer to truncate
 * @param pkt Upper limit of how many packets to return
 *
 * @return A buffer with at most `ptk` packets in it
 */
GWBUF* truncate_packets(GWBUF* b, uint64_t pkt);
}

/**
 * Extract the SQL portion of a COM_QUERY packet
 *
 * This function is a wrapper around GWBUF::get_sql().
 * No data is copied.
 *
 * @param       buf     The packet buffer
 * @param       sql     Pointer that is set to point at the SQL data
 * @param       length  Length of the SQL query data
 * @return      True if the packet is a COM_QUERY or COM_STMT_PREPARE packet, and
 *              sql was extracted.
 */
inline bool modutil_extract_SQL(GWBUF* buf, char** pSql, int* length)
{
    const auto& sql = buf->get_sql();

    *pSql = const_cast<char*>(sql.c_str());
    *length = sql.length();

    return *length;
}
