/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
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
#define MXB_MODULE_NAME "binlogfilter"

#include <zlib.h>
#include <inttypes.h>
#include <algorithm>
#include <optional>

#include <mysqld_error.h>
#include <maxbase/regex.hh>
#include <maxbase/alloc.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/parser.hh>
#include <maxscale/session.hh>

#include "binlogfilter.hh"
#include "binlogfiltersession.hh"

/**
 * BinlogFilterSession constructor
 *
 * @param pSession    The calling routing/filter session
 * @param pFilter     Pointer to filter configuration
 */
BinlogFilterSession::BinlogFilterSession(MXS_SESSION* pSession, SERVICE* pService,
                                         const BinlogFilter* pFilter)
    : mxs::FilterSession(pSession, pService)
    , m_filter(*pFilter)
    , m_config(pFilter->getConfig())
{
}

/**
 * BinlogFilterSession destructor
 */
BinlogFilterSession::~BinlogFilterSession()
{
}

static bool is_matching_query(std::string_view haystack, std::string_view needle)
{
    return mxb::sv_strcasestr(haystack, needle) != std::string_view::npos;
}

static bool is_master_binlog_checksum(std::string_view sql)
{
    return is_matching_query(sql, "SELECT @master_binlog_checksum");
}

static bool is_using_gtid(std::string_view sql)
{
    return is_matching_query(sql, "SET @slave_connect_state=");
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
 * @param packet     The inout data from client
 * @return           False on errors, true otherwise.
 */
bool BinlogFilterSession::routeQuery(GWBUF&& packet)
{
    uint8_t* data = packet.data();

    switch (mariadb::get_command(data))
    {
    case MXS_COM_REGISTER_SLAVE:
        // Connected client is registering as Slave Server
        m_serverid = mariadb::get_byte4(data + MYSQL_HEADER_LEN + 1);
        MXB_INFO("Client is registering as Replica server with ID %u", m_serverid);
        break;

    case MXS_COM_BINLOG_DUMP:
        // Connected Slave server is waiting for binlog events
        m_state = BINLOG_MODE;
        MXB_INFO("Replica server %u is waiting for binlog events.", m_serverid);

        if (!m_is_gtid && m_config.rewrite_src)
        {
            std::ostringstream ss;
            ss << "GTID replication is required when '"
               << REWRITE_SRC << "' and '" << REWRITE_DEST << "' are used";
            mxs::ReplyRoute rr;
            GWBUF error = mariadb::create_error_packet(
                1, ER_MASTER_FATAL_ERROR_READING_BINLOG, "HY000", ss.str().c_str());
            mxs::Reply reply = protocol().make_reply(error);
            mxs::FilterSession::clientReply(std::move(error), rr, reply);
            return 0;
        }
        break;

    case MXS_COM_QUERY:
        // Connected client is using SQL mode
        m_state = COMMAND_MODE;
        m_reading_checksum = is_master_binlog_checksum(get_sql(packet));
        packet.set_type(GWBUF::TYPE_COLLECT_RESULT);

        if (is_using_gtid(get_sql(packet)))
        {
            m_is_gtid = true;
        }
        break;

    default:
        // Not something we care about, just pass it through
        break;
    }

    // Route input data
    return mxs::FilterSession::routeQuery(std::move(packet));
}

/**
 * Extract binlog replication header from event data
 *
 * @param event    The replication event
 * @param hdr      Pointer to replication header to fill
 */
static void extract_header(const uint8_t* event, REP_HEADER* hdr)
{
    hdr->seqno = event[3];
    hdr->payload_len = mariadb::get_byte3(event);
    hdr->ok = event[MYSQL_HEADER_LEN];
    if (hdr->ok != 0)
    {
        // Don't parse data in case of Error in Replication Stream
        return;
    }

    // event points to Event Header (19 bytes)
    event += MYSQL_HEADER_LEN + 1;
    hdr->timestamp = mariadb::get_byte4(event);
    hdr->event_type = event[4];
    event += 4 + 1;
    hdr->serverid = mariadb::consume_byte4(&event);
    hdr->event_size = mariadb::consume_byte4(&event);
    hdr->next_pos = mariadb::consume_byte4(&event);
    hdr->flags = mariadb::get_byte2(event);
}

/**
 * Reply data to client: Binlog events can be filtered
 *
 * @param packet     GWBUF with binlog event
 * @return           False on error, true otherwise.
 */
bool BinlogFilterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    uint8_t* event = packet.data();
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
            getReplicationChecksum(packet);
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
            checkEvent(packet, hdr);

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
            replaceEvent(packet, hdr);
        }
        break;

    default:
        break;
    }

    // Send data
    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

/**
 * Check whether events in a transaction can be skipped.
 * The triggering event is TABLE_MAP_EVENT.
 *
 * Member variable m_skip is set accordingly to db/table match.
 *
 * @param buffer    The GWBUF with binlog event data
 * @param hdr       Reference to replication event header
 * @return          True id TABLE_MAP_EVENT contains
 * db/table names to skip
 */
bool BinlogFilterSession::checkEvent(GWBUF& buffer, const REP_HEADER& hdr)
{
    mxb_assert(!m_is_large);

    uint8_t* event = buffer.data();
    uint8_t* body = event + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN;
    uint32_t body_size = hdr.event_size - BINLOG_EVENT_HDR_LEN;

    if (hdr.ok != 0)
    {
        // Error in binlog stream: no filter
        m_state = ERRORED;
        m_skip = false;
        MXB_INFO("Replica server %u received error in replication stream", m_serverid);
    }
    else
    {
        int extra_bytes = 0;
        // Current event size is less than MYSQL_PACKET_LENGTH_MAX
        // or is the beginning of large event.
        switch (hdr.event_type)
        {
        case HEARTBEAT_EVENT:
            {
                // The slave server that receives this event will compare the binlog name and the next
                // position of the heartbeat event to its own. The binlog name check will pass but the
                // position check will fail if the slave's relay log is ahead of the master's binlog. Since
                // the slave only checks if it's ahead of the master, by setting the next event position to a
                // fake value we bypass this. This is safe as heartbeat events are never written into the
                // relay log and thus do not affect replication.
                REP_HEADER hdr_copy = hdr;
                hdr_copy.next_pos = 0xffffffff;
                fixEvent(buffer.data() + MYSQL_HEADER_LEN + 1,
                         buffer.length() - MYSQL_HEADER_LEN - 1,
                         hdr_copy);

                // Set m_skip = false anyway: cannot alter this event
                m_skip = false;
            }
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

        case EXECUTE_LOAD_QUERY_EVENT:
            // EXECUTE_LOAD_QUERY_EVENT has an extra 13 bytes of data (file ID, file offset etc.)
            extra_bytes = 4 + 4 + 4 + 1;
            [[fallthrough]];

        case QUERY_EVENT:
            // Handle the SQL statement: DDL, DML, BEGIN, COMMIT
            checkStatement(buffer, hdr, extra_bytes);

            // checkStatement can reallocate the buffer in case the size changes: use fresh pointers
            fixEvent(buffer.data() + MYSQL_HEADER_LEN + 1,
                     buffer.length() - MYSQL_HEADER_LEN - 1,
                     hdr);
            break;

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
                fixEvent(event + MYSQL_HEADER_LEN + 1, hdr.event_size, hdr);
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

static bool should_skip(const BinlogConfig::Values& config, const std::string& str)
{
    return (config.match && !config.match.match(str)) || (config.exclude && config.exclude.match(str));
}

static bool should_skip_query(const mxs::Parser& parser,
                              const BinlogConfig::Values& config,
                              const std::string& sql,
                              const std::string& db = "")
{
    GWBUF buf = mariadb::create_query(sql);
    bool rval = false;
    std::vector<mxs::Parser::TableName> tables = parser.get_table_names(buf);

    if (parser.get_trx_type_mask(buf) == 0)
    {
        // Not a transaction management related command
        for (const auto& t : tables)
        {
            std::string name = mxb::cat(!t.db.empty() ? t.db : db, ".", t.table);

            if (should_skip(config, name))
            {
                rval = true;
                break;
            }
        }

        // Also check for the default database in case the query has no tables in it. The dot at the end is
        // required to distinct database names from table names.
        if (tables.empty())
        {
            rval = should_skip(config, db + '.');
        }
    }

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
    m_skip = should_skip(m_config, table);
    MXB_INFO("[%s] TABLE_MAP: %s", m_skip ? "SKIP" : "    ", table.c_str());
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
    mariadb::set_byte4(event + event_size - 4, chksum);
}

/**
 * Set next pos to 0 and recalculate CRC32 in the event data
 *
 * @param event    Pointer to event data
 * @event_size     The event size
 */
void BinlogFilterSession::fixEvent(uint8_t* event, uint32_t event_size, const REP_HEADER& hdr)
{
    // Update event length in case we changed it
    mariadb::set_byte4(event + 4 + 1 + 4, event_size);

    // Set next pos to 0.
    // The next_pos offset is the 13th byte in replication event header 19 bytes
    // +  4 (time) + 1 (type) + 4 (server_id) + 4 (event_size)
    mariadb::set_byte4(event + 4 + 1 + 4 + 4, hdr.next_pos);

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
 * @param packet The GWBUF with event data
 */
void BinlogFilterSession::replaceEvent(GWBUF& packet, const REP_HEADER& hdr)
{
    if (hdr.event_type == QUERY_EVENT)
    {
        uint8_t* event = packet.data() + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN;
        uint32_t event_size = hdr.event_size - BINLOG_EVENT_HDR_LEN;

        int db_name_len = event[4 + 4];
        int var_block_len_offset = 4 + 4 + 1 + 2;
        int var_block_len = mariadb::get_byte2(event + var_block_len_offset);
        int static_size = 4 + 4 + 1 + 2 + 2;
        int statement_len = event_size - static_size - var_block_len - db_name_len - 1 - (m_crc ? 4 : 0);
        uint8_t* sql_start = event + static_size + var_block_len + db_name_len + 1;
        memset(sql_start, ' ', statement_len);

        // Add a comment if we have enough space and use that to display that the event was ignored. This will
        // be helpful for verifying that events are filtered and for debugging if any problems arise.
        if (statement_len >= 3)
        {
            const char msg[] = "-- Event ignored";
            memcpy(sql_start, msg, std::min(sizeof(msg) - 1, (size_t)statement_len));
        }

        return;
    }

    uint32_t buf_len = packet.length();
    uint32_t orig_event_type = 0;

    // If size < BINLOG_EVENT_HDR_LEN + crc32 add rand_event to buff contiguous
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
        size_t sz = MYSQL_HEADER_LEN + 1 + (new_event_size - buf_len);
        packet.prepare_to_write(sz);
        packet.write_complete(sz);
    }

    // point to data
    uint8_t* ptr = packet.data();

    /**
     * Replication protocol:
     * 1) set 3 bytes for packet size
     * 2) the packet sequence is not touched!!!
     * 3) set 1 byte OK indicator
     */
    // Set New Packet size: new event_size + 1 byte replication status
    mariadb::set_byte3(ptr, new_event_size + 1);

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
    mariadb::set_byte4(ptr + event_header_offset, 0);
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
    mariadb::set_byte4(ptr + event_header_offset, 0);
    // Point to event_size
    event_header_offset += 4;

    // Set event_event size [4 bytes]
    mariadb::set_byte4(ptr + event_header_offset, new_event_size);
    // Next pos [4 bytes] is set by fixEvent(), go ahead!!!
    event_header_offset += 4;
    // Point to event_flags,
    event_header_offset += 4;

    // Set LOG_EVENT_SKIP_REPLICATION_F flags [2 bytes]
    mariadb::set_byte2(ptr + event_header_offset, LOG_EVENT_SKIP_REPLICATION_F);

    // Point to RAND_EVENT content now
    event_header_offset += 2;

    /**
     * We set now the value for the first and second seed
     * as input packet size and event_size
     * event_size is 0 for all packets belonging to a large event
     */
    // Set first seed as the input packet size (4 bytes only)
    mariadb::set_byte4(ptr + event_header_offset,
                       buf_len - (MYSQL_HEADER_LEN + 1));
    event_header_offset += 4;
    // Set 0 for next 4 bytes of first seed
    mariadb::set_byte4(ptr + event_header_offset, 0);

    // Point to second seed
    event_header_offset += 4;
    // Set second seed as the input event type (4 bytes only)
    mariadb::set_byte4(ptr + event_header_offset, orig_event_type);
    event_header_offset += 4;
    // Set 0 for next 4 bytes of second seed
    mariadb::set_byte4(ptr + event_header_offset, 0);

    /**
     * Now we remove the useless bytes in the buffer
     * in case of inout packet is bigger than RAND_EVENT packet
     */
    if (packet.length() > (new_event_size + 1 + MYSQL_HEADER_LEN))
    {
        uint32_t remove_bytes = packet.length() - (new_event_size + 1 + MYSQL_HEADER_LEN);
        packet.rtrim(remove_bytes);
    }

    // Fix Event Next pos = 0 and set new CRC32
    fixEvent(ptr + MYSQL_HEADER_LEN + 1, new_event_size, hdr);
}

/**
 * Extract the value of a specific column from a buffer
 * TODO: also in use in binlogrouter code, to be moved
 * in a common place
 *
 * @param buf    GWBUF with a resultset
 * @param col    The column number to extract
 * @return       The value of the column
 */
static std::optional<std::string_view> extract_column(const GWBUF& buf, size_t col)
{
    uint8_t* ptr;
    size_t len, ncol, collen;
    char* rval;

    ptr = (uint8_t*)buf.data();
    /* First packet should be the column count */
    len = mariadb::get_byte3(ptr);
    ptr += 3;
    if (*ptr != 1)      // Check sequence number is 1
    {
        return {};
    }
    ptr++;
    ncol = *ptr++;
    if (ncol < col)     // Not that many column in result
    {
        return {};
    }

    // Now ptr points at the column definition
    while (ncol-- > 0)
    {
        len = mariadb::get_byte3(ptr);
        ptr += 4;   // Skip to payload
        ptr += len; // Skip over payload
    }
    // Now we should have an EOF packet
    len = mariadb::get_byte3(ptr);
    ptr += 4;       // Skip to payload
    if (*ptr != 0xfe)
    {
        return {};
    }
    ptr += len;

    // Finally we have reached the row
    len = mariadb::get_byte3(ptr);
    ptr += 4;

    /**
     * The first EOF packet signals the start of the resultset rows
     * and the second  EOF packet signals the end of the result set.
     * If the resultset contains a second EOF packet right after the first one,
     * the result set is empty and contains no rows.
     */
    if (len == 5 && *ptr == 0xfe)
    {
        return {};
    }

    while (--col > 0)
    {
        collen = *ptr++;
        ptr += collen;
    }
    collen = *ptr++;
    return std::string_view{(const char*)ptr, collen};
}

/**
 * Abort filter operation
 */
void BinlogFilterSession::filterError()
{
    /* Abort client connection on copy failure */
    m_state = ERRORED;
    m_pSession->kill();
}

/**
 * Get replication checksum value from a GWBUF resultset
 * Sets the member variable m_crc to true in case of found
 * CRC32 value.
 *
 * @param packet     The resultset
 * @return           False on error
 */
void BinlogFilterSession::getReplicationChecksum(const GWBUF& packet)
{
    if (auto crc = extract_column(packet, 1))
    {
        if (mxb::sv_strcasestr(*crc, "CRC32") != std::string_view::npos)
        {
            m_crc = true;
        }
    }
}

/**
 * Handles the event size and sets member variables
 * 'm_is_large' and 'm_large_left'
 *
 * If received data len is MYSQL_PACKET_LENGTH_MAX
 * then the beginning of a large event receiving is set.
 *
 * Also remaining data are set
 *
 * @param len    The binlog event payload len
 * @param hdr    The reference to binlog event header
 */
void BinlogFilterSession::handlePackets(uint32_t len, const REP_HEADER& hdr)
{
    // Mark the beginning of a lrage event transmission
    if (len == MYSQL_PACKET_LENGTH_MAX)
    {
        // Mark the beginning of a large event transmission
        m_is_large = true;

        // Set remaining data to receive accordingly to hdr.event_size
        m_large_left = hdr.event_size - (MYSQL_PACKET_LENGTH_MAX - 1);
    }
}

/**
 * Process received data size of a large event transmission
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
 * Check QUERY_EVENT events.
 *
 * @see https://mariadb.com/kb/en/library/query_event/
 *
 * This function checks whether the statement should be replicated and whether the database/table name should
 * be rewritten. If a rewrite takes place, the buffer can be reallocated.
 *
 * @param buffer     Pointer to the buffer containing the event
 * @param hdr       The extracted replication header
 * @param extra_len Extra static bytes that this event has (only EXECUTE_LOAD_QUERY_EVENT uses it)
 */
void BinlogFilterSession::checkStatement(GWBUF& buffer, const REP_HEADER& hdr, int extra_len)
{
    uint8_t* event = buffer.data() + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN;
    uint32_t event_size = hdr.event_size - BINLOG_EVENT_HDR_LEN;

    int db_name_len = event[4 + 4];
    int var_block_len_offset = 4 + 4 + 1 + 2;
    int var_block_len = mariadb::get_byte2(event + var_block_len_offset);
    int static_size = 4 + 4 + 1 + 2 + 2 + extra_len;
    int statement_len = event_size - static_size - var_block_len - db_name_len - 1 - (m_crc ? 4 : 0);

    std::string db((char*)event + static_size + var_block_len, db_name_len);
    std::string sql((char*)event + static_size + var_block_len + db_name_len + 1, statement_len);

    const auto& config = m_config;
    m_skip = should_skip_query(parser(), config, sql, db);
    MXB_INFO("[%s] (%s) %s", m_skip ? "SKIP" : "    ", db.c_str(), sql.c_str());

    if (!m_skip && config.rewrite_src)
    {
        auto new_db = config.rewrite_src.replace(db, config.rewrite_dest.c_str());
        auto new_sql = config.rewrite_src.replace(sql, config.rewrite_dest.c_str());

        if ((new_db.empty() && !db.empty()) || (new_sql.empty() && !sql.empty()))
        {
            MXB_ERROR("PCRE2 error on pattern '%s' with replacement '%s': %s",
                      config.rewrite_src.pattern().c_str(),
                      config.rewrite_dest.c_str(),
                      config.rewrite_src.error().c_str());
        }
        else if (db != new_db || sql != new_sql)
        {
            db = new_db;
            sql = new_sql;
            int len = sql.length() + db.length() - statement_len - db_name_len;

            if (len > 0)
            {
                // Buffer is too short, extend it
                buffer.prepare_to_write(len);
                buffer.write_complete(len);
            }
            else if (len < 0)
            {
                // Make the buffer shorter (len is negative so we add it to the total length)
                buffer.rtrim(-len);
            }

            event = buffer.data() + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN;

            memcpy(event + static_size + var_block_len, db.c_str(), db.length() + 1);
            memcpy(event + static_size + var_block_len + db.length() + 1, sql.c_str(), sql.length());
            event[4 + 4] = db.length();

            // Also fix the packet length
            mariadb::set_byte3(buffer.data(), buffer.length() - MYSQL_HEADER_LEN);

            MXB_INFO("Rewrote query: (%s) %s", db.c_str(), sql.c_str());
        }
    }
}

void BinlogFilterSession::checkAnnotate(const uint8_t* event, const uint32_t event_size)
{
    std::string sql((char*)event, event_size - (m_crc ? 4 : 0));
    m_skip = should_skip_query(parser(), m_config, sql);
    MXB_INFO("[%s] Annotate: %s", m_skip ? "SKIP" : "    ", sql.c_str());
}
