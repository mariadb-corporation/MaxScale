/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file modutil.c  - Implementation of useful routines for modules
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 04/06/14     Mark Riddoch            Initial implementation
 * 24/10/14     Massimiliano Pinto      Added modutil_send_mysql_err_packet, modutil_create_mysql_err_msg
 * 04/01/16     Martin Brampton         Streamline code in modutil_get_complete_packets
 *
 * @endverbatim
 */

#include <string.h>
#include <strings.h>

#include <maxscale/buffer.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>
#include <maxscale/poll.h>
#include <maxscale/modutil.h>
#include <maxscale/platform.h>

/** These are used when converting MySQL wildcards to regular expressions */
static SPINLOCK re_lock = SPINLOCK_INIT;
static bool pattern_init = false;
static pcre2_code *re_percent = NULL;
static pcre2_code *re_single = NULL;
static pcre2_code *re_escape = NULL;
static const PCRE2_SPTR pattern_percent = (PCRE2_SPTR) "%";
static const PCRE2_SPTR pattern_single = (PCRE2_SPTR) "([^\\\\]|^)_";
static const PCRE2_SPTR pattern_escape = (PCRE2_SPTR) "[.]";
static const char* sub_percent = ".*";
static const char* sub_single = "$1.";
static const char* sub_escape = "\\.";

static void modutil_reply_routing_error(
    DCB*        backend_dcb,
    int         error,
    const char* state,
    char*       errstr,
    uint32_t    flags);


/**
 * Check if a GWBUF structure is a MySQL COM_QUERY packet
 *
 * @param       buf     Buffer to check
 * @return      True if GWBUF is a COM_QUERY packet
 */
int
modutil_is_SQL(GWBUF *buf)
{
    unsigned char *ptr;

    if (GWBUF_LENGTH(buf) < 5)
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
int
modutil_is_SQL_prepare(GWBUF *buf)
{
    unsigned char *ptr;

    if (GWBUF_LENGTH(buf) < 5)
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    return ptr[4] == 0x16 ;         // COM_STMT_PREPARE
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
int
modutil_extract_SQL(GWBUF *buf, char **sql, int *length)
{
    unsigned char *ptr;

    if (!modutil_is_SQL(buf))
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    *length = *ptr++;
    *length += (*ptr++ << 8);
    *length += (*ptr++ << 16);
    ptr += 2;  // Skip sequence id  and COM_QUERY byte
    *length = *length - 1;
    *sql = (char *)ptr;
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
int
modutil_MySQL_Query(GWBUF *buf, char **sql, int *length, int *residual)
{
    unsigned char *ptr;

    if (!modutil_is_SQL(buf))
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    *residual = *ptr++;
    *residual += (*ptr++ << 8);
    *residual += (*ptr++ << 16);
    ptr += 2;  // Skip sequence id  and COM_QUERY byte
    *residual = *residual - 1;
    *length = GWBUF_LENGTH(buf) - 5;
    *residual -= *length;
    *sql = (char *)ptr;
    return 1;
}

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
int modutil_MySQL_query_len(GWBUF* buf, int* nbytes_missing)
{
    int len;
    int buflen;

    if (!modutil_is_SQL(buf))
    {
        len = 0;
        goto retblock;
    }
    len = MYSQL_GET_PAYLOAD_LEN((uint8_t *)GWBUF_DATA(buf));
    *nbytes_missing = len - 1;
    buflen = gwbuf_length(buf);

    *nbytes_missing -= buflen - 5;

retblock:
    return len;
}


/**
 * Replace the contents of a GWBUF with the new SQL statement passed as a text string.
 * The routine takes care of the modification needed to the MySQL packet,
 * returning a GWBUF chain that can be used to send the data to a MySQL server
 *
 * @param orig  The original request in a GWBUF
 * @param sql   The SQL text to replace in the packet
 * @return A newly formed GWBUF containing the MySQL packet.
 */
GWBUF *
modutil_replace_SQL(GWBUF *orig, char *sql)
{
    unsigned char   *ptr;
    int     length, newlength;
    GWBUF   *addition;

    if (!modutil_is_SQL(orig))
    {
        return NULL;
    }
    ptr = GWBUF_DATA(orig);
    length = *ptr++;
    length += (*ptr++ << 8);
    length += (*ptr++ << 16);
    ptr += 2;  // Skip sequence id  and COM_QUERY byte

    newlength = strlen(sql);
    if (length - 1 == newlength)
    {
        /* New SQL is the same length as old */
        memcpy(ptr, sql, newlength);
    }
    else if (length - 1 > newlength)
    {
        /* New SQL is shorter */
        memcpy(ptr, sql, newlength);
        GWBUF_RTRIM(orig, (length - 1) - newlength);
        ptr = GWBUF_DATA(orig);
        *ptr++ = (newlength + 1) & 0xff;
        *ptr++ = ((newlength + 1) >> 8) & 0xff;
        *ptr++ = ((newlength + 1) >> 16) & 0xff;
    }
    else
    {
        memcpy(ptr, sql, length - 1);
        addition = gwbuf_alloc(newlength - (length - 1));
        memcpy(GWBUF_DATA(addition), &sql[length - 1], newlength - (length - 1));
        ptr = GWBUF_DATA(orig);
        *ptr++ = (newlength + 1) & 0xff;
        *ptr++ = ((newlength + 1) >> 8) & 0xff;
        *ptr++ = ((newlength + 1) >> 16) & 0xff;
        addition->gwbuf_type = orig->gwbuf_type;
        orig->next = addition;
    }

    return orig;
}


/**
 * Extract the SQL from a COM_QUERY packet and return in a NULL terminated buffer.
 * The buffer should be freed by the caller when it is no longer required.
 *
 * If the packet is not a COM_QUERY packet then the function will return NULL
 *
 * @param buf   The buffer chain
 * @return Null terminated string containing query text or NULL on error
 */
char *
modutil_get_SQL(GWBUF *buf)
{
    unsigned int len, length;
    unsigned char *ptr;
    char *dptr, *rval = NULL;

    if (modutil_is_SQL(buf) || modutil_is_SQL_prepare(buf) ||
        MYSQL_IS_COM_INIT_DB((uint8_t*)GWBUF_DATA(buf)))
    {
        ptr = GWBUF_DATA(buf);
        length = *ptr++;
        length += (*ptr++ << 8);
        length += (*ptr++ << 16);

        rval = (char *) MXS_MALLOC(length + 1);

        if (rval)
        {
            dptr = rval;
            ptr += 2; // Skip sequence id  and COM_QUERY byte
            len = GWBUF_LENGTH(buf) - 5;

            while (buf && length > 0)
            {
                int clen = length > len ? len : length;
                memcpy(dptr, ptr, clen);
                dptr += clen;
                length -= clen;
                buf = buf->next;

                if (buf)
                {
                    ptr = GWBUF_DATA(buf);
                    len = GWBUF_LENGTH(buf);
                }
            }
            *dptr = 0;
        }
    }
    return rval;
}

/**
 * Copy query string from GWBUF buffer to separate memory area.
 *
 * @param buf   GWBUF buffer including the query
 *
 * @return Plain text query if the packet type is COM_QUERY. Otherwise return
 * a string including the packet type.
 */
char *
modutil_get_query(GWBUF *buf)
{
    uint8_t* packet;
    mysql_server_cmd_t packet_type;
    size_t len;
    char* query_str = NULL;

    packet = GWBUF_DATA(buf);
    packet_type = (mysql_server_cmd_t)packet[4];

    switch (packet_type)
    {
    case MYSQL_COM_QUIT:
        len = strlen("[Quit msg]") + 1;
        if ((query_str = (char *)MXS_MALLOC(len + 1)) == NULL)
        {
            goto retblock;
        }
        memcpy(query_str, "[Quit msg]", len);
        memset(&query_str[len], 0, 1);
        break;

    case MYSQL_COM_QUERY:
        len = MYSQL_GET_PAYLOAD_LEN(packet) - 1; /*< distract 1 for packet type byte */
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)MXS_MALLOC(len + 1)) == NULL)
        {
            if (len >= 1 && len <= ~(size_t)0 - 1)
            {
                ss_dassert(!query_str);
            }
            goto retblock;
        }
        memcpy(query_str, &packet[5], len);
        memset(&query_str[len], 0, 1);
        break;

    default:
        len = strlen(STRPACKETTYPE(packet_type)) + 1;
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)MXS_MALLOC(len + 1)) == NULL)
        {
            if (len >= 1 && len <= ~(size_t)0 - 1)
            {
                ss_dassert(!query_str);
            }
            goto retblock;
        }
        memcpy(query_str, STRPACKETTYPE(packet_type), len);
        memset(&query_str[len], 0, 1);
        break;
    } /*< switch */
retblock:
    return query_str;
}


/**
 * create a GWBUFF with a MySQL ERR packet
 *
 * @param packet_number         MySQL protocol sequence number in the packet
 * @param in_affected_rows      MySQL affected rows
 * @param mysql_errno           The MySQL errno
 * @param sqlstate_msg          The MySQL State Message
 * @param mysql_message         The Error Message
 * @return      The allocated GWBUF or NULL on failure
 */
GWBUF *modutil_create_mysql_err_msg(int        packet_number,
                                    int        affected_rows,
                                    int        merrno,
                                    const char *statemsg,
                                    const char *msg)
{
    uint8_t *outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t *mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    unsigned int mysql_errno = 0;
    const char *mysql_error_msg = NULL;
    const char *mysql_state = NULL;
    GWBUF *errbuf = NULL;

    if (statemsg == NULL || msg == NULL)
    {
        return NULL;
    }
    mysql_errno = (unsigned int)merrno;
    mysql_error_msg = msg;
    mysql_state = statemsg;

    field_count = 0xff;

    gw_mysql_set_byte2(mysql_err, mysql_errno);

    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    mysql_payload_size = sizeof(field_count) +
                         sizeof(mysql_err) +
                         sizeof(mysql_statemsg) +
                         strlen(mysql_error_msg);

    /* allocate memory for packet header + payload */
    errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    ss_dassert(errbuf != NULL);

    if (errbuf == NULL)
    {
        return NULL;
    }
    outbuf = GWBUF_DATA(errbuf);

    /** write packet header and packet number */
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = packet_number;

    /** write header */
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    mysql_payload = outbuf + sizeof(mysql_packet_header);

    /** write field */
    memcpy(mysql_payload, &field_count, sizeof(field_count));
    mysql_payload = mysql_payload + sizeof(field_count);

    /** write errno */
    memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
    mysql_payload = mysql_payload + sizeof(mysql_err);

    /** write sqlstate */
    memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
    mysql_payload = mysql_payload + sizeof(mysql_statemsg);

    /** write error message */
    memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

    return errbuf;
}

/**
 * modutil_send_mysql_err_packet
 *
 * Send a MySQL protocol Generic ERR message, to the dcb
 *
 * @param dcb                   The DCB to send the packet
 * @param packet_number         MySQL protocol sequence number in the packet
 * @param in_affected_rows      MySQL affected rows
 * @param mysql_errno           The MySQL errno
 * @param sqlstate_msg          The MySQL State Message
 * @param mysql_message         The Error Message
 * @return      0 for successful dcb write or 1 on failure
 *
 */
int modutil_send_mysql_err_packet(DCB        *dcb,
                                  int        packet_number,
                                  int        in_affected_rows,
                                  int        mysql_errno,
                                  const char *sqlstate_msg,
                                  const char *mysql_message)
{
    GWBUF* buf;

    buf = modutil_create_mysql_err_msg(packet_number, in_affected_rows, mysql_errno,
                                       sqlstate_msg, mysql_message);

    return dcb->func.write(dcb, buf);
}

/**
 * Return the first packet from a buffer.
 *
 * @param p_readbuf Pointer to pointer to GWBUF. If the GWBUF contains a
 *                  complete packet, after the call it will have been updated
 *                  to begin at the byte following the packet.
 *
 * @return Pointer to GWBUF if the buffer contained at least one complete packet,
 *         otherwise NULL.
 *
 * @attention The returned GWBUF is not necessarily contiguous.
 */
GWBUF* modutil_get_next_MySQL_packet(GWBUF** p_readbuf)
{
    GWBUF *packet = NULL;
    GWBUF *readbuf = *p_readbuf;

    if (readbuf)
    {
        CHK_GWBUF(readbuf);

        size_t totalbuflen = gwbuf_length(readbuf);
        if (totalbuflen >= MYSQL_HEADER_LEN)
        {
            size_t packetlen;

            if (GWBUF_LENGTH(readbuf) >= 3) // The length is in the 3 first bytes.
            {
                uint8_t *data = (uint8_t *)GWBUF_DATA((readbuf));
                packetlen = MYSQL_GET_PAYLOAD_LEN(data) + 4;
            }
            else
            {
                // The header is split between two GWBUFs.
                uint8_t data[3];
                gwbuf_copy_data(readbuf, 0, 3, data);
                packetlen = MYSQL_GET_PAYLOAD_LEN(data) + 4;
            }

            if (packetlen <= totalbuflen)
            {
                packet = gwbuf_split(p_readbuf, packetlen);
            }
        }
    }

    return packet;
}

/**
 * @brief Calculate the length of the complete MySQL packets in the buffer
 *
 * @param buffer Buffer to inspect
 * @return Length of the complete MySQL packets in bytes
 */
static size_t get_complete_packets_length(GWBUF *buffer)
{
    uint8_t packet_len[3];
    uint32_t buflen = GWBUF_LENGTH(buffer);
    size_t offset = 0;
    size_t total = 0;

    while (buffer && gwbuf_copy_data(buffer, offset, 3, packet_len) == 3)
    {
        uint32_t len = gw_mysql_get_byte3(packet_len) + MYSQL_HEADER_LEN;

        if (len < buflen)
        {
            offset += len;
            total += len;
            buflen -= len;
        }
        /** The packet is spread across multiple buffers or a buffer ends with
         * a complete packet. */
        else
        {
            uint32_t read_len = len;

            while (read_len >= buflen && buffer)
            {
                read_len -= buflen;
                buffer = buffer->next;
                buflen = buffer ? GWBUF_LENGTH(buffer) : 0;
            }

            /** Either the buffer ended with a complete packet or the buffer
             * contains more data than is required. */
            if (read_len == 0 || (buffer && read_len < buflen))
            {
                total += len;
                offset = read_len;
                buflen -= read_len;
            }
            /** The buffer chain contains at least one incomplete packet */
            else
            {
                ss_dassert(!buffer);
                break;
            }
        }
    }

    return total;
}

/**
 * @brief Split the buffer into complete and partial packets
 *
 * @param p_readbuf Buffer to split, set to NULL if no partial packets are left
 * @return Head of the chain of complete packets or NULL if no complete packets
 * are available
 */
GWBUF* modutil_get_complete_packets(GWBUF **p_readbuf)
{
    size_t buflen;
    /** At least 3 bytes are needed to calculate the packet length. */
    if (p_readbuf == NULL || (*p_readbuf) == NULL || (buflen = gwbuf_length(*p_readbuf)) < 3)
    {
        return NULL;
    }

    size_t total = get_complete_packets_length(*p_readbuf);
    GWBUF* complete = NULL;

    if (buflen == total)
    {
        complete = *p_readbuf;
        *p_readbuf = NULL;
    }
    else if (total > 0)
    {
#ifdef SS_DEBUG
        size_t before = gwbuf_length(*p_readbuf);
#endif
        complete = gwbuf_split(p_readbuf, total);
#ifdef SS_DEBUG
        ss_dassert(gwbuf_length(complete) == total);
        ss_dassert(*p_readbuf == NULL || before - total == gwbuf_length(*p_readbuf));
#endif
    }
    return complete;
}

int modutil_count_signal_packets(GWBUF *reply, int n_found, bool* more, modutil_state* state)
{
    unsigned int len = gwbuf_length(reply);
    int eof = 0;
    int err = 0;
    size_t offset = 0;
    bool skip_next = state ? state->state : false;

    while (offset < len)
    {
        uint8_t header[MYSQL_HEADER_LEN + 5]; // Maximum size of an EOF packet

        gwbuf_copy_data(reply, offset, MYSQL_HEADER_LEN + 1, header);

        unsigned int payloadlen = MYSQL_GET_PAYLOAD_LEN(header);
        unsigned int pktlen = payloadlen + MYSQL_HEADER_LEN;

        if (payloadlen == GW_MYSQL_MAX_PACKET_LEN)
        {
            skip_next = true;
        }
        else if (skip_next)
        {
            skip_next = false;
        }
        else
        {
            uint8_t command = MYSQL_GET_COMMAND(header);

            if (command == MYSQL_REPLY_ERR)
            {
                err++;
            }
            else if (command == MYSQL_REPLY_EOF && pktlen == MYSQL_EOF_PACKET_LEN)
            {
                eof++;
            }
        }

        if (offset + pktlen >= len || (eof + err + n_found) >= 2)
        {
            gwbuf_copy_data(reply, offset, sizeof(header), header);
            uint16_t* status = (uint16_t*)(header + MYSQL_HEADER_LEN + 1 + 2); // Skip command and warning count
            *more = ((*status) & SERVER_MORE_RESULTS_EXIST);
            offset += pktlen;
            break;
        }

        offset += pktlen;
    }

    int total = err + eof + n_found;

    if (state)
    {
        state->state = skip_next;
    }

    return total;
}

/**
 * Create parse error and EPOLLIN event to event queue of the backend DCB.
 * When event is notified the error message is processed as error reply and routed
 * upstream to client.
 *
 * @param backend_dcb   DCB where event is added
 * @param errstr        Plain-text string error
 * @param flags         GWBUF type flags
 */
void modutil_reply_parse_error(DCB*     backend_dcb,
                               char*    errstr,
                               uint32_t flags)
{
    CHK_DCB(backend_dcb);
    modutil_reply_routing_error(backend_dcb, 1064, "42000", errstr, flags);
}

/**
 * Create authentication error and EPOLLIN event to event queue of the backend DCB.
 * When event is notified the error message is processed as error reply and routed
 * upstream to client.
 *
 * @param backend_dcb   DCB where event is added
 * @param errstr        Plain-text string error
 * @param flags         GWBUF type flags
 */
void modutil_reply_auth_error(DCB*     backend_dcb,
                              char*    errstr,
                              uint32_t flags)
{
    CHK_DCB(backend_dcb);
    modutil_reply_routing_error(backend_dcb, 1045, "28000", errstr, flags);
}


/**
 * Create error message and EPOLLIN event to event queue of the backend DCB.
 * When event is notified the message is processed as error reply and routed
 * upstream to client.
 *
 * @param backend_dcb   DCB where event is added
 * @param error         SQL error number
 * @param state         SQL state
 * @param errstr        Plain-text string error
 * @param flags         GWBUF type flags
 */
static void modutil_reply_routing_error(DCB*        backend_dcb,
                                        int         error,
                                        const char* state,
                                        char*       errstr,
                                        uint32_t    flags)
{
    GWBUF* buf;
    CHK_DCB(backend_dcb);

    buf = modutil_create_mysql_err_msg(1, 0, error, state, errstr);
    MXS_FREE(errstr);

    if (buf == NULL)
    {
        MXS_ERROR("Creating routing error message failed.");
        return;
    }
    /** Set flags that help router to process reply correctly */
    gwbuf_set_type(buf, flags);
    /** Create an incoming event for backend DCB */
    poll_add_epollin_event_to_dcb(backend_dcb, buf);
    return;
}

/**
 * Find the first occurrence of a character in a string. This function ignores
 * escaped characters and all characters that are enclosed in single or double quotes.
 * @param ptr Pointer to area of memory to inspect
 * @param c Character to search for
 * @param len Size of the memory area
 * @return Pointer to the first non-escaped, non-quoted occurrence of the character.
 * If the character is not found, NULL is returned.
 */
char* strnchr_esc(char* ptr, char c, int len)
{
    char* p = (char*)ptr;
    char* start = p;
    bool quoted = false, escaped = false;
    char qc = 0;

    while (p < start + len)
    {
        if (escaped)
        {
            escaped = false;
        }
        else if (*p == '\\')
        {
            escaped = true;
        }
        else if ((*p == '\'' || *p  == '"') && !quoted)
        {
            quoted = true;
            qc = *p;
        }
        else if (quoted && *p == qc)
        {
            quoted = false;
        }
        else if (*p == c && !escaped && !quoted)
        {
            return p;
        }
        p++;
    }

    return NULL;
}

/**
 * Find the first occurrence of a character in a string. This function ignores
 * escaped characters and all characters that are enclosed in single or double quotes.
 * MySQL style comment blocks and identifiers in backticks are also ignored.
 * @param ptr Pointer to area of memory to inspect
 * @param c Character to search for
 * @param len Size of the memory area
 * @return Pointer to the first non-escaped, non-quoted occurrence of the character.
 * If the character is not found, NULL is returned.
 */
char* strnchr_esc_mysql(char* ptr, char c, int len)
{
    char* p = (char*) ptr;
    char* start = p, *end = start + len;
    bool quoted = false, escaped = false, backtick = false, comment = false;
    char qc = 0;

    while (p < end)
    {
        if (escaped)
        {
            escaped = false;
        }
        else if ((!comment && !quoted && !backtick) || (comment && *p == '*') ||
                 (!comment && quoted && *p == qc) || (!comment && backtick && *p == '`'))
        {
            switch (*p)
            {
            case '\\':
                escaped = true;
                break;

            case '\'':
            case '"':
                if (!quoted)
                {
                    quoted = true;
                    qc = *p;
                }
                else if (*p == qc)
                {
                    quoted = false;
                }
                break;

            case '/':
                if (p + 1 < end && *(p + 1) == '*')
                {
                    comment = true;
                    p += 1;
                }
                break;

            case '*':
                if (comment && p + 1 < end && *(p + 1) == '/')
                {
                    comment = false;
                    p += 1;
                }
                break;

            case '`':
                backtick = !backtick;
                break;

            case '#':
                return NULL;

            case '-':
                if (p + 2 < end && *(p + 1) == '-' &&
                    isspace(*(p + 2)))
                {
                    return NULL;
                }
                break;

            default:
                break;
            }

            if (*p == c && !escaped && !quoted && !comment && !backtick)
            {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

/**
 * @brief Check if the string is the final part of a valid SQL statement
 *
 * This function checks whether the string pointed by @p start contains any
 * tokens that are interpreted as executable commands.
 * @param start String containing the statement
 * @param len Length of the string
 * @return True if statement contains no executable parts
 */
bool is_mysql_statement_end(const char* start, int len)
{
    const char *ptr = start;
    bool rval = false;

    while (ptr < start + len && (isspace(*ptr) || *ptr == ';'))
    {
        ptr++;
    }

    if (ptr < start + len)
    {
        switch (*ptr)
        {
        case '-':
            if (ptr < start + len - 2 && *(ptr + 1) == '-' && isspace(*(ptr + 2)))
            {
                rval = true;
            }
            break;

        case '#':
            rval = true;
            break;

        case '/':
            if (ptr < start + len - 1 && *(ptr + 1) == '*')
            {
                rval = true;
            }
            break;
        }
    }
    else
    {
        rval = true;
    }

    return rval;
}

/**
 * @brief Check if the token is the END part of a BEGIN ... END block.
 * @param ptr String with at least three non-whitespace characters in it
 * @return True if the token is the final part of a BEGIN .. END block
 */
bool is_mysql_sp_end(const char* start, int len)
{
    const char *ptr = start;

    while (ptr < start + len && (isspace(*ptr) || *ptr == ';'))
    {
        ptr++;
    }

    return ptr < start + len - 3 && strncasecmp(ptr, "end", 3) == 0;
}

/**
 * Create a COM_QUERY packet from a string.
 * @param query Query to create.
 * @return Pointer to GWBUF with the query or NULL if memory allocation failed
 */
GWBUF* modutil_create_query(const char* query)
{
    ss_dassert(query);
    size_t len = strlen(query) + 1; // Query plus the command byte
    GWBUF* rval = gwbuf_alloc(len + MYSQL_HEADER_LEN);

    if (rval)
    {
        uint8_t *ptr = (uint8_t*)rval->start;
        *ptr++ = (len);
        *ptr++ = (len) >> 8;
        *ptr++ = (len) >> 16;
        *ptr++ = 0x0;
        *ptr++ = 0x03;
        memcpy(ptr, query, strlen(query));
    }

    return rval;
}

/**
 * Count the number of statements in a query.
 * @param buffer Buffer to analyze.
 * @return Number of statements.
 */
int modutil_count_statements(GWBUF* buffer)
{
    char* ptr = ((char*)(buffer)->start + 5);
    char* end = ((char*)(buffer)->end);
    int num = 1;

    while (ptr < end && (ptr = strnchr_esc(ptr, ';', end - ptr)))
    {
        num++;
        while (*ptr == ';')
        {
            ptr++;
        }
    }

    ptr = end - 1;
    while (isspace(*ptr))
    {
        ptr--;
    }

    if (*ptr == ';')
    {
        num--;
    }

    return num;
}

/**
 * Initialize the PCRE2 patterns used when converting MySQL wildcards to PCRE syntax.
 */
void prepare_pcre2_patterns()
{
    spinlock_acquire(&re_lock);
    if (!pattern_init)
    {
        int err;
        size_t erroff;
        PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];

        if ((re_percent = pcre2_compile(pattern_percent, PCRE2_ZERO_TERMINATED,
                                        0, &err, &erroff, NULL)) &&
            (re_single = pcre2_compile(pattern_single, PCRE2_ZERO_TERMINATED,
                                       0, &err, &erroff, NULL)) &&
            (re_escape = pcre2_compile(pattern_escape, PCRE2_ZERO_TERMINATED,
                                       0, &err, &erroff, NULL)))
        {
            assert(!pattern_init);
            pattern_init = true;
        }
        else
        {
            pcre2_get_error_message(err, errbuf, sizeof(errbuf));
            MXS_ERROR("Failed to compile PCRE2 pattern: %s", errbuf);
        }

        if (!pattern_init)
        {
            pcre2_code_free(re_percent);
            pcre2_code_free(re_single);
            pcre2_code_free(re_escape);
            re_percent = NULL;
            re_single = NULL;
            re_escape = NULL;
        }
    }
    spinlock_release(&re_lock);
}

/**
 * Check if @c string matches @c pattern according to the MySQL wildcard rules.
 * The wildcard character @c '%%' is replaced with @c '.*' and @c '_' is replaced
 * with @c '.'. All Regular expression special characters are escaped before
 * matching is made.
 * @param pattern Wildcard pattern
 * @param string String to match
 * @return MXS_PCRE2_MATCH if the pattern matches, MXS_PCRE2_NOMATCH if it does
 * not match and MXS_PCRE2_ERROR if an error occurred
 * @see maxscale/pcre2.h
 */
mxs_pcre2_result_t modutil_mysql_wildcard_match(const char* pattern, const char* string)
{
    prepare_pcre2_patterns();
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    bool err = false;
    PCRE2_SIZE matchsize = strlen(string) + 1;
    PCRE2_SIZE tempsize = matchsize;
    char* matchstr = (char*) MXS_MALLOC(matchsize);
    char* tempstr = (char*) MXS_MALLOC(tempsize);

    if (matchstr && tempstr)
    {
        pcre2_match_data *mdata_percent = pcre2_match_data_create_from_pattern(re_percent, NULL);
        pcre2_match_data *mdata_single = pcre2_match_data_create_from_pattern(re_single, NULL);
        pcre2_match_data *mdata_escape = pcre2_match_data_create_from_pattern(re_escape, NULL);

        if (mdata_percent && mdata_single && mdata_escape)
        {
            if (mxs_pcre2_substitute(re_escape, pattern, sub_escape,
                                     &matchstr, &matchsize) == MXS_PCRE2_ERROR ||
                mxs_pcre2_substitute(re_single, matchstr, sub_single,
                                     &tempstr, &tempsize) == MXS_PCRE2_ERROR ||
                mxs_pcre2_substitute(re_percent, tempstr, sub_percent,
                                     &matchstr, &matchsize) == MXS_PCRE2_ERROR)
            {
                err = true;
            }

            if (!err)
            {
                int errcode;
                rval = mxs_pcre2_simple_match(matchstr, string, PCRE2_CASELESS, &errcode);
                if (rval == MXS_PCRE2_ERROR)
                {
                    if (errcode != 0)
                    {
                        PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];
                        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
                        MXS_ERROR("Failed to match pattern: %s", errbuf);
                    }
                    err = true;
                }
            }
        }
        else
        {
            err = true;
        }

        if (err)
        {
            MXS_ERROR("Fatal error when matching wildcard patterns.");
        }

        pcre2_match_data_free(mdata_percent);
        pcre2_match_data_free(mdata_single);
        pcre2_match_data_free(mdata_escape);
    }

    MXS_FREE(matchstr);
    MXS_FREE(tempstr);

    return rval;
}

/*
 * Replace user-provided literals with question marks.
 *
 * TODO: Make the canonicalization allocate only one buffer of memory
 *
 * @param querybuf GWBUF with a COM_QUERY statement
 * @return A copy of the query in its canonical form or NULL if an error occurred.
 */
char* modutil_get_canonical(GWBUF* querybuf)
{
    char *querystr = NULL;

    if (GWBUF_LENGTH(querybuf) > MYSQL_HEADER_LEN + 1 && GWBUF_IS_SQL(querybuf))
    {
        size_t srcsize = GWBUF_LENGTH(querybuf) - MYSQL_HEADER_LEN - 1;
        char *src = (char*)GWBUF_DATA(querybuf) + MYSQL_HEADER_LEN + 1;
        size_t destsize = 0;
        char *dest = NULL;

        if (replace_quoted((const char**)&src, &srcsize, &dest, &destsize))
        {
            /** Reset the buffers so that the old result is reused and a new
             * result is created.*/
            src = dest;
            srcsize = destsize;
            dest = NULL;
            destsize = 0;

            if (remove_mysql_comments((const char**)&src, &srcsize, &dest, &destsize))
            {
                /** Both buffers now contain allocated memory so all we need
                 * to do is to swap them */
                if (replace_values((const char**)&dest, &destsize, &src, &srcsize))
                {
                    querystr = squeeze_whitespace(src);
                    MXS_FREE(dest);
                }
                else
                {
                    MXS_FREE(src);
                    MXS_FREE(dest);
                }
            }
            else
            {
                MXS_FREE(src);
            }
        }
    }

    return querystr;
}


char* modutil_MySQL_bypass_whitespace(char* sql, size_t len)
{
    char *i = sql;
    char *end = i + len;

    while (i != end)
    {
        if (isspace(*i))
        {
            ++i;
        }
        else if (*i == '/') // Might be a comment
        {
            if ((i + 1 != end) && (*(i + 1) == '*')) // Indeed it was
            {
                i += 2;

                while (i != end)
                {
                    if (*i == '*') // Might be the end of the comment
                    {
                        ++i;

                        if (i != end)
                        {
                            if (*i == '/') // Indeed it was
                            {
                                ++i;
                                break; // Out of this inner while.
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
        else if (*i == '-') // Might be the start of a comment to the end of line
        {
            bool is_comment = false;

            if (i + 1 != end)
            {
                if (*(i + 1) == '-') // Might be, yes.
                {
                    if (i + 2 != end)
                    {
                        if (isspace(*(i + 2))) // Yes, it is.
                        {
                            is_comment = true;

                            i += 3;

                            while ((i != end) && (*i != '\n'))
                            {
                                ++i;
                            }

                            if (i != end)
                            {
                                ss_dassert(*i == '\n');
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
                ss_dassert(*i == '\n');
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

bool modutil_ignorable_ping(DCB *dcb)
{
    static uint8_t com_ping_packet[] =
    {
        0x01, 0x00, 0x00, 0x00, 0x0e
    };

    GWBUF *buf = gwbuf_alloc_and_load(sizeof(com_ping_packet), com_ping_packet);
    bool rval = false;

    if (buf)
    {
        gwbuf_set_type(buf, GWBUF_TYPE_IGNORABLE);

        if (dcb->func.write(dcb, buf))
        {
            rval = true;
        }
    }

    return rval;
}

const char format_str[] = "COM_UNKNOWN(%02hhx)";

// The message always fits inside the buffer
thread_local char unknow_type[sizeof(format_str)] = "";

const char* STRPACKETTYPE(int p)
{
    switch (p)
    {
        case MYSQL_COM_SLEEP:
            return "MYSQL_COM_SLEEP";
        case MYSQL_COM_QUIT:
            return "MYSQL_COM_QUIT";
        case MYSQL_COM_INIT_DB:
            return "MYSQL_COM_INIT_DB";
        case MYSQL_COM_QUERY:
            return "MYSQL_COM_QUERY";
        case MYSQL_COM_FIELD_LIST:
            return "MYSQL_COM_FIELD_LIST";
        case MYSQL_COM_CREATE_DB:
            return "MYSQL_COM_CREATE_DB";
        case MYSQL_COM_DROP_DB:
            return "MYSQL_COM_DROP_DB";
        case MYSQL_COM_REFRESH:
            return "MYSQL_COM_REFRESH";
        case MYSQL_COM_SHUTDOWN:
            return "MYSQL_COM_SHUTDOWN";
        case MYSQL_COM_STATISTICS:
            return "MYSQL_COM_STATISTICS";
        case MYSQL_COM_PROCESS_INFO:
            return "MYSQL_COM_PROCESS_INFO";
        case MYSQL_COM_CONNECT:
            return "MYSQL_COM_CONNECT";
        case MYSQL_COM_PROCESS_KILL:
            return "MYSQL_COM_PROCESS_KILL";
        case MYSQL_COM_DEBUG:
            return "MYSQL_COM_DEBUG";
        case MYSQL_COM_PING:
            return "MYSQL_COM_PING";
        case MYSQL_COM_TIME:
            return "MYSQL_COM_TIME";
        case MYSQL_COM_DELAYED_INSERT:
            return "MYSQL_COM_DELAYED_INSERT";
        case MYSQL_COM_CHANGE_USER:
            return "MYSQL_COM_CHANGE_USER";
        case MYSQL_COM_BINLOG_DUMP:
            return "MYSQL_COM_BINLOG_DUMP";
        case MYSQL_COM_TABLE_DUMP:
            return "MYSQL_COM_TABLE_DUMP";
        case MYSQL_COM_CONNECT_OUT:
            return "MYSQL_COM_CONNECT_OUT";
        case MYSQL_COM_REGISTER_SLAVE:
            return "MYSQL_COM_REGISTER_SLAVE";
        case MYSQL_COM_STMT_PREPARE:
            return "MYSQL_COM_STMT_PREPARE";
        case MYSQL_COM_STMT_EXECUTE:
            return "MYSQL_COM_STMT_EXECUTE";
        case MYSQL_COM_STMT_SEND_LONG_DATA:
            return "MYSQL_COM_STMT_SEND_LONG_DATA";
        case MYSQL_COM_STMT_CLOSE:
            return "MYSQL_COM_STMT_CLOSE";
        case MYSQL_COM_STMT_RESET:
            return "MYSQL_COM_STMT_RESET";
        case MYSQL_COM_SET_OPTION:
            return "MYSQL_COM_SET_OPTION";
        case MYSQL_COM_STMT_FETCH:
            return "MYSQL_COM_STMT_FETCH";
        case MYSQL_COM_DAEMON:
            return "MYSQL_COM_DAEMON";
    }

    snprintf(unknow_type, sizeof(unknow_type), format_str, p);

    return unknow_type;
}
