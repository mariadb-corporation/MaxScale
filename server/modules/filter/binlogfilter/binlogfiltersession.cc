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

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "binlogfilter"

#include <maxscale/protocol/mysql.h>
#include "binlogfilter.hh"
#include "binlogfiltersession.hh"
#include <inttypes.h>

// New packet which replaces the skipped events has 0 payload
#define NEW_PACKET_PAYLOD BINLOG_EVENT_HDR_LEN

// BinlogFilterSession constructor
BinlogFilterSession::BinlogFilterSession(MXS_SESSION* pSession,
                                         const BinlogFilter* pFilter)
    : mxs::FilterSession(pSession)
    , m_filter(*pFilter)
    , m_serverid(0)
    , m_state(pFilter->is_active() ? COMMAND_MODE : INACTIVE)
    , m_skip(false)
    , m_complete_packet(true)
    , m_crc(false)
{
    MXS_NOTICE("Filter [%s] is %s",
               MXS_MODULE_NAME,
               m_filter.getConfig().active ? "enabled" : "disabled");
}

// BinlogFilterSession destructor
BinlogFilterSession::~BinlogFilterSession()
{
}

//static: create new session
BinlogFilterSession* BinlogFilterSession::create(MXS_SESSION* pSession,
                                                 const BinlogFilter* pFilter)
{
    return new BinlogFilterSession(pSession, pFilter);
}

// route input data from client
int BinlogFilterSession::routeQuery(GWBUF* pPacket)
{
    if (m_state != INACTIVE)
    {
        uint8_t *data = GWBUF_DATA(pPacket);

        // We assume OK indicator, the first byte after MYSQL_HEADER_LEN is 0
        // TODO: check complete packet or
        // at least MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN bytes
        switch ((int)MYSQL_GET_COMMAND(data))
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
            break;
        }
    }

    // Route input data
    return mxs::FilterSession::routeQuery(pPacket);
}

// Reply data to client
int BinlogFilterSession::clientReply(GWBUF* pPacket)
{
    if (m_state == BINLOG_MODE)
    {
        if (skipEvent(pPacket))
        {
            // Assuming ROW replication format:
            // If transaction events needs to be skipped,
            // they are replaced by an empty paylod packet
            filterEvent(pPacket);
        }
    }

    // Send data
    return mxs::FilterSession::clientReply(pPacket);
}

// close session
void BinlogFilterSession::close()
{
    if (m_state == BINLOG_MODE)
    {
        MXS_INFO("Slave server %" PRIu32 ": replication stopped.",
                 m_serverid);
    }
}

// extract binlog replication header from event data
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

    MXS_INFO("Slave server %" PRIu32 ": clientReply, event_type [%d], "
             "flags %d, event_size %" PRIu32 ", next_pos %" PRIu32 ", packet size %" PRIu32 "",
             hdr->serverid,
             hdr->event_type,
             hdr->flags,
             hdr->event_size,
             hdr->next_pos,
             hdr->payload_len);
}

// Check whether events in a transaction can be skipped.
// The triggering event is TABLE_MAP_EVENT.
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
                  // Some events skipped.
                  // Set next pos to 0 instead of real one.
                  gw_mysql_set_byte4(event + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4, 0);

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

// Extract Dbname and Tabe name from TABLE_MAP event
static void inline extractTableInfo(const uint8_t *ptr,
                                    char **dbname,
                                    char **tblname)
{
    // TODO: add offsets in order to facilitate reading
    int db_len = *(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8);

    *dbname = (char *)(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 1);
    *tblname = (char *)(ptr + MYSQL_HEADER_LEN + 1 + BINLOG_EVENT_HDR_LEN + 8 + 1 + db_len + 1 + 1);
}

// Check whether a db/table can be skipped based on configuration
void BinlogFilterSession::skipDatabaseTable(const uint8_t* data,
                                            const REP_HEADER& hdr)
{
    // Check for TABBLE_MAP event:
    // Each time this event is seen the m_skip is overwritten
    if (hdr.ok == 0 && hdr.event_type == TABLE_MAP_EVENT)
    {
        char *db = NULL;
        char *table = NULL;
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

// Replace the current event: no memory allocation
void BinlogFilterSession::filterEvent(GWBUF* pPacket)
{
    ss_dassert(m_skip == true);

    uint8_t *ptr = GWBUF_DATA(pPacket);

    // Set NEW event_type
    ptr[MYSQL_HEADER_LEN + 1 + 4] = RAND_EVENT;
    // SET ignorable flags
    gw_mysql_set_byte2(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4 + 4,
                       LOG_EVENT_IGNORABLE_F | LOG_EVENT_SKIP_REPLICATION_F);

    // Set event_len, size of empty rand_event (header + 0 bytes)
    gw_mysql_set_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4,
                       BINLOG_EVENT_HDR_LEN + 0);

    // Set next pos to 0
    gw_mysql_set_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4, 0);

    // Set New Packet size: even_len + 1 byte replication status
    gw_mysql_set_byte3(ptr, BINLOG_EVENT_HDR_LEN + 0 + 1);

    MXS_INFO("All events belonging to this table will be skipped");

    MXS_INFO("Filtered event #%d,"
             "ok %d, type %d, flags %d, size %d, next_pos %d, packet_size %d\n",
             ptr[3],
             ptr[4],
             RAND_EVENT,
             gw_mysql_get_byte2(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4 + 4),
             gw_mysql_get_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4),
             gw_mysql_get_byte4(ptr + MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + 4),
             gw_mysql_get_byte3(ptr));

    // Remove useless bytes
    pPacket = gwbuf_rtrim(pPacket,
                          gwbuf_length(pPacket) - (BINLOG_EVENT_HDR_LEN + 1 + 4));
}
