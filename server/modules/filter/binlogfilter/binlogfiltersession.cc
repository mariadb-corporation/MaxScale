/*
 * Copyright (c) 2017 MariaDB Corporation Ab
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

static char* extract_column(GWBUF *buf, int col);
static void event_set_crc32(uint8_t* event, uint32_t event_size);

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
    , m_complete_packet(true)
    , m_crc(false)
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
        uint8_t *data = GWBUF_DATA(pPacket);

        // We assume OK indicator, the first byte after MYSQL_HEADER_LEN is 0
        // TODO: check complete packet or
        // at least MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN bytes
        switch (MYSQL_GET_COMMAND(data))
        {
        case COM_REGISTER_SLAVE:
            // Connected client is registering as Slave Server
            m_serverid = gw_mysql_get_byte4(data + MYSQL_HEADER_LEN + 1);
            MXS_INFO("Client is registering as "
                     "Slave server with ID %" PRIu32 "",
                     m_serverid);
            break;

        case COM_BINLOG_DUMP:
            // Connected Slave server waits for binlog events
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
            if (strcasestr((char *)data + MYSQL_HEADER_LEN + 1,
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
    switch (m_state)
    {
    /**
     * TODO: remove this code when filters
     * will be able to pass some session informations
     * from "session->router_session"
     * m_crc will be thus set in routeQuery.
     */
        case COMMAND_MODE:
            if (m_sql_query != NULL && !getReplicationChecksum(pPacket))
            {
                // Free buffer and close client connection
                filterError(pPacket);
                return 0;
            }
            break;

        case BINLOG_MODE:
            if (skipEvent(pPacket))
            {
                // Assuming ROW replication format:
                // If transaction events need to be skipped,
                // they are replaced by an empty paylod packet
                filterEvent(pPacket);
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
static inline void extractHeader(register const uint8_t *event,
                                 register REP_HEADER *hdr)
{
    hdr->payload_len = gw_mysql_get_byte3(event);
    hdr->seqno = event[3];
    hdr->ok = event[MYSQL_HEADER_LEN];
    hdr->timestamp = gw_mysql_get_byte4(event + MYSQL_HEADER_LEN + 1);
    hdr->event_type = event[MYSQL_HEADER_LEN + 1 + 4];
    // TODO: add offsets in order to facilitate reading
    hdr->serverid = gw_mysql_get_byte4(event + MYSQL_HEADER_LEN + 1 + 4 + 1);
    hdr->event_size = gw_mysql_get_byte4(event + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4);
    hdr->next_pos = gw_mysql_get_byte4(event + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4);
    hdr->flags = gw_mysql_get_byte2(event + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4 + 4);

    MXS_INFO("Event Header: serverId %" PRIu32 ", event_type [%d], "
             "flags %d, event_size %" PRIu32 ", next_pos %" PRIu32 ", "
             "packet size %" PRIu32 "",
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
 * @return          True id TABLE_MAP_EVENT contains
 * db/table names to skip
 */
bool BinlogFilterSession::skipEvent(GWBUF* buffer)
{
    uint8_t *event = GWBUF_DATA(buffer);
    REP_HEADER hdr;

    // Extract Replication header event from event data
    extractHeader(event, &hdr);

    if (hdr.ok == 0)
    {
        switch(hdr.event_type)
        {
           case TABLE_MAP_EVENT:
               // Check db/table and set m_skip accordingly
               skipDatabaseTable(event, hdr);
               break;
           case XID_EVENT:
              // COMMIT: reset m_skip if set and set next pos to 0
              if (m_skip)
              {
                  m_skip = false;
                  /**
                   * Some events skipped.
                   * Set next pos to 0 instead of real one and new CRC32
                   */
                  fixEvent(event + MYSQL_HEADER_LEN + 1, hdr.event_size);

                  MXS_INFO("Skipped events: Setting next_pos = 0 in XID_EVENT");
              }
              break;
           default:
              // Other events can be skipped or not, depending on m_skip value
              break;
        }
        // m_skip could be true or false
        return m_skip;
    }
    else
    {
        return false;    // always false: no filtering
    }
}

/**
 * Extract Dbname and Table name from TABLE_MAP event
 *
 * @param ptr       Pointer to event data
 * @param dbname    Pointer to pointer to database name
 * @param tblname   Pointer to pointer to table name
 */
static void inline extractTableInfo(const uint8_t *ptr,
                                    char **dbname,
                                    char **tblname)
{
    // TODO: add offsets in order to facilitate reading
    int db_len = *(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8);

    *dbname = (char *)(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 1);
    *tblname = (char *)(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 1 + db_len + 1 + 1);
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
    // Each time this event is seen the m_skip is overwritten
    if (hdr.ok == 0 && hdr.event_type == TABLE_MAP_EVENT)
    {
        char *db = NULL;
        char *table = NULL;

        // Get filter configuration
        const BinlogConfig& fConfig = m_filter.getConfig();

        // Get db/table
        extractTableInfo(data, &db, &table);

        // Check match with configuration
        m_skip = (bool)(strcmp(db, fConfig.dbname.c_str()) == 0 ||
                  strcmp(table, fConfig.table.c_str()) == 0);

        MXS_INFO("Dbname is [%s], Table is [%s], Skip [%s]\n",
                 db ? db : "",
                 table ? table : "",
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
 * Replace data in the current event: no memory allocation
 *
 * @param pPacket    The GWBUF with event data
 */
void BinlogFilterSession::filterEvent(GWBUF* pPacket)
{
    ss_dassert(m_skip == true);

    uint8_t *ptr = GWBUF_DATA(pPacket);
    // size of empty rand_event (header + 0 bytes + CRC32)
    uint32_t new_event_size = BINLOG_EVENT_HDR_LEN + 0;
    new_event_size += m_crc ? 4 : 0;

    // Set NEW event_type
    ptr[MYSQL_HEADER_LEN + 1 + 4] = RAND_EVENT;
    // SET ignorable flags
    gw_mysql_set_byte2(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4 + 4,
                       LOG_EVENT_IGNORABLE_F | LOG_EVENT_SKIP_REPLICATION_F);

    // Set event_event size)
    gw_mysql_set_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4,
                       new_event_size);

    // Set New Packet size: new event_size + 1 byte replication status
    gw_mysql_set_byte3(ptr, new_event_size + 1);

    MXS_INFO("Filtered event #%d,"
             "ok %d, type %d, flags %d, size %d, next_pos %d, packet_size %d\n",
             ptr[3],
             ptr[4],
             RAND_EVENT,
             gw_mysql_get_byte2(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4 + 4),
             gw_mysql_get_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4),
             gw_mysql_get_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4),
             gw_mysql_get_byte3(ptr));

    // Remove the useless bytes in the buffer
    uint32_t remove_bytes = gwbuf_length(pPacket) - (new_event_size + 1 + MYSQL_HEADER_LEN);
    pPacket = gwbuf_rtrim(pPacket, remove_bytes);

    // Fix Event Next pos = 0 and set new CRC32
    fixEvent(ptr + MYSQL_HEADER_LEN + 1, new_event_size);
}

/**
 *Extract the value of a specific columnr from a buffer
* TODO: also in use in binlogrouter code, to be moved
* in a common place
*
* @param buf    GWBUF with a resultset
* @param col    The column number to extract
* @return       The value of the column
*/
static char* extract_column(GWBUF *buf, int col)
{
    uint8_t *ptr;
    int len, ncol, collen;
    char    *rval;

    if (buf == NULL)
    {
        return NULL;
    }

    ptr = (uint8_t *)GWBUF_DATA(buf);
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
    if ((rval = (char *)MXS_MALLOC(collen + 1)) == NULL)
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
    char *crc;
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
