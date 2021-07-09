/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
#include <maxscale/dcb.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#define PTR_IS_RESULTSET(b)    (b[0] == 0x01 && b[1] == 0x0 && b[2] == 0x0 && b[3] == 0x01)
#define PTR_IS_EOF(b)          (b[0] == 0x05 && b[1] == 0x0 && b[2] == 0x0 && b[4] == 0xfe)
#define PTR_IS_OK(b)           (b[4] == 0x00)
#define PTR_IS_ERR(b)          (b[4] == 0xff)
#define PTR_IS_LOCAL_INFILE(b) (b[4] == 0xfb)

/** Static initialization define for modutil_state */
#define MODUTIL_STATE_INIT {0}

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

/**
 * Extract the SQL portion of a COM_QUERY packet
 *
 * NB This sets *sql to point into the packet and does not
 * allocate any new storage. The string pointed to by *sql is
 * not NULL terminated.
 *
 * This routine is very simplistic and does not deal with SQL text
 * that spans multiple buffers.
 *
 * The length returned is the complete length of the SQL, which may
 * be larger than the amount of data in this packet.
 *
 * @param       buf     The packet buffer
 * @param       sql     Pointer that is set to point at the SQL data
 * @param       length  Length of the SQL query data
 * @return      True if the packet is a COM_QUERY packet
 */
inline bool modutil_extract_SQL(GWBUF* buf, char** sql, int* length)
{
    unsigned char* ptr;

    if (!modutil_is_SQL(buf) && !modutil_is_SQL_prepare(buf))
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    *length = *ptr++;
    *length += (*ptr++ << 8);
    *length += (*ptr++ << 16);
    ptr += 2;   // Skip sequence id  and COM_QUERY byte
    *length = *length - 1;
    *sql = (char*)ptr;
    return 1;
}

/**
 * Extract the SQL portion of a COM_QUERY packet
 *
 * NB This sets *sql to point into the packet and does not
 * allocate any new storage. The string pointed to by *sql is
 * not NULL terminated.
 *
 * The number of bytes pointed to *sql is returned in *length
 *
 * The remaining number of bytes required for the complete query string
 * are returned in *residual
 *
 * @param       buf             The packet buffer
 * @param       sql             Pointer that is set to point at the SQL data
 * @param       length          Length of the SQL query data pointed to by sql
 * @param       residual        Any remain part of the query in future packets
 * @return      True if the packet is a COM_QUERY packet
 */
inline bool modutil_MySQL_Query(GWBUF* buf, char** sql, int* length, int* residual)
{
    unsigned char* ptr;

    if (!modutil_is_SQL(buf))
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    *residual = *ptr++;
    *residual += (*ptr++ << 8);
    *residual += (*ptr++ << 16);
    ptr += 2;   // Skip sequence id  and COM_QUERY byte
    *residual = *residual - 1;
    *length = gwbuf_link_length(buf) - 5;
    *residual -= *length;
    *sql = (char*)ptr;
    return 1;
}
extern char*  modutil_get_SQL(GWBUF*);
extern GWBUF* modutil_replace_SQL(GWBUF*, const char*);
extern char*  modutil_get_query(GWBUF* buf);
GWBUF*        modutil_get_next_MySQL_packet(GWBUF** p_readbuf);
GWBUF*        modutil_get_complete_packets(GWBUF** p_readbuf);

/**
 * Calculate the length of MySQL packet and how much is missing from the GWBUF
 * passed as parameter.
 *
 * This routine assumes that there is only one MySQL packet in the buffer.
 *
 * @param buf                   buffer list including the query, may consist of
 *                              multiple buffers
 * @param nbytes_missing        pointer to missing bytecount
 *
 * @return the length of MySQL packet and writes missing bytecount to
 * nbytes_missing.
 */
inline int modutil_MySQL_query_len(GWBUF* buf, int* nbytes_missing)
{
    int len;
    int buflen;

    if (!modutil_is_SQL(buf))
    {
        len = 0;
        goto retblock;
    }
    len = MYSQL_GET_PAYLOAD_LEN((uint8_t*)GWBUF_DATA(buf));
    *nbytes_missing = len - 1;
    buflen = gwbuf_length(buf);

    *nbytes_missing -= buflen - 5;

retblock:
    return len;
}

int modutil_count_statements(GWBUF* buffer);
int modutil_count_packets(GWBUF* buffer);

GWBUF* modutil_create_query(const char* query);
GWBUF* modutil_create_mysql_err_msg(int packet_number, int affected_rows, int merrno,
                                    const char* statemsg, const char* msg);
GWBUF* modutil_create_ok();
GWBUF* modutil_create_eof(uint8_t sequence);

/** Struct used for tracking the state inside the modutil functions */
typedef struct
{
    uint8_t state;
} modutil_state;

mxs_pcre2_result_t modutil_mysql_wildcard_match(const char* pattern, const char* string);

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

/** Character and token searching functions */
char* strnchr_esc(char* ptr, char c, int len);
char* strnchr_esc_mysql(char* ptr, char c, int len);
bool  is_mysql_statement_end(const char* start, int len);
bool  is_mysql_sp_end(const char* start, int len);

// TODO: Move modutil out of the core
const char* STRPACKETTYPE(int p);

namespace maxscale
{

/**
 * Extract SQL from buffer
 *
 * @param buffer Buffer containing an SQL statement
 * @param len    Maximum length of the returned string, no limit by default
 *
 * @return The SQL statement. If the buffer does not contain a SQL statement, an empty string is returned.
 */
std::string extract_sql(GWBUF* buffer, size_t len = -1);
std::string extract_sql(const mxs::Buffer& buffer, size_t len = -1);

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
