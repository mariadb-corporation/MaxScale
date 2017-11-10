#pragma once
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

#include <maxscale/cppdefs.hh>
#include <maxscale/filter.hh>
#include "binlogfilter.hh"

//TODO: add in a separate .h file in common to BinlogRouter code
#define LOG_EVENT_IGNORABLE_F                   0x0080
#define LOG_EVENT_SKIP_REPLICATION_F            0x8000
#define RAND_EVENT                              0x000D
#define TABLE_MAP_EVENT                         0x0013
#define XID_EVENT                               0x0010
#define BINLOG_EVENT_HDR_LEN                        19

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

    // Whether to skip current event
    bool skipEvent(GWBUF* data);

    // Skip database/table events in current trasaction
    void skipDatabaseTable(const uint8_t* data, const REP_HEADER& hdr);

    // Filter the replication event
    void filterEvent(GWBUF* data);

private:
    // Used in the create function
    BinlogFilterSession(MXS_SESSION* pSession, const BinlogFilter* pFilter);

    // Reference to Filter instance
    const BinlogFilter& m_filter;

private:
    // Internal states for filter operations
    enum state_t
    {
        INACTIVE,        // Fitering is not active
        COMMAND_MODE,    // Connected client in SQL mode: no filtering
        BINLOG_MODE      // Connected client in BINLOG_MODE: filter events
    };

private:
    // Event filtering member vars
    uint32_t  m_serverid;           // server-id of connected slave
    state_t   m_state;              // Internal state
    bool      m_skip;               // Mark event skipping
    bool      m_complete_packet;    // A complete received. Not implemented
    bool      m_crc;                // CRC32 for events. Not implemented
    bool      m_large_payload;      // Packet bigger than 16MB. Not implemented
};
