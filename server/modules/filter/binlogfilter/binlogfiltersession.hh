#pragma once
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

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "binlogfilter.hh"

//TODO: add in a separate .h file in common to BinlogRouter code
#define LOG_EVENT_IGNORABLE_F                   0x0080
#define LOG_EVENT_SKIP_REPLICATION_F            0x8000
#define RAND_EVENT                              0x000D
#define TABLE_MAP_EVENT                         0x0013
#define XID_EVENT                               0x0010
#define QUERY_EVENT                             0x0002
#define MARIADB10_GTID_EVENT                    0x00a2
#define MARIADB_ANNOTATE_ROWS_EVENT             0x00a0
#define HEARTBEAT_EVENT                         0x001B
#define BINLOG_EVENT_HDR_LEN                        19

class BinlogConfig;

typedef struct rep_header_t
{
    int             payload_len;    /*< Payload length (24 bits) */
    uint8_t         seqno;          /*< Response sequence number */
    uint8_t         ok;             /*< OK Byte from packet */
    uint32_t        timestamp;      /*< Timestamp - start of binlog record */
    uint8_t         event_type;     /*< Binlog event type */
    uint32_t        serverid;       /*< Server id of master */
    uint32_t        event_size;     /*< Size of header, post-header and body */
    uint32_t        next_pos;       /*< Position of next event */
    uint16_t        flags;          /*< Event flags */
} REP_HEADER;
// End TODO
//
class BinlogFilter;

class BinlogFilterSession : public maxscale::FilterSession
{
    // Prevent copy-constructor and assignment operator usage
    BinlogFilterSession(const BinlogFilterSession&);
    BinlogFilterSession& operator = (const BinlogFilterSession&);

public:
    ~BinlogFilterSession();

    // Create a new filter session
    static BinlogFilterSession* create(MXS_SESSION* pSession,
                                       const BinlogFilter* pFilter);

    // Called when a client session has been closed
    void close();

    // Handle a query from the client
    int routeQuery(GWBUF* pPacket);

    // Handle a reply from server
    int clientReply(GWBUF* pPacket);

private:
    // Used in the create function
    BinlogFilterSession(MXS_SESSION* pSession, const BinlogFilter* pFilter);

    // Reference to Filter instance
    const BinlogFilter& m_filter;

    // Skip database/table events in current trasaction
    void skipDatabaseTable(const uint8_t* data, const REP_HEADER& hdr);

    // Get Replication Checksum from registration query
    bool getReplicationChecksum(GWBUF* pPacket);

    // Abort filter operations
    void filterError(GWBUF* pPacket);

    // Fix event: set next pos to 0 and set new CRC32
    void fixEvent(uint8_t* data, uint32_t event_size);

    // Whether to skip current event
    bool checkEvent(GWBUF* data, const REP_HEADER& hdr);

    // Filter the replication event
    void replaceEvent(GWBUF** data);

    // Handle event size
    void handlePackets(uint32_t len, const REP_HEADER& hdr);

    // Handle event data
    void handleEventData(uint32_t len, const uint8_t seqno);

    // Check SQL statement in QUERY_EVENT
    bool checkStatement(const uint8_t* event,
                        const uint32_t event_size);

    // Check Default DB name extracted from QUERY_EVENT
    bool checkUseDB(const std::string& db_name,
                    const BinlogConfig& config);

    // Check DB.TABLE in ANNOTATE_ROWS event
    void checkAnnotate(const uint8_t* event,
                       const uint32_t event_size);

private:
    // Internal states for filter operations
    enum state_t
    {
        ERRORED,         // A blocking error occurred
        INACTIVE,        // Fitering is not active
        COMMAND_MODE,    // Connected client in SQL mode: no filtering
        BINLOG_MODE      // Connected client in BINLOG_MODE: filter events
    };

private:
    // Event filtering member vars
    uint32_t  m_serverid;           // server-id of connected slave
    state_t   m_state;              // Internal state
    bool      m_skip;               // Mark event skipping
    bool      m_crc;                // CRC32 for events. Not implemented
    uint32_t  m_large_left;         // Remaining bytes of a large event
    bool      m_is_large;           // Large Event indicator
    GWBUF*    m_sql_query;          // SQL query buffer
};
