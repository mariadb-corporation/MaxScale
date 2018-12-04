/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * This filter replaces binlog events being sent
 * by binlogrouter module to connected slave server.
 * The checked binlog events are related to DML
 * or DDL statements:
 * if configuration matches, the affected eventa and following ones
 * are replaced by RAND_EVENT events
 *
 * (1) Binlog events being checked
 *
 * - HEARTBEAT_EVENT: always skipped
 * - MARIADB10_GTID_EVENT: just resets filtering process
 * - MARIADB_ANNOTATE_ROWS_EVENT: filtering is possible
 * - TABLE_MAP_EVENT: filtering is possible
 * - QUERY_EVENT: filtering is possible.
 *   If statement is COMMIT, filtering process stops
 * - XID_EVENT: filtering process stops.
 *
 * (2) Replacing events
 *
 * Events are replaced by a RAND_EVENT, which is in details:
 *
 * - 19 bytes binlog header
 * - 8 bytes first seed
 * - 8 bytes second seed
 * - 4 bytes CRC32 (if required)
 *
 * Number of bytes: 35 without CRC32 ad 39 with it.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "binlogfilter"

#include <zlib.h>
#include <inttypes.h>
#include <algorithm>

#include <maxscale/protocol/mysql.hh>
#include <maxscale/alloc.h>
#include <maxscale/poll.hh>

#include "binlogfilter.hh"
#include "binlogfiltersession.hh"

/**
 * BinlogFilterSession constructor
 *
 * @param pSession    The calling routing/filter session
 * @param pFilter     Pointer to filter configuration
 */
BinlogFilterSession::BinlogFilterSession(MXS_SESSION* pSession,
                                         const BinlogFilter* pFilter)
    : mxs::FilterSession(pSession)
    , m_filter(*pFilter)
{
}

/**
 * BinlogFilterSession destructor
 */
BinlogFilterSession::~BinlogFilterSession()
{
}

// static
/**
 * create new filter session
 *
 * @param pSession    The calling routing/filter session
 * @param pFilter     Pointer to filter configuration
 * @return            The new allocated session
 */
BinlogFilterSession* BinlogFilterSession::create(MXS_SESSION* pSession,
                                                 const BinlogFilter* pFilter)
{
    return new BinlogFilterSession(pSession, pFilter);
}

static bool is_master_binlog_checksum(GWBUF* buffer)
{
    const char target[] = "SELECT @master_binlog_checksum";
    char query[1024];       // Large enough for most practical cases
    size_t bytes = gwbuf_copy_data(buffer, MYSQL_HEADER_LEN + 1, sizeof(query) - 1, (uint8_t*)query);
    query[bytes] = '\0';

    return strcasestr(query, target);
}

/**
 * Route input data from client.
 *
 * Input data might be related to:
 * - SQL commands
 * - Slave Replication protocol
 *
 * When member variable m_state is BINLOG_MODE,
 * event filtering is possible.
 *
 * @param pPacket    The inout data from client
 * @return           0 on errors, >0 otherwise.
 */
int BinlogFilterSession::routeQuery(GWBUF* pPacket)
{
    uint8_t* data = GWBUF_DATA(pPacket);

    switch (MYSQL_GET_COMMAND(data))
    {
    case MXS_COM_REGISTER_SLAVE:
        // Connected client is registering as Slave Server
        m_serverid = gw_mysql_get_byte4(data + MYSQL_HEADER_LEN + 1);
        MXS_INFO("Client is registering as Slave server with ID %u", m_serverid);
        break;

    case MXS_COM_BINLOG_DUMP:
        // Connected Slave server is waiting for binlog events
        m_state = BINLOG_MODE;
        MXS_INFO("Slave server %u is waiting for binlog events.", m_serverid);
        break;

    case MXS_COM_QUERY:
        // Connected client is using SQL mode
        m_state = COMMAND_MODE;
        m_reading_checksum = is_master_binlog_checksum(pPacket);
        break;

    default:
        // Not something we care about, just pass it through
        break;
    }

    // Route input data
    return mxs::FilterSession::routeQuery(pPacket);
}

/**
 * Extract binlog replication header from event data
 *
 * @param event    The replication event
 * @param hdr      Pointer to repliction header to fill
 */
static void extract_header(register const uint8_t* event,
                           register REP_HEADER* hdr)
{
    hdr->seqno = event[3];
    hdr->payload_len = gw_mysql_get_byte3(event);
    hdr->ok = event[MYSQL_HEADER_LEN];
    if (hdr->ok != 0)
    {
        // Don't parse data in case of Error in Replication Stream
        return;
    }

    // event points to Event Header (19 bytes)
    event += MYSQL_HEADER_LEN + 1;
    hdr->timestamp = gw_mysql_get_byte4(event);
    hdr->event_type = event[4];
    // TODO: add offsets in order to facilitate reading
    hdr->serverid = gw_mysql_get_byte4(event + 4 + 1);
    hdr->event_size = gw_mysql_get_byte4(event + 4 + 1 + 4);
    hdr->next_pos = gw_mysql_get_byte4(event + 4 + 1 + 4 + 4);
    hdr->flags = gw_mysql_get_byte2(event + 4 + 1 + 4 + 4 + 4);
}

/**
 * Reply data to client: Binlog events can be filtered
 *
 * @param pPacket    GWBUF with binlog event
 * @return           0 on error, >0 otherwise.
 */
int BinlogFilterSession::clientReply(GWBUF* pPacket)
{
    uint8_t* event = GWBUF_DATA(pPacket);
    uint32_t len = MYSQL_GET_PAYLOAD_LEN(event);
    REP_HEADER hdr;

    switch (m_state)
    {
    /**
     * TODO: remove this code when filters
     * will be able to pass some session informations
     * from "session->router_session"
     * m_crc will be thus set in routeQuery.
     */
    case COMMAND_MODE:
        if (m_reading_checksum)
        {
            getReplicationChecksum(pPacket);
            m_reading_checksum = false;
        }
        break;

    case BINLOG_MODE:
        if (!m_is_large)
        {
            // This binlog event contains:
            // OK byte
            // replication event header
            // event data, partial or total (if > 16 MBytes)
            extract_header(event, &hdr);

            // Check whether this event and next ones can be filtered
            checkEvent(pPacket, hdr);

            // Check whether this event is part of a large event being sent
            handlePackets(len, hdr);
        }
        else
        {
            // Handle data part of a large event:
            // Packet sequence is at offset 3
            handleEventData(len);
        }

        // If transaction events need to be skipped,
        // they are replaced by a RAND_EVENT event packet
        if (m_skip)
        {
            replaceEvent(&pPacket);
        }
        break;

    default:
        break;
    }

    // Send data
    return mxs::FilterSession::clientReply(pPacket);
}

/**
 * Close filter session
 */
void BinlogFilterSession::close()
{
}

/**
 * Check whether events in a transaction can be skipped.
 * The triggering event is TABLE_MAP_EVENT.
 *
 * Member variable m_skip is set accordingly to db/table match.
 *
 * @param buffer    The GWBUF with binlog event data
 * @param hdr       Reference to repliction event header
 * @return          True id TABLE_MAP_EVENT contains
 * db/table names to skip
 */
bool BinlogFilterSession::checkEvent(GWBUF* buffer,
                                     const REP_HEADER& hdr)
{
    mxb_assert(!m_is_large);

    uint8_t* event = GWBUF_DATA(buffer);
    uint8_t* body = event + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN;
    uint32_t body_size = hdr.event_size - BINLOG_EVENT_HDR_LEN;

    if (hdr.ok != 0)
    {
        // Error in binlog stream: no filter
        m_state = ERRORED;
        m_skip = false;
        MXS_INFO("Slave server %u received error in replication stream", m_serverid);
    }
    else
    {
        // Current event size is less than MYSQL_PACKET_LENGTH_MAX
        // or is the beginning of large event.
        switch (hdr.event_type)
        {
        case HEARTBEAT_EVENT:
            // Set m_skip = false anyway: cannot alter this event
            m_skip = false;
            break;

        case MARIADB10_GTID_EVENT:
            // New transaction, reset m_skip anyway
            m_skip = false;
            break;

        case MARIADB_ANNOTATE_ROWS_EVENT:
            // This even can come if replication mode is ROW and it comes before TABLE_MAP event. It has no
            // effect so it can be safely replicated.
            checkAnnotate(body, body_size);
            break;

        case TABLE_MAP_EVENT:
            // Check db/table and set m_skip accordingly
            skipDatabaseTable(body);
            break;

        case QUERY_EVENT:
            // Handle the SQL statement: DDL, DML, BEGIN, COMMIT
            // If statement is COMMIT, then continue with next case.
            if (checkStatement(body, body_size))
            {
                break;
            }

        case XID_EVENT:
            /** Note: This case is reached when event_type is
             * XID_EVENT or QUERY_EVENT with COMMIT
             *
             * reset m_skip if set and set next pos to 0
             */
            if (m_skip)
            {
                m_skip = false;
                /**
                 * Some events skipped.
                 * Set next pos to 0 instead of real one and new CRC32
                 */
                fixEvent(event + MYSQL_HEADER_LEN + 1, hdr.event_size);
            }
            break;

        default:
            // Other events can be skipped or not, depending on m_skip value
            break;
        }
    }

    // m_skip could be true or false
    return m_skip;
}

/**
 * Extract Dbname and Table name from TABLE_MAP event
 *
 * @param ptr       Pointer to event data
 * @param dbname    Pointer to pointer to database name
 * @param tblname   Pointer to pointer to table name
 */
static std::string inline extract_table_info(const uint8_t* ptr)
{
    /**
     * Extract dbname and table name from Table_map_log_event/TABLE_MAP_EVENT
     * https://dev.mysql.com/doc/internals/en/event-data-for-specific-event-types.html
     */

    int db_len_offset = 6 + 2;
    int db_len = ptr[db_len_offset];
    int tbl_len = ptr[db_len_offset + 1 + db_len + 1];      // DB is NULL terminated

    std::string dbname((char*)(ptr + db_len_offset + 1), db_len);
    std::string tblname((char*)(ptr + db_len_offset + 1 + db_len + 1 + 1), tbl_len);
    return dbname + "." + tblname;
}

static bool should_skip(const BinlogConfig& config, const std::string& str)
{
    bool skip = true;

    if (!config.match
        || pcre2_match(config.match, (PCRE2_SPTR)str.c_str(), PCRE2_ZERO_TERMINATED,
                       0, 0, config.md_match, NULL) >= 0)
    {
        if (!config.exclude
            || pcre2_match(config.exclude, (PCRE2_SPTR)str.c_str(), PCRE2_ZERO_TERMINATED, 0, 0,
                           config.md_exclude, NULL) == PCRE2_ERROR_NOMATCH)
        {
            skip = false;
        }
    }

    return skip;
}

static bool should_skip_query(const BinlogConfig& config, const std::string& sql, const std::string& db = "")
{
    uint32_t pktlen = sql.size() + 1;   // Payload and command byte
    GWBUF* buf = gwbuf_alloc(MYSQL_HEADER_LEN + pktlen);
    uint8_t* data = GWBUF_DATA(buf);

    data[0] = pktlen;
    data[1] = pktlen >> 8;
    data[2] = pktlen >> 16;
    data[3] = 0;
    data[4] = (uint8_t)MXS_COM_QUERY;
    strcpy((char*)&data[5], sql.c_str());

    bool rval = false;
    int n = 0;
    char** names = qc_get_table_names(buf, &n, true);

    for (int i = 0; i < n; i++)
    {
        std::string name = strchr(names[i], '.') ? names[i] : db + "." + names[i];

        if (should_skip(config, name))
        {
            rval = true;
            break;
        }
    }

    qc_free_table_names(names, n);
    gwbuf_free(buf);
    return rval;
}

/**
 * Check whether a db/table can be skipped based on configuration
 *
 * Member variable m_skip is set to true if the db/table names
 * need to be skipped.
 *
 * @param data    Binlog event data
 * @param hdr     Reference to replication event header
 */
void BinlogFilterSession::skipDatabaseTable(const uint8_t* data)
{
    std::string table = extract_table_info(data);
    m_skip = should_skip(m_filter.getConfig(), table);
    MXS_INFO("[%s] TABLE_MAP: %s", m_skip ? "SKIP" : "    ", table.c_str());
}

/**
 * Set CRC32 in the event buffer
 *
 * @param event         Pointer to event data
 * @param event_size    The event size
 */
static void event_set_crc32(uint8_t* event, uint32_t event_size)
{
    uint32_t chksum = crc32(0L, NULL, 0);
    chksum = crc32(chksum, event, event_size - 4);
    gw_mysql_set_byte4(event + event_size - 4, chksum);
}

/**
 * Set next pos to 0 and recalculate CRC32 in the event data
 *
 * @param event    Pointer to event data
 * @event_size     The event size
 */
void BinlogFilterSession::fixEvent(uint8_t* event, uint32_t event_size)
{
    // Set next pos to 0.
    // The next_pos offset is the 13th byte in replication event header 19 bytes
    // +  4 (time) + 1 (type) + 4 (server_id) + 4 (event_size)
    gw_mysql_set_byte4(event + 4 + 1 + 4 + 4, 0);

    // Set CRC32 in the new event
    if (m_crc)
    {
        event_set_crc32(event, event_size);
    }
}

/**
 * Replace data in the current packet with binlog event
 * with a RAND_EVENT
 * No memory allocation is done if current packet size
 * is bigger than (MYSQL_HEADER_LEN + 1 + RAND_EVENT)
 *
 * @param pPacket    The GWBUF with event data
 */
void BinlogFilterSession::replaceEvent(GWBUF** ppPacket)
{

    uint32_t buf_len = gwbuf_length(*ppPacket);
    uint32_t orig_event_type = 0;

    // If size < BINLOG_EVENT_HDR_LEN + crc32 add rand_event to buff contiguos
    mxb_assert(m_skip == true);

    /**
     * RAND_EVENT is:
     * - 19 bytes header
     * - 8 bytes first seed
     * - 8 bytes second seed
     * - 4 bytes CRC32 (if required)
     */
    // size of rand_event (header + 16 bytes + CRC32)
    uint32_t new_event_size = BINLOG_EVENT_HDR_LEN + 16;
    new_event_size += m_crc ? 4 : 0;

    /**
     * If buf_len < (network packet len with RAND_EVENT),
     * then create a new complete rand_event network packet.
     *
     * This might happen in case of:
     * - any "small" binlog event
     * or
     * - remaining bytes of a large event transmission
     */
    if (buf_len < (MYSQL_HEADER_LEN + 1 + new_event_size))
    {
        GWBUF* pTmpbuf;
        pTmpbuf = gwbuf_alloc(MYSQL_HEADER_LEN + 1   \
                              + (new_event_size - buf_len));
        // Append new buff to current one
        *ppPacket = gwbuf_append(*ppPacket, pTmpbuf);
        // Make current buff contiguous
        *ppPacket = gwbuf_make_contiguous(*ppPacket);
    }

    // point to data
    uint8_t* ptr = GWBUF_DATA(*ppPacket);

    /**
     * Replication protocol:
     * 1) set 3 bytes for packet size
     * 2) the packet sequence is not touched!!!
     * 3) set 1 byte OK indicator
     */
    // Set New Packet size: new event_size + 1 byte replication status
    gw_mysql_set_byte3(ptr, new_event_size + 1);

    // Force OK flag after 3 bytes packet size
    ptr[MYSQL_HEADER_LEN] = 0;

    // Now modify the event header fields (19 bytes)
    // 4 bytes timestamp
    // 1 byte event type
    // 4 bytes server_id
    // 4 bytes event_size
    // 4 bytes next_pos
    // 2 bytes flags
    int event_header_offset = MYSQL_HEADER_LEN + 1;

    // Force Set timestamp to 0 [4 bytes]
    gw_mysql_set_byte4(ptr + event_header_offset, 0);
    // Point to event_type
    event_header_offset += 4;

    // Save current event type only for standard packets
    if (!m_is_large)
    {
        orig_event_type = ptr[event_header_offset];
    }
    // Set NEW event_type [1 byte]
    ptr[event_header_offset] = RAND_EVENT;
    // Point to server_id
    event_header_offset++;

    // Force Set server_id to 0 [4 bytes]
    gw_mysql_set_byte4(ptr + event_header_offset, 0);
    // Point to event_size
    event_header_offset += 4;

    // Set event_event size [4 bytes]
    gw_mysql_set_byte4(ptr + event_header_offset, new_event_size);
    // Next pos [4 bytes] is set by fixEvent(), go ahead!!!
    event_header_offset += 4;
    // Point to event_flags,
    event_header_offset += 4;

    // Set LOG_EVENT_SKIP_REPLICATION_F flags [2 bytes]
    gw_mysql_set_byte2(ptr + event_header_offset,
                       LOG_EVENT_SKIP_REPLICATION_F);

    // Point to RAND_EVENT content now
    event_header_offset += 2;

    /**
     * We set now the value for the first and second seed
     * as input packet size and event_size
     * event_size is 0 for all packets belonging to a large event
     */
    // Set first seed as the input packet size (4 bytes only)
    gw_mysql_set_byte4(ptr + event_header_offset,
                       buf_len - (MYSQL_HEADER_LEN + 1));
    event_header_offset += 4;
    // Set 0 for next 4 bytes of first seed
    gw_mysql_set_byte4(ptr + event_header_offset, 0);

    // Point to second seed
    event_header_offset += 4;
    // Set second seed as the input event type (4 bytes only)
    gw_mysql_set_byte4(ptr + event_header_offset, orig_event_type);
    event_header_offset += 4;
    // Set 0 for next 4 bytes of second seed
    gw_mysql_set_byte4(ptr + event_header_offset, 0);

    /**
     * Now we remove the useless bytes in the buffer
     * in case of inout packet is bigger than RAND_EVENT packet
     */
    if (gwbuf_length(*ppPacket) > (new_event_size + 1 + MYSQL_HEADER_LEN))
    {
        uint32_t remove_bytes = gwbuf_length(*ppPacket)   \
            - (new_event_size + 1 + MYSQL_HEADER_LEN);
        *ppPacket = gwbuf_rtrim(*ppPacket, remove_bytes);
    }

    // Fix Event Next pos = 0 and set new CRC32
    fixEvent(ptr + MYSQL_HEADER_LEN + 1, new_event_size);
}

/**
 * Extract the value of a specific columnr from a buffer
 * TODO: also in use in binlogrouter code, to be moved
 * in a common place
 *
 * @param buf    GWBUF with a resultset
 * @param col    The column number to extract
 * @return       The value of the column
 */
static char* extract_column(GWBUF* buf, int col)
{
    uint8_t* ptr;
    int len, ncol, collen;
    char* rval;

    if (buf == NULL)
    {
        return NULL;
    }

    ptr = (uint8_t*)GWBUF_DATA(buf);
    /* First packet should be the column count */
    len = gw_mysql_get_byte3(ptr);
    ptr += 3;
    if (*ptr != 1)      // Check sequence number is 1
    {
        return NULL;
    }
    ptr++;
    ncol = *ptr++;
    if (ncol < col)     // Not that many column in result
    {
        return NULL;
    }

    // Now ptr points at the column definition
    while (ncol-- > 0)
    {
        len = gw_mysql_get_byte3(ptr);
        ptr += 4;   // Skip to payload
        ptr += len; // Skip over payload
    }
    // Now we should have an EOF packet
    len = gw_mysql_get_byte3(ptr);
    ptr += 4;       // Skip to payload
    if (*ptr != 0xfe)
    {
        return NULL;
    }
    ptr += len;

    // Finally we have reached the row
    len = gw_mysql_get_byte3(ptr);
    ptr += 4;

    /**
     * The first EOF packet signals the start of the resultset rows
     * and the second  EOF packet signals the end of the result set.
     * If the resultset contains a second EOF packet right after the first one,
     * the result set is empty and contains no rows.
     */
    if (len == 5 && *ptr == 0xfe)
    {
        return NULL;
    }

    while (--col > 0)
    {
        collen = *ptr++;
        ptr += collen;
    }
    collen = *ptr++;
    if ((rval = (char*)MXS_MALLOC(collen + 1)) == NULL)
    {
        return NULL;
    }
    memcpy(rval, ptr, collen);
    rval[collen] = 0;       // NULL terminate

    return rval;
}

/**
 * Abort filter operation
 *
 * @param pPacket    The buffer to free
 */
void BinlogFilterSession::filterError(GWBUF* pPacket)
{
    /* Abort client connection on copy failure */
    m_state = ERRORED;
    poll_fake_hangup_event(m_pSession->client_dcb);
    gwbuf_free(pPacket);
}

/**
 * Get replication checksum value from a GWBUF resultset
 * Sets the member variable m_crc to true in case of found
 * CRC32 value.
 *
 * @param pPacket    The resultset
 * @return           False on error
 */
void BinlogFilterSession::getReplicationChecksum(GWBUF* pPacket)
{
    if (char* crc = extract_column(pPacket, 1))
    {
        if (strcasestr(crc, "CRC32"))
        {
            m_crc = true;
        }

        MXS_FREE(crc);
    }
}

/**
 * Handles the event size and sets member variables
 * 'm_is_large' and 'm_large_left'
 *
 * If received data len is MYSQL_PACKET_LENGTH_MAX
 * then the beginning of a large event receiving is set.
 *
 * Also remaininf data are set
 *
 * @param len    The binlog event paylod len
 * @param hdr    The reference to binlog event header
 */
void BinlogFilterSession::handlePackets(uint32_t len, const REP_HEADER& hdr)
{
    // Mark the beginning of a lrage event transmission
    if (len == MYSQL_PACKET_LENGTH_MAX)
    {
        // Mark the beginning of a large event transmission
        m_is_large = true;

        // Set remaining data to receive accordingy to hdr.event_size
        m_large_left = hdr.event_size - (MYSQL_PACKET_LENGTH_MAX - 1);
    }
}

/**
 * Process received data size of a large event trasmission
 * Incoming data don't carry the OK byte and event header
 *
 * This sets member variables
 * 'm_is_large' and 'm_large_left'
 *
 * @param len      Received payload size
 * @param seqno    Packet seqno, logging only
 */
void BinlogFilterSession::handleEventData(uint32_t len)
{
    /**
     * Received bytes are part of a large event transmission
     * Network packet has 4 bytes header +  data:
     * no ok byte, no event header!
     */

    // Decrement remaining bytes
    m_large_left -= len;

    // Mark the end of a large event transmission
    if (m_large_left == 0)
    {
        m_is_large = false;
    }
}

/**
 * Check wether the config for db/table filtering is found in
 * the SQL statement inside QUERY_EVENT binlog event.
 * Note: COMMIT is an exception here, routine returns false
 * and the called will set m_skip to the right value.
 *
 * @see https://mariadb.com/kb/en/library/query_event/
 *
 * In case of config match the member variable m_skip is set to true.
 * With empty config it returns true and skipping is always false.
 *
 *
 * @param event         The QUERY_EVENT binlog event
 * @param event_size    The binlog event size
 * @return              False for COMMIT, true otherwise
 */
bool BinlogFilterSession::checkStatement(const uint8_t* event, const uint32_t event_size)
{
    int db_name_len = event[4 + 4];
    int var_block_len_offset = 4 + 4 + 1 + 2;
    int var_block_len = gw_mysql_get_byte2(event + var_block_len_offset);
    int static_size = 4 + 4 + 1 + 2 + 2;
    int statement_len = event_size - static_size - var_block_len - db_name_len - 1 - (m_crc ? 4 : 0);

    std::string db((char*)event + static_size + var_block_len, db_name_len);
    std::string sql((char*)event + static_size + var_block_len + db_name_len + 1, statement_len);
    std::string lower_sql;
    std::transform(sql.begin(), sql.end(), std::back_inserter(lower_sql), tolower);

    if (lower_sql.find("commit") != std::string::npos)
    {
        return false;
    }

    m_skip = should_skip_query(m_filter.getConfig(), sql, db);
    MXS_INFO("[%s] (%s) %s", m_skip ? "SKIP" : "    ", db.c_str(), sql.c_str());

    return true;
}

void BinlogFilterSession::checkAnnotate(const uint8_t* event, const uint32_t event_size)
{
    std::string sql((char*)event, event_size - (m_crc ? 4 : 0));
    m_skip = should_skip_query(m_filter.getConfig(), sql);
    MXS_INFO("[%s] Annotate: %s", m_skip ? "SKIP" : "    ", sql.c_str());
}
