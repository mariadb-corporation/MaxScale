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

#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>
#include <maxscale/poll.h>
#include "binlogfilter.hh"
#include "binlogfiltersession.hh"
#include <zlib.h>
#include <inttypes.h>

// New packet which replaces the skipped events has 0 payload
#define NEW_PACKET_PAYLOD BINLOG_EVENT_HDR_LEN

static char* extract_column(GWBUF* buf, int col);
static void  event_set_crc32(uint8_t* event, uint32_t event_size);
static void  extract_header(register const uint8_t* event,
                            register REP_HEADER* hdr);
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
    , m_serverid(0)
    , m_state(pFilter->is_active() ? COMMAND_MODE : INACTIVE)
    , m_skip(false)
    , m_crc(false)
    , m_large_left(0)
    , m_is_large(0)
    , m_sql_query(NULL)
{
    MXS_NOTICE("Filter [%s] is %s",
               MXS_MODULE_NAME,
               m_filter.getConfig().active ? "enabled" : "disabled");
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
    if (m_state != INACTIVE)
    {
        uint8_t* data = GWBUF_DATA(pPacket);

        switch (MYSQL_GET_COMMAND(data))
        {
        case MXS_COM_REGISTER_SLAVE:
            // Connected client is registering as Slave Server
            m_serverid = gw_mysql_get_byte4(data + MYSQL_HEADER_LEN + 1);
            MXS_INFO("Client is registering as "
                     "Slave server with ID %" PRIu32 "",
                     m_serverid);
            break;

        case MXS_COM_BINLOG_DUMP:
            // Connected Slave server is waiting for binlog events
            m_state = BINLOG_MODE;
            MXS_INFO("Slave server %" PRIu32 " is waiting for binlog events.",
                     m_serverid);
            break;

        default:
            // Connected client is using SQL mode
            m_state = COMMAND_MODE;
            /**
             * TODO: remove this code when filters
             * will be able to pass some session informations
             * from "session->router_session"
             * The calling session (ROUTER_SLAVE from blr.h) is not accessible.
             *
             * With new maxscale filter features, simply add:
             * in 'case COM_REGISTER_SLAVE:'
             * m_crc = (SOME_STRUCT* )get_calling_session_info()->crc;
             *
             */
            if (strcasestr((char*)data + MYSQL_HEADER_LEN + 1,
                           "SELECT @master_binlog_checksum") != NULL)
            {
                if ((m_sql_query = gwbuf_clone(pPacket)) == NULL)
                {
                    filterError(pPacket);
                    return 0;
                }
            }
            break;
        }
    }

    // Route input data
    return mxs::FilterSession::routeQuery(pPacket);
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
        if (m_sql_query != NULL
            && !getReplicationChecksum(pPacket))
        {
            // Free buffer and close client connection
            filterError(pPacket);
            return 0;
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
            handleEventData(len, event[3]);
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
    if (m_state == BINLOG_MODE)
    {
        MXS_DEBUG("Slave server %" PRIu32 ": replication stopped.",
                  m_serverid);
    }
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

    MXS_INFO("Binlog Event, Header: pkt #%d, "
             "serverId %" PRIu32 ", event_type [%d], "
                                 "flags %d, event_size %" PRIu32 ", next_pos %" PRIu32 ", "
                                                                                       "packet size %" PRIu32 "",
             hdr->seqno,
             hdr->serverid,
             hdr->event_type,
             hdr->flags,
             hdr->event_size,
             hdr->next_pos,
             hdr->payload_len);
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

    if (hdr.ok != 0)
    {
        // Error in binlog stream: no filter
        m_state = ERRORED;
        m_skip = false;
        MXS_ERROR("Slave server %" PRIu32 " received error "
                                          "in replication stream, packet #%u",
                  m_serverid,
                  event[3]);
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
            // This even can come if replication mode is ROW
            // and it comes before TABLE_MAP event
            // m_skip can be set to true/false
            checkAnnotate(event, hdr.event_size);
            break;

        case TABLE_MAP_EVENT:
            // Check db/table and set m_skip accordingly
            skipDatabaseTable(event, hdr);
            break;

        case QUERY_EVENT:
            // Handle the SQL statement: DDL, DML, BEGIN, COMMIT
            // If statement is COMMIT, then continue with next case.
            if (checkStatement(event, hdr.event_size))
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

                MXS_INFO("Skipped events: Setting next_pos = 0 in %s",
                         event[4] == XID_EVENT ?
                         "XID_EVENT" : "COMMIT");
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
static void inline extract_table_info(const uint8_t* ptr,
                                      std::string&   dbname,
                                      std::string&   tblname)
{
    /**
     * Extract dbname and table name from Table_map_log_event/TABLE_MAP_EVENT
     * https://dev.mysql.com/doc/internals/en/event-data-for-specific-event-types.html
     */

    int db_len_offset = MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 6 + 2;
    int db_len = ptr[db_len_offset];
    int tbl_len = ptr[db_len_offset + 1 + db_len + 1];      // DB is NULL terminated

    dbname.assign((char*)(ptr + db_len_offset + 1), db_len);
    tblname.assign((char*)(ptr + db_len_offset + 1 + db_len + 1 + 1), tbl_len);
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
void BinlogFilterSession::skipDatabaseTable(const uint8_t* data,
                                            const REP_HEADER& hdr)
{
    // Check for TABLE_MAP event:
    // Note: Each time this event is seen the m_skip is overwritten
    if (hdr.event_type == TABLE_MAP_EVENT)
    {
        // Get filter configuration
        const BinlogConfig& fConfig = m_filter.getConfig();

        // Set m_skip to false and return with empty config values
        if (fConfig.dbname.empty() && fConfig.table.empty())
        {
            m_skip = false;
            return;
        }

        std::string db;
        std::string table;

        // Get db/table from event data
        extract_table_info(data, db, table);

        /**
         * Check db/table match with configuration
         *
         * Note: currently only one dbname and table name in config.
         *
         * (1.1) config db is set and config table is not set:
         *    - if current db matches m_skip = true (say "db.*")
         * (1.2) config db is set and config table is set
         *    - both db and table should match for m_skip = true (db.table)
         * (2.1) No db match or config table not set:
         *    - m_skip set to false;
         * (2.2) config db is not set:
         *    - if config table is set and matches: m_skip = true (say *.table)
         */
        if (!fConfig.dbname.empty()
            && db == fConfig.dbname)
        {
            // Config Db name matches: 1.1 OR 1.2
            m_skip = fConfig.table.empty()      /* 1.1 */
                || table == fConfig.table;      /* 1.2 */
        }
        else
        {
            // No Db name match or db config is not set: 2.1 OR 2.2
            // Check only table name if set, the dbname doesn't matter
            m_skip = (!fConfig.dbname.empty() || fConfig.table.empty())
                ?       // (2.1)
                false
                :       // (2.2)
                (fConfig.dbname.empty()
                 && table == fConfig.table) == 0;
        }

        MXS_INFO("TABLE_MAP_EVENT: Dbname is [%s], Table is [%s], Skip [%s]",
                 db.c_str(),
                 table.c_str(),
                 m_skip ? "Yes" : "No");
    }
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

    // Log the replaced event
    // Now point to event_size offset
    event_header_offset = MYSQL_HEADER_LEN + 1 + 4 + 1 + 4;
    MXS_DEBUG("Filtered event #%d, "
              "ok %d, type %d, flags %d, size %d, next_pos %d, packet_size %d\n",
              ptr[3],
              ptr[4],
              RAND_EVENT,
              gw_mysql_get_byte2(ptr + event_header_offset + 4 + 4),
              gw_mysql_get_byte4(ptr + event_header_offset),
              gw_mysql_get_byte4(ptr + event_header_offset + 4),
              gw_mysql_get_byte3(ptr));
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
 * Set CRC32 in the event buffer
 *
 * @param event         Pointer to event data
 * @param event_size    The event size
 */
static void event_set_crc32(uint8_t* event, uint32_t event_size)
{
    uint32_t chksum = crc32(0L, NULL, 0);
    chksum = crc32(chksum,
                   event,
                   event_size - 4);
    gw_mysql_set_byte4(event + event_size - 4, chksum);
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
bool BinlogFilterSession::getReplicationChecksum(GWBUF* pPacket)
{
    char* crc;
    if ((crc = extract_column(pPacket, 1)) == NULL)
    {
        return false;
    }

    if (strcasestr(crc, "CRC32"))
    {
        m_crc = true;
    }

    MXS_FREE(crc);
    gwbuf_free(m_sql_query);
    m_sql_query = NULL;

    return true;
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

        // Log large event receiving
        MXS_DEBUG("Large event start: size %" PRIu32 ", "
                                                     "remaining %" PRIu32 " bytes",
                  hdr.event_size,
                  m_large_left);
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
void BinlogFilterSession::handleEventData(uint32_t len,
                                          const uint8_t seqno)
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

    MXS_INFO("Binlog Event, data_only: pkt #%d, received %" PRIu32 ", "
                                                                   "remaining %" PRIu32 " bytes\n",
             seqno,
             len,
             m_large_left);
}

/**
 * The routines sets the DB.TABLE for matching in QUERY_EVENT
 *
 * The db_table can be set to empty value in case the default db is present
 * but no config is set.
 *
 * If a default db is present the match is only for table name
 *
 * @param db_table    Reference to db.table string to fill
 * @param config      Reference filter config
 * @param use_db      The event comes with default db (caused by USE db stmt)
 */
void matchDbTableSQL(std::string& db_table,
                     const BinlogConfig& config,
                     bool use_db)
{

    /**
     * Db name and Table name match in a QUERY_EVENT event data
     *
     * Note:
     * - db_table comes as an empty string ("")
     * - with default db only table if set for match even if empty
     */
    if (!use_db && !config.dbname.empty())
    {
        // Set [FULL] TABLE NAME for match
        // default db is not set and config db is set:
        // use "db."
        db_table = config.dbname;
        db_table += ".";
        // Note: table name is added at the end
    }

    // Add the config table for matching:
    db_table += config.table;

    /**
     * db_table can be now:
     * A) "" which disables the matching in the caller
     * B) if use_db is not set and config db is set:
     *    "cfg_db.cfg_table" or "cfg_db." (say db.*)
     * C) if use_db is true or config db is not set:
     *    "cfg_table" (say *.table)
     */
}

/**
 * Check wether the config for db/table filtering is found in
 * the SQL statement inside QUERY_EVENT binlog event.
 * Note: COMMIT is an exception here, routine returns false
 * and the called will set m_skip to the right value.
 *
 * In case of config match the member variable m_skip is set to true.
 * With empty config it returns true and skipping is always false.
 *
 *
 * @param event         The QUERY_EVENT binlog event
 * @param event_size    The binlog event size
 * @return              False for COMMIT, true otherwise
 */
bool BinlogFilterSession::checkStatement(const uint8_t* event,
                                         const uint32_t event_size)
{
    // Get filter configuration
    const BinlogConfig& fConfig = m_filter.getConfig();

    // Set m_skip to false and return with empty config values
    if (fConfig.dbname.empty() && fConfig.table.empty())
    {
        m_skip = false;
        return true;
    }

    /**
     * Handling of Query_log_event/QUERY_EVENT is based on this doc:
     * https://dev.mysql.com/doc/internals/en/event-data-for-specific-event-types.html
     */
    int db_name_len, var_block_len, statement_len, var_block_len_offset, var_block_end;
    db_name_len = event[MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4];
    var_block_len_offset = MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 4 + 4 + 1 + 2;
    var_block_len = gw_mysql_get_byte2(event + var_block_len_offset);
    var_block_end = var_block_len_offset + 2;

    // SQL statement len
    statement_len = (MYSQL_HEADER_LEN + 1 + event_size)                  \
        - (var_block_end + var_block_len + db_name_len + 1)   \
        - (m_crc ? 4 : 0);

    // Set dbname (NULL terminated in the packet)
    std::string db_name = (char*)event + var_block_end + var_block_len;

    // Set SQL statement, NULL is NOT present in the packet !
    std::string statement_sql((char*)event + var_block_end   \
                              + var_block_len + db_name_len + 1,
                              statement_len);

    // Check for BEGIN (it comes for START TRANSACTION too)
    if (statement_sql.compare(0, 5, "BEGIN") == 0)
    {
        // New transaction, reset m_skip anyway
        m_skip = false;
        return true;
    }

    // Check for COMMIT
    if (statement_sql.compare(0, 6, "COMMIT") == 0)
    {
        // End of transaction, m_skip handled by the caller
        return false;
    }

    // Default DB check with config
    if (!db_name_len || !checkUseDB(db_name, fConfig))
    {
        std::string db_table("");

        // The Default db is not present in the event.
        // Create the TABLE or DB.TABLE or DB. match
        matchDbTableSQL(db_table, fConfig, db_name_len != 0);

        // Match non empty db/table in the SQL statement
        m_skip = !db_table.empty()
            && statement_sql.find(db_table) != std::string::npos;

        MXS_INFO("QUERY_EVENT: config DB.TABLE is [%s], Skip [%s]",
                 db_table.c_str(),
                 m_skip ? "Yes" : "No");
    }
    return true;
}

/**
 * Check whether default DB config match is enough
 * for event skipping
 *
 * The routines sets member variable m_skip to true if:
 * - config db is set, db name matches and no config table
 * Return is true in the above case or if dbname doesn't match.
 *
 * @param db_name    Reference to default db in QUERY_EVENT
 * @param config     Reference to filter config
 * @return           True if match can be stopped, false otherwise.
 */
bool BinlogFilterSession::checkUseDB(const std::string& db_name,
                                     const BinlogConfig& config)
{
    bool ret = false;

    // Check match with configuration and set m_skip
    if ((!config.dbname.empty()
         && db_name.compare(config.dbname) == 0))
    {
        // Config db exists and dbname in QUERY_EVENT matches
        // and config table is not set: set m_skip = true
        m_skip = config.table.empty();

        if (m_skip)
        {
            // Return: no need to match the config table (say db.*)
            ret = true;
        }
    }
    else
    {
        if (!config.dbname.empty())
        {
            // No db match
            m_skip = false;
            // Return, no other checks
            ret = true;
        }
    }

    MXS_INFO("QUERY_EVENT: Default DB is [%s], config [%s], Skip [%s], "
             "Stop matching [%s]",
             db_name.c_str(),
             config.dbname.c_str(),
             m_skip ? "Yes" : "No",
             ret ? "Yes" : "No");

    // Return true stops the matching, false keeps it.
    return ret;
}

/**
 * Checks whether an ANNOTATE_ROWS event
 * can be skipped, based on config matching against
 * SQL statement (which start right after the even header)
 *
 * In case of match the member var m_skip is set to true
 *
 * @param event         The QUERY_EVENT binlog event
 * @param event_size    The binlog event size
 */
void BinlogFilterSession::checkAnnotate(const uint8_t* event,
                                        const uint32_t event_size)
{
    // Get filter configuration
    const BinlogConfig& fConfig = m_filter.getConfig();

    // Set m_skip to false and return with empty config values
    if (fConfig.dbname.empty() && fConfig.table.empty())
    {
        m_skip = false;
        return;
    }

    // SQL statement len
    int statement_len = event_size   \
        - (MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN)   \
        - (m_crc ? 4 : 0);

    std::string db_table("");
    std::string statement_sql((char*)event   \
                              + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN,
                              statement_len);

    // Create the TABLE or DB.TABLE or DB. match
    matchDbTableSQL(db_table, fConfig, false);

    // Match non empty db/table in the SQL statement
    m_skip = !db_table.empty()
        && statement_sql.find(db_table) != std::string::npos;

    MXS_INFO("ANNOTATE_ROWS_EVENT: config DB.TABLE is [%s], Skip [%s]",
             db_table.c_str(),
             m_skip ? "Yes" : "No");
}
