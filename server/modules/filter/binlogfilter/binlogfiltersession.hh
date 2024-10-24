/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "binlogconfig.hh"
#include "binlogfilter.hh"

// TODO: add in a separate .h file in common to BinlogRouter code
#define LOG_EVENT_IGNORABLE_F        0x0080
#define LOG_EVENT_SKIP_REPLICATION_F 0x8000
#define RAND_EVENT                   0x000D
#define TABLE_MAP_EVENT              0x0013
#define XID_EVENT                    0x0010
#define BEGIN_LOAD_QUERY_EVENT       0x0011
#define EXECUTE_LOAD_QUERY_EVENT     0x0012
#define QUERY_EVENT                  0x0002
#define MARIADB10_GTID_EVENT         0x00a2
#define MARIADB_ANNOTATE_ROWS_EVENT  0x00a0
#define HEARTBEAT_EVENT              0x001B
#define BINLOG_EVENT_HDR_LEN         19

typedef struct rep_header_t
{
    int      payload_len;   /*< Payload length (24 bits) */
    uint8_t  seqno;         /*< Response sequence number */
    uint8_t  ok;            /*< OK Byte from packet */
    uint32_t timestamp;     /*< Timestamp - start of binlog record */
    uint8_t  event_type;    /*< Binlog event type */
    uint32_t serverid;      /*< Server id of master */
    uint32_t event_size;    /*< Size of header, post-header and body */
    uint32_t next_pos;      /*< Position of next event */
    uint16_t flags;         /*< Event flags */
} REP_HEADER;
// End TODO
//
class BinlogFilter;

class BinlogFilterSession : public maxscale::FilterSession
{
    // Prevent copy-constructor and assignment operator usage
    BinlogFilterSession(const BinlogFilterSession&);
    BinlogFilterSession& operator=(const BinlogFilterSession&);

public:
    ~BinlogFilterSession();

    BinlogFilterSession(MXS_SESSION* pSession, SERVICE* pService, const BinlogFilter* pFilter);

    bool routeQuery(GWBUF&& packet) override;

    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    // Reference to Filter instance
    const BinlogFilter& m_filter;

    // Skip database/table events in current transaction
    void skipDatabaseTable(const uint8_t* data);

    // Get Replication Checksum from registration query
    void getReplicationChecksum(const GWBUF& packet);

    // Abort filter operations
    void filterError();

    // Fix event: set next pos to 0 and set new CRC32
    void fixEvent(uint8_t* data, uint32_t event_size, const REP_HEADER& hdr);

    // Whether to skip current event
    bool checkEvent(GWBUF& data, const REP_HEADER& hdr);

    // Filter the replication event
    void replaceEvent(GWBUF& data, const REP_HEADER& hdr);

    // Handle event size
    void handlePackets(uint32_t len, const REP_HEADER& hdr);

    // Handle event data
    void handleEventData(uint32_t len);

    // Check SQL statement in QUERY_EVENT or EXECUTE_LOAD_QUERY_EVENT
    void checkStatement(GWBUF& buffer, const REP_HEADER& hdr, int extra_len = 0);

    // Check DB.TABLE in ANNOTATE_ROWS event
    void checkAnnotate(const uint8_t* event,
                       const uint32_t event_size);

private:
    // Internal states for filter operations
    enum state_t
    {
        ERRORED,        // A blocking error occurred
        COMMAND_MODE,   // Connected client in SQL mode: no filtering
        BINLOG_MODE     // Connected client in BINLOG_MODE: filter events
    };

private:
    // A local copy of the configuration
    BinlogConfig::Values m_config;

    // Event filtering member vars
    uint32_t m_serverid = 0;            // server-id of connected slave
    state_t  m_state = COMMAND_MODE;    // Internal state
    bool     m_skip = false;            // Mark event skipping
    bool     m_crc = false;             // CRC32 for events. Not implemented
    uint32_t m_large_left = 0;          // Remaining bytes of a large event
    bool     m_is_large = false;        // Large Event indicator
    bool     m_reading_checksum = false;// Whether we are waiting for the binlog checksum response
    bool     m_is_gtid = false;
};
