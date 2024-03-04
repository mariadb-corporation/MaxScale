/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file modutil.c  - Implementation of useful routines for modules
 */

#include <string.h>
#include <strings.h>

#include <array>
#include <iterator>
#include <mutex>
#include <functional>
#include <cctype>
#include <mysql.h>

#include <maxsql/mariadb.hh>
#include <maxbase/alloc.hh>
#include <maxscale/buffer.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/utils.hh>
#include <maxscale/mysql_utils.hh>

/**
 * Extract the SQL from a COM_QUERY packet and return in a NULL terminated buffer.
 * The buffer should be freed by the caller when it is no longer required.
 *
 * If the packet is not a COM_QUERY packet then the function will return NULL
 *
 * @param buf   The buffer chain
 * @return Null terminated string containing query text or NULL on error
 */
char* modutil_get_SQL(GWBUF* buf)
{
    unsigned int len, length;
    unsigned char* ptr;
    char* dptr, * rval = NULL;

    if (mariadb::is_com_query_or_prepare(*buf) || MYSQL_IS_COM_INIT_DB((uint8_t*)GWBUF_DATA(buf)))
    {
        ptr = GWBUF_DATA(buf);
        length = *ptr++;
        length += (*ptr++ << 8);
        length += (*ptr++ << 16);

        rval = (char*) MXB_MALLOC(length + 1);

        if (rval)
        {
            dptr = rval;
            ptr += 2;   // Skip sequence id  and COM_QUERY byte
            len = gwbuf_link_length(buf) - 5;

            if (buf && length > 0)
            {
                int clen = length > len ? len : length;
                memcpy(dptr, ptr, clen);
                dptr += clen;
                length -= clen;
            }
            *dptr = 0;
        }
    }
    return rval;
}

/**
 * Create a COM_QUERY packet from a string.
 * @param query Query to create.
 * @return Pointer to GWBUF with the query or NULL if memory allocation failed
 */
GWBUF* modutil_create_query(const char* query)
{
    mxb_assert(query);
    size_t len = strlen(query) + 1;     // Query plus the command byte
    GWBUF* rval = gwbuf_alloc(len + MYSQL_HEADER_LEN);

    if (rval)
    {
        uint8_t* ptr = rval->data();
        *ptr++ = (len);
        *ptr++ = (len) >> 8;
        *ptr++ = (len) >> 16;
        *ptr++ = 0x0;
        *ptr++ = 0x03;
        memcpy(ptr, query, strlen(query));
    }

    return rval;
}

// See: https://mariadb.com/kb/en/library/ok_packet/
GWBUF* modutil_create_ok()
{
    uint8_t ok[] =
    {0x7, 0x0, 0x0, 0x1,// packet header
     0x0,               // OK header byte
     0x0,               // affected rows
     0x0,               // last_insert_id
     0x0, 0x0,          // server status
     0x0, 0x0           // warnings
    };

    return gwbuf_alloc_and_load(sizeof(ok), ok);
}

// See: https://mariadb.com/kb/en/library/eof_packet/
GWBUF* modutil_create_eof(uint8_t seq)
{
    uint8_t eof[] = {0x5, 0x0, 0x0, seq, 0xfe, 0x0, 0x0, 0x0, 0x0};
    return gwbuf_alloc_and_load(sizeof(eof), eof);
}

const char* modutil_MySQL_bypass_whitespace(const char* sql, size_t len)
{
    const char* i = sql;
    const char* end = i + len;

    while (i != end)
    {
        if (isspace(*i))
        {
            ++i;
        }
        else if (*i == '/')     // Might be a comment
        {
            if ((i + 1 != end) && (*(i + 1) == '*'))    // Indeed it was
            {
                i += 2;

                while (i != end)
                {
                    if (*i == '*')      // Might be the end of the comment
                    {
                        ++i;

                        if (i != end)
                        {
                            if (*i == '/')      // Indeed it was
                            {
                                ++i;
                                break;      // Out of this inner while.
                            }
                        }
                    }
                    else
                    {
                        // It was not the end of the comment.
                        ++i;
                    }
                }
            }
            else
            {
                // Was not a comment, so we'll bail out.
                break;
            }
        }
        else if (*i == '-')     // Might be the start of a comment to the end of line
        {
            bool is_comment = false;

            if (i + 1 != end)
            {
                if (*(i + 1) == '-')    // Might be, yes.
                {
                    if (i + 2 != end)
                    {
                        if (isspace(*(i + 2)))      // Yes, it is.
                        {
                            is_comment = true;

                            i += 3;

                            while ((i != end) && (*i != '\n'))
                            {
                                ++i;
                            }

                            if (i != end)
                            {
                                mxb_assert(*i == '\n');
                                ++i;
                            }
                        }
                    }
                }
            }

            if (!is_comment)
            {
                break;
            }
        }
        else if (*i == '#')
        {
            ++i;

            while ((i != end) && (*i != '\n'))
            {
                ++i;
            }

            if (i != end)
            {
                mxb_assert(*i == '\n');
                ++i;
            }
            break;
        }
        else
        {
            // Neither whitespace not start of a comment, so we bail out.
            break;
        }
    }

    return i;
}

const char format_str[] = "COM_UNKNOWN(%02hhx)";

// The message always fits inside the buffer
thread_local char unknow_type[sizeof(format_str)] = "";

const char* STRPACKETTYPE(int p)
{
    switch (p)
    {
    case MXS_COM_SLEEP:
        return "COM_SLEEP";

    case MXS_COM_QUIT:
        return "COM_QUIT";

    case MXS_COM_INIT_DB:
        return "COM_INIT_DB";

    case MXS_COM_QUERY:
        return "COM_QUERY";

    case MXS_COM_FIELD_LIST:
        return "COM_FIELD_LIST";

    case MXS_COM_CREATE_DB:
        return "COM_CREATE_DB";

    case MXS_COM_DROP_DB:
        return "COM_DROP_DB";

    case MXS_COM_REFRESH:
        return "COM_REFRESH";

    case MXS_COM_SHUTDOWN:
        return "COM_SHUTDOWN";

    case MXS_COM_STATISTICS:
        return "COM_STATISTICS";

    case MXS_COM_PROCESS_INFO:
        return "COM_PROCESS_INFO";

    case MXS_COM_CONNECT:
        return "COM_CONNECT";

    case MXS_COM_PROCESS_KILL:
        return "COM_PROCESS_KILL";

    case MXS_COM_DEBUG:
        return "COM_DEBUG";

    case MXS_COM_PING:
        return "COM_PING";

    case MXS_COM_TIME:
        return "COM_TIME";

    case MXS_COM_DELAYED_INSERT:
        return "COM_DELAYED_INSERT";

    case MXS_COM_CHANGE_USER:
        return "COM_CHANGE_USER";

    case MXS_COM_BINLOG_DUMP:
        return "COM_BINLOG_DUMP";

    case MXS_COM_TABLE_DUMP:
        return "COM_TABLE_DUMP";

    case MXS_COM_CONNECT_OUT:
        return "COM_CONNECT_OUT";

    case MXS_COM_REGISTER_SLAVE:
        return "COM_REGISTER_SLAVE";

    case MXS_COM_STMT_PREPARE:
        return "COM_STMT_PREPARE";

    case MXS_COM_STMT_EXECUTE:
        return "COM_STMT_EXECUTE";

    case MXS_COM_STMT_SEND_LONG_DATA:
        return "COM_STMT_SEND_LONG_DATA";

    case MXS_COM_STMT_CLOSE:
        return "COM_STMT_CLOSE";

    case MXS_COM_STMT_RESET:
        return "COM_STMT_RESET";

    case MXS_COM_SET_OPTION:
        return "COM_SET_OPTION";

    case MXS_COM_STMT_FETCH:
        return "COM_STMT_FETCH";

    case MXS_COM_DAEMON:
        return "COM_DAEMON";

    case MXS_COM_RESET_CONNECTION:
        return "COM_RESET_CONNECTION";

    case MXS_COM_STMT_BULK_EXECUTE:
        return "COM_STMT_BULK_EXECUTE";

    case MXS_COM_MULTI:
        return "COM_MULTI";

    case MXS_COM_XPAND_REPL:
        return "COM_XPAND_REPL";
    }

    snprintf(unknow_type, sizeof(unknow_type), format_str, static_cast<unsigned char>(p));

    return unknow_type;
}

namespace maxscale
{

/**
 * Extract the SQL state from an error packet.
 *
 * @param pBuffer  Pointer to an error packet.
 * @param ppState  On return will point to the state in @c pBuffer.
 * @param pnState  On return the pointed to value will be 6.
 */
static inline void extract_error_state(const uint8_t* pBuffer, const uint8_t** ppState, uint16_t* pnState)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state. In this context
    // the marker and the state itself are combined.
    *ppState = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    // The SQLSTATE is optional and, if present, starts with the hash sign
    *pnState = **ppState == '#' ? 6 : 0;
}

/**
 * Extract the message from an error packet.
 *
 * @param pBuffer    Pointer to an error packet.
 * @param ppMessage  On return will point to the start of the message in @c pBuffer.
 * @param pnMessage  On return the pointed to value will be the length of the message.
 */
static inline void extract_error_message(const uint8_t* pBuffer, const uint8_t** ppMessage,
                                         uint16_t* pnMessage)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    int packet_len = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(pBuffer);

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state, followed by
    // a message until the end of the packet.
    *ppMessage = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    *pnMessage = packet_len - MYSQL_HEADER_LEN - 1 - 2;

    if (**ppMessage == '#')     // The SQLSTATE is optional
    {
        (*ppMessage) += 6;
        (*pnMessage) -= 6;
    }
}

std::string extract_error(const GWBUF* buffer)
{
    std::string rval;
    auto* data = buffer->data();
    if (MYSQL_IS_ERROR_PACKET(data))
    {
        const uint8_t* pState;
        uint16_t nState;
        extract_error_state(data, &pState, &nState);

        const uint8_t* pMessage;
        uint16_t nMessage;
        extract_error_message(data, &pMessage, &nMessage);

        std::string err((const char*)pState, nState);
        std::string msg((const char*)pMessage, nMessage);

        rval = err.empty() ? msg : err + ": " + msg;
    }

    return rval;
}

GWBUF* truncate_packets(GWBUF* buffer, uint64_t packets)
{
    auto it = buffer->begin();
    size_t total_bytes = buffer->length();
    size_t bytes_used = 0;

    while (it != buffer->end())
    {
        size_t bytes_left = total_bytes - bytes_used;

        if (bytes_left < MYSQL_HEADER_LEN)
        {
            // Partial header
            break;
        }

        // Extract packet length and command byte
        uint32_t len = *it++;
        len |= (*it++) << 8;
        len |= (*it++) << 16;
        ++it;   // Skip the sequence

        if (bytes_left < len + MYSQL_HEADER_LEN)
        {
            // Partial packet payload
            break;
        }

        bytes_used += len + MYSQL_HEADER_LEN;

        mxb_assert(it != buffer->end());
        it += len;

        if (--packets == 0)
        {
            // Trim off the extra data at the end
            buffer->rtrim(std::distance(it, buffer->end()));
            break;
        }
    }

    return buffer;
}
}
