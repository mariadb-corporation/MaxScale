/*
 * This file is distributed as part of MaxScale from MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
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
 *
 * @endverbatim
 */
#include <buffer.h>
#include <string.h>
#include <mysql_client_server_protocol.h>
#include <modutil.h>

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
    DCB*     backend_dcb,
    int      error,
    char*    state,
    char*    errstr,
    uint32_t flags);


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
    len = MYSQL_GET_PACKET_LEN((uint8_t *)GWBUF_DATA(buf));
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

    if (!modutil_is_SQL(buf) && !modutil_is_SQL_prepare(buf))
    {
        return rval;
    }
    ptr = GWBUF_DATA(buf);
    length = *ptr++;
    length += (*ptr++ << 8);
    length += (*ptr++ << 16);

    if ((rval = (char *)malloc(length + 1)) == NULL)
    {
        return NULL;
    }
    dptr = rval;
    ptr += 2;  // Skip sequence id  and COM_QUERY byte
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
    packet_type = packet[4];

    switch (packet_type)
    {
    case MYSQL_COM_QUIT:
        len = strlen("[Quit msg]") + 1;
        if ((query_str = (char *)malloc(len + 1)) == NULL)
        {
            goto retblock;
        }
        memcpy(query_str, "[Quit msg]", len);
        memset(&query_str[len], 0, 1);
        break;

    case MYSQL_COM_QUERY:
        len = MYSQL_GET_PACKET_LEN(packet) - 1; /*< distract 1 for packet type byte */
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)malloc(len + 1)) == NULL)
        {
            goto retblock;
        }
        memcpy(query_str, &packet[5], len);
        memset(&query_str[len], 0, 1);
        break;

    default:
        len = strlen(STRPACKETTYPE(packet_type)) + 1;
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)malloc(len + 1)) == NULL)
        {
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

    mysql_statemsg[0]='#';
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
 * Buffer contains at least one of the following:
 * complete [complete] [partial] mysql packet
 *
 * return pointer to gwbuf containing a complete packet or
 *   NULL if no complete packet was found.
 */
GWBUF* modutil_get_next_MySQL_packet(GWBUF** p_readbuf)
{
    GWBUF*   packetbuf;
    GWBUF*   readbuf;
    size_t   buflen;
    size_t   packetlen;
    size_t   totalbuflen;
    uint8_t* data;
    size_t   nbytes_copied = 0;
    uint8_t* target;

    readbuf = *p_readbuf;

    if (readbuf == NULL)
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }
    CHK_GWBUF(readbuf);

    if (GWBUF_EMPTY(readbuf))
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }
    totalbuflen = gwbuf_length(readbuf);
    data        = (uint8_t *)GWBUF_DATA((readbuf));
    packetlen   = MYSQL_GET_PACKET_LEN(data) + 4;

    /** packet is incomplete */
    if (packetlen > totalbuflen)
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }

    packetbuf = gwbuf_alloc(packetlen);
    target    = GWBUF_DATA(packetbuf);
    packetbuf->gwbuf_type = readbuf->gwbuf_type; /*< Copy the type too */
    /**
     * Copy first MySQL packet to packetbuf and leave posible other
     * packets to read buffer.
     */
    while (nbytes_copied < packetlen && totalbuflen > 0)
    {
        uint8_t* src = GWBUF_DATA((*p_readbuf));
        size_t   bytestocopy;

        buflen = GWBUF_LENGTH((*p_readbuf));
        bytestocopy = MIN(buflen,packetlen-nbytes_copied);

        memcpy(target+nbytes_copied, src, bytestocopy);
        *p_readbuf = gwbuf_consume((*p_readbuf), bytestocopy);
        totalbuflen = gwbuf_length((*p_readbuf));
        nbytes_copied += bytestocopy;
    }
    ss_dassert(buflen == 0 || nbytes_copied == packetlen);

return_packetbuf:
    return packetbuf;
}

/**
 * Parse the buffer and split complete packets into individual buffers.
 * Any partial packets are left in the old buffer.
 * @param p_readbuf Buffer to split, set to NULL if no partial packets are left
 * @return Head of the chain of complete packets, all in a single, contiguous buffer
 */
GWBUF* modutil_get_complete_packets(GWBUF** p_readbuf)
{
    GWBUF *buff = NULL, *packet;
    uint8_t *ptr;
    uint32_t len,blen,total = 0;

    if (p_readbuf == NULL || (*p_readbuf) == NULL ||
       gwbuf_length(*p_readbuf) < 3)
    {
        return NULL;
    }

    packet = gwbuf_make_contiguous(*p_readbuf);
    packet->next = NULL;
    *p_readbuf = packet;
    ptr = (uint8_t*)packet->start;
    len = gw_mysql_get_byte3(ptr) + 4;
    blen = gwbuf_length(packet);

    if (len == blen)
    {
        *p_readbuf = NULL;
        return packet;
    }
    else if (len > blen)
    {
        return NULL;
    }

    while (total + len < blen)
    {
        ptr += len;
        total += len;

        /** We need at least 3 bytes of the packet header to know how long the whole
         * packet is going to be. */
        if (total + 3 >= blen)
        {
            break;
        }

        len = gw_mysql_get_byte3(ptr) + 4;
    }

    /** Full packets only, return original */
    if (total + len == blen)
    {
        *p_readbuf = NULL;
        return packet;
    }

    /** The next packet is a partial, split into complete and partial packets */
    if ((buff = gwbuf_clone_portion(packet, 0, total)) == NULL)
    {
        MXS_ERROR("Failed to partially clone buffer.");
        return NULL;
    }
    gwbuf_consume(packet,total);
    return buff;
}

/**
 * Count the number of EOF, OK or ERR packets in the buffer. Only complete
 * packets are inspected and the buffer is assumed to only contain whole packets.
 * If partial packets are in the buffer, they are ignored. The caller must handle the
 * detection of partial packets in buffers.
 * @param reply Buffer to use
 * @param use_ok Whether the DEPRECATE_EOF flag is set
 * @param n_found If there were previous packets found 
 * @return Number of EOF packets
 */
int
modutil_count_signal_packets(GWBUF *reply, int use_ok,  int n_found, int* more)
{
    unsigned char* ptr = (unsigned char*) reply->start;
    unsigned char* end = (unsigned char*) reply->end;
    unsigned char* prev = ptr;
    int pktlen, eof = 0, err = 0;
    int errlen = 0, eoflen = 0;
    int iserr = 0, iseof = 0;
    bool moreresults = false;
    while (ptr < end)
    {
        pktlen = MYSQL_GET_PACKET_LEN(ptr) + 4;

        if ((iserr = PTR_IS_ERR(ptr)) || (iseof = PTR_IS_EOF(ptr)))
        {
            if (iserr)
            {
                err++;
                errlen = pktlen;
            }
            else if (iseof)
            {
                eof++;
                eoflen = pktlen;
            }
        }

        if ((ptr + pktlen) > end || (eof + n_found) >= 2)
        {
            moreresults = PTR_EOF_MORE_RESULTS(ptr);
            ptr = prev;
            break;
        }

        prev = ptr;
        ptr += pktlen;
    }


    /*
     * If there were new EOF/ERR packets found, make sure that they are the last
     * packet in the buffer.
     */
    if ((eof || err) && n_found)
    {
        if (err)
        {
            ptr -= errlen;
            if (!PTR_IS_ERR(ptr))
            {
                err = 0;
            }
        }
        else
        {
            ptr -= eoflen;
            if (!PTR_IS_EOF(ptr))
            {
                eof = 0;
            }
        }
    }

    *more = moreresults;

    return(eof + err);
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
static void modutil_reply_routing_error(DCB*     backend_dcb,
                                        int      error,
                                        char*    state,
                                        char*    errstr,
                                        uint32_t flags)
{
    GWBUF* buf;
    CHK_DCB(backend_dcb);

    buf = modutil_create_mysql_err_msg(1, 0, error, state, errstr);
    free(errstr);

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
void* strnchr_esc(char* ptr, char c, int len)
{
    char* p = (char*)ptr;
    char* start = p;
    bool quoted = false, escaped = false;
    char qc;

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
 * Create a COM_QUERY packet from a string.
 * @param query Query to create.
 * @return Pointer to GWBUF with the query or NULL if an error occurred.
 */
GWBUF* modutil_create_query(char* query)
{
    if (query == NULL)
    {
        return NULL;
    }

    GWBUF* rval = gwbuf_alloc(strlen(query) + 5);
    int pktlen = strlen(query) + 1;
    unsigned char* ptr;

    if (rval)
    {
        ptr = (unsigned char*)rval->start;
        *ptr++ = (pktlen);
        *ptr++ = (pktlen)>>8;
        *ptr++ = (pktlen)>>16;
        *ptr++ = 0x0;
        *ptr++ = 0x03;
        memcpy(ptr,query,strlen(query));
        gwbuf_set_type(rval,GWBUF_TYPE_MYSQL);
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
        PCRE2_UCHAR errbuf[STRERROR_BUFLEN];

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
 * @see maxscale_pcre2.h
 */
mxs_pcre2_result_t modutil_mysql_wildcard_match(const char* pattern, const char* string)
{
    prepare_pcre2_patterns();
    mxs_pcre2_result_t rval = MXS_PCRE2_ERROR;
    bool err = false;
    PCRE2_SIZE matchsize = strlen(string) + 1;
    PCRE2_SIZE tempsize = matchsize;
    char* matchstr = (char*) malloc(matchsize);
    char* tempstr = (char*) malloc(tempsize);

    pcre2_match_data *mdata_percent = pcre2_match_data_create_from_pattern(re_percent, NULL);
    pcre2_match_data *mdata_single = pcre2_match_data_create_from_pattern(re_single, NULL);
    pcre2_match_data *mdata_escape = pcre2_match_data_create_from_pattern(re_escape, NULL);

    if (matchstr && tempstr && mdata_percent && mdata_single && mdata_escape)
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
                    PCRE2_UCHAR errbuf[STRERROR_BUFLEN];
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
    free(matchstr);
    free(tempstr);
    return rval;
}
