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
#include <maxbase/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/utils.h>
#include <maxscale/mysql_utils.hh>

/** These are used when converting MySQL wildcards to regular expressions */
static bool pattern_init = false;
static pcre2_code* re_percent = NULL;
static pcre2_code* re_single = NULL;
static pcre2_code* re_escape = NULL;
static const PCRE2_SPTR pattern_percent = (PCRE2_SPTR) "%";
static const PCRE2_SPTR pattern_single = (PCRE2_SPTR) "([^\\\\]|^)_";
static const PCRE2_SPTR pattern_escape = (PCRE2_SPTR) "[.]";
static const char* sub_percent = ".*";
static const char* sub_single = "$1.";
static const char* sub_escape = "\\.";

/**
 * Replace the contents of a GWBUF with the new SQL statement passed as a text string.
 * The routine takes care of the modification needed to the MySQL packet,
 * returning a GWBUF chain that can be used to send the data to a MySQL server
 *
 * @param orig  The original request in a GWBUF
 * @param sql   The SQL text to replace in the packet
 * @return A newly formed GWBUF containing the MySQL packet.
 */
GWBUF* modutil_replace_SQL(GWBUF* orig, const char* sql)
{
    unsigned char* ptr;
    int length, newlength;
    GWBUF* addition;

    if (!modutil_is_SQL(orig))
    {
        return NULL;
    }
    ptr = GWBUF_DATA(orig);
    length = *ptr++;
    length += (*ptr++ << 8);
    length += (*ptr++ << 16);
    ptr += 2;   // Skip sequence id  and COM_QUERY byte

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
char* modutil_get_SQL(GWBUF* buf)
{
    unsigned int len, length;
    unsigned char* ptr;
    char* dptr, * rval = NULL;

    if (modutil_is_SQL(buf) || modutil_is_SQL_prepare(buf)
        || MYSQL_IS_COM_INIT_DB((uint8_t*)GWBUF_DATA(buf)))
    {
        ptr = GWBUF_DATA(buf);
        length = *ptr++;
        length += (*ptr++ << 8);
        length += (*ptr++ << 16);

        rval = (char*) MXS_MALLOC(length + 1);

        if (rval)
        {
            dptr = rval;
            ptr += 2;   // Skip sequence id  and COM_QUERY byte
            len = gwbuf_link_length(buf) - 5;

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
                    len = gwbuf_link_length(buf);
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
char* modutil_get_query(GWBUF* buf)
{
    uint8_t* packet;
    mxs_mysql_cmd_t packet_type;
    size_t len;
    char* query_str = NULL;

    packet = GWBUF_DATA(buf);
    packet_type = (mxs_mysql_cmd_t)packet[4];

    switch (packet_type)
    {
    case MXS_COM_QUIT:
        len = strlen("[Quit msg]") + 1;
        if ((query_str = (char*)MXS_MALLOC(len + 1)) == NULL)
        {
            goto retblock;
        }
        memcpy(query_str, "[Quit msg]", len);
        memset(&query_str[len], 0, 1);
        break;

    case MXS_COM_QUERY:
        len = MYSQL_GET_PAYLOAD_LEN(packet) - 1;    /*< distract 1 for packet type byte */
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char*)MXS_MALLOC(len + 1)) == NULL)
        {
            if (len >= 1 && len <= ~(size_t)0 - 1)
            {
                mxb_assert(!query_str);
            }
            goto retblock;
        }
        memcpy(query_str, &packet[5], len);
        memset(&query_str[len], 0, 1);
        break;

    default:
        len = strlen(STRPACKETTYPE(packet_type)) + 1;
        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char*)MXS_MALLOC(len + 1)) == NULL)
        {
            if (len >= 1 && len <= ~(size_t)0 - 1)
            {
                mxb_assert(!query_str);
            }
            goto retblock;
        }
        memcpy(query_str, STRPACKETTYPE(packet_type), len);
        memset(&query_str[len], 0, 1);
        break;
    }   /*< switch */
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
GWBUF* modutil_create_mysql_err_msg(int packet_number,
                                    int affected_rows,
                                    int merrno,
                                    const char* statemsg,
                                    const char* msg)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t* mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    unsigned int mysql_errno = 0;
    const char* mysql_error_msg = NULL;
    const char* mysql_state = NULL;
    GWBUF* errbuf = NULL;

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

    mysql_payload_size = sizeof(field_count)
        + sizeof(mysql_err)
        + sizeof(mysql_statemsg)
        + strlen(mysql_error_msg);

    /* allocate memory for packet header + payload */
    errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    mxb_assert(errbuf != NULL);

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

// Helper function for debug assertions
static bool only_one_packet(GWBUF* buffer)
{
    mxb_assert(buffer);
    uint8_t header[4] = {};
    gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN, header);
    size_t packet_len = gw_mysql_get_byte3(header);
    size_t buffer_len = gwbuf_length(buffer);
    return packet_len + MYSQL_HEADER_LEN == buffer_len;
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
    GWBUF* packet = NULL;
    GWBUF* readbuf = *p_readbuf;

    if (readbuf)
    {
        size_t totalbuflen = gwbuf_length(readbuf);
        if (totalbuflen >= MYSQL_HEADER_LEN)
        {
            size_t packetlen;

            if (gwbuf_link_length(readbuf) >= 3)    // The length is in the 3 first bytes.
            {
                uint8_t* data = (uint8_t*)GWBUF_DATA((readbuf));
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

    mxb_assert(!packet || only_one_packet(packet));
    return packet;
}

/**
 * @brief Calculate the length of the complete MySQL packets in the buffer
 *
 * @param buffer Buffer to inspect
 * @return Length of the complete MySQL packets in bytes
 */
static size_t get_complete_packets_length(GWBUF* buffer)
{
    uint8_t packet_len[3];
    uint32_t buflen = gwbuf_link_length(buffer);
    size_t offset = 0;
    size_t total = 0;

    GWBUF* tail = buffer ? buffer->tail : nullptr;

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
                buflen = buffer ? gwbuf_link_length(buffer) : 0;
            }

            // TODO: Fix GWBUF interface so that this function can be written without
            // TODO: knowledge about the internals of GWBUF.
            if (buffer)
            {
                buffer->tail = tail;
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
                mxb_assert(!buffer);
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
GWBUF* modutil_get_complete_packets(GWBUF** p_readbuf)
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
        mxb_assert(gwbuf_length(complete) == total);
        mxb_assert(*p_readbuf == NULL || before - total == gwbuf_length(*p_readbuf));
#endif
    }
    return complete;
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
        uint8_t* ptr = (uint8_t*)rval->start;
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

/**
 * Count the number of statements in a query.
 * @param buffer Buffer to analyze.
 * @return Number of statements.
 */
int modutil_count_statements(GWBUF* buffer)
{
    char* start = ((char*)(buffer)->start + 5);
    char* ptr = start;
    char* end = ((char*)(buffer)->end);
    int num = 1;

    while (ptr < end && (ptr = mxb::strnchr_esc(ptr, ';', end - ptr)))
    {
        num++;
        while (ptr < end && *ptr == ';')
        {
            ptr++;
        }
    }

    ptr = end - 1;

    if (ptr >= start && ptr < end)
    {
        while (ptr > start && isspace(*ptr))
        {
            ptr--;
        }

        if (*ptr == ';')
        {
            num--;
        }
    }

    return num;
}

int modutil_count_packets(GWBUF* buffer)
{
    int packets = 0;
    size_t offset = 0;
    uint8_t len[3];

    while (gwbuf_copy_data(buffer, offset, 3, len) == 3)
    {
        ++packets;
        offset += gw_mysql_get_byte3(len) + MYSQL_HEADER_LEN;
    }

    return packets;
}

/**
 * Initialize the PCRE2 patterns used when converting MySQL wildcards to PCRE syntax.
 */
void prepare_pcre2_patterns()
{
    static std::mutex re_lock;
    std::lock_guard<std::mutex> guard(re_lock);

    if (!pattern_init)
    {
        int err;
        size_t erroff;
        PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];

        if ((re_percent = pcre2_compile(pattern_percent,
                                        PCRE2_ZERO_TERMINATED,
                                        0,
                                        &err,
                                        &erroff,
                                        NULL))
            && (re_single = pcre2_compile(pattern_single,
                                          PCRE2_ZERO_TERMINATED,
                                          0,
                                          &err,
                                          &erroff,
                                          NULL))
            && (re_escape = pcre2_compile(pattern_escape,
                                          PCRE2_ZERO_TERMINATED,
                                          0,
                                          &err,
                                          &erroff,
                                          NULL)))
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
        pcre2_match_data* mdata_percent = pcre2_match_data_create_from_pattern(re_percent, NULL);
        pcre2_match_data* mdata_single = pcre2_match_data_create_from_pattern(re_single, NULL);
        pcre2_match_data* mdata_escape = pcre2_match_data_create_from_pattern(re_escape, NULL);

        if (mdata_percent && mdata_single && mdata_escape)
        {
            if (mxs_pcre2_substitute(re_escape,
                                     pattern,
                                     sub_escape,
                                     &matchstr,
                                     &matchsize) == MXS_PCRE2_ERROR
                || mxs_pcre2_substitute(re_single,
                                        matchstr,
                                        sub_single,
                                        &tempstr,
                                        &tempsize) == MXS_PCRE2_ERROR
                || mxs_pcre2_substitute(re_percent,
                                        tempstr,
                                        sub_percent,
                                        &matchstr,
                                        &matchsize) == MXS_PCRE2_ERROR)
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


char* modutil_MySQL_bypass_whitespace(char* sql, size_t len)
{
    char* i = sql;
    char* end = i + len;

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
    }

    snprintf(unknow_type, sizeof(unknow_type), format_str, p);

    return unknow_type;
}

namespace maxscale
{

std::string extract_sql(const mxs::Buffer& buffer, size_t len)
{
    std::string rval;
    uint8_t cmd = mxs_mysql_get_command(buffer.get());

    if (cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_PREPARE)
    {
        size_t header_len = MYSQL_HEADER_LEN + 1;
        size_t total_len = std::min(buffer.length() - header_len, len);
        rval.reserve(total_len);

        // Skip the packet header and the command byte
        std::copy_n(std::next(buffer.begin(), header_len), total_len, std::back_inserter(rval));
    }

    return rval;
}

std::string extract_sql(GWBUF* buffer, size_t len)
{
    mxs::Buffer buf(buffer);
    std::string rval = extract_sql(buf);
    buf.release();
    return rval;
}

/**
 * Extract the SQL state from an error packet.
 *
 * @param pBuffer  Pointer to an error packet.
 * @param ppState  On return will point to the state in @c pBuffer.
 * @param pnState  On return the pointed to value will be 6.
 */
static inline void extract_error_state(uint8_t* pBuffer, uint8_t** ppState, uint16_t* pnState)
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
static inline void extract_error_message(uint8_t* pBuffer, uint8_t** ppMessage, uint16_t* pnMessage)
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

std::string extract_error(GWBUF* buffer)
{
    std::string rval;

    if (MYSQL_IS_ERROR_PACKET(((uint8_t*)GWBUF_DATA(buffer))))
    {
        size_t replylen = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
        uint8_t replybuf[replylen];
        gwbuf_copy_data(buffer, 0, sizeof(replybuf), replybuf);

        uint8_t* pState;
        uint16_t nState;
        extract_error_state(replybuf, &pState, &nState);

        uint8_t* pMessage;
        uint16_t nMessage;
        extract_error_message(replybuf, &pMessage, &nMessage);

        std::string err(reinterpret_cast<const char*>(pState), nState);
        std::string msg(reinterpret_cast<const char*>(pMessage), nMessage);

        rval = err.empty() ? msg : err + ": " + msg;
    }

    return rval;
}

GWBUF* truncate_packets(GWBUF* b, uint64_t packets)
{
    mxs::Buffer buffer(b);

    auto it = buffer.begin();
    size_t total_bytes = buffer.length();
    size_t bytes_used = 0;

    while (it != buffer.end())
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

        mxb_assert(it != buffer.end());
        it.advance(len);

        if (--packets == 0)
        {
            // Trim off the extra data at the end
            buffer.erase(it, buffer.end());
            break;
        }
    }

    return buffer.release();
}
}
