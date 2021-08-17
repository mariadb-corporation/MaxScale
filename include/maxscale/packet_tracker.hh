/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/ccdefs.hh>

struct GWBUF;

namespace maxsql
{

class ComResponse;

// TODO add documentation
class PacketTracker
{
public:
    // The State reflects the response status. For the unlikely event that the query is split, but
    // no response is expected, the tracker may still be waiting for packets from the client while
    // m_state = Done. The function expecting_more_packets() would return true in this case.
    // TODO, maybe, rename to ResponseState.
    enum class State {FirstPacket, Field, FieldEof, Row,
                      ComFieldList, ComStatistics, ComStmtFetch,
                      Done, ErrorPacket, Error};

    PacketTracker() = default;

    explicit PacketTracker(GWBUF* pQuery);      // Track this query

    PacketTracker(const PacketTracker&) = delete;
    PacketTracker& operator=(const PacketTracker&) = delete;

    PacketTracker(PacketTracker&&) = default;
    PacketTracker& operator=(PacketTracker&&) = default;

    bool update_request(GWBUF* pPacket);        // Updates the query (must be a split packet)
    void update_response(GWBUF* pPacket);       // Update as response packets are received.

    bool expecting_request_packets() const;
    bool expecting_response_packets() const;
    bool expecting_more_packets() const;

    State state() const;

private:
    // State functions.
    State first_packet(const ComResponse& response);
    State field(const ComResponse& response);
    State field_eof(const ComResponse& response);
    State row(const ComResponse& response);
    State com_field_list(const ComResponse& response);
    State com_statistics(const ComResponse& response);
    State com_stmt_fetch(const ComResponse& response);
    State expect_no_response_packets(const ComResponse& response);      // states: Done, ErrorPacket,  Error

    State m_state = State::Error;
    bool  m_client_com_packet_internal = false;
    bool  m_server_com_packet_internal = false;
    bool  m_expect_more_split_query_packets = false;

    int m_command;
    int m_total_fields;
    int m_field_count;
};

inline PacketTracker::State PacketTracker::state() const
{
    return m_state;
}

std::ostream& operator<<(std::ostream&, PacketTracker::State state);
}
