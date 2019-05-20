/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#pragma once

#include <maxscale/ccdefs.hh>

struct GWBUF;

namespace maxsql
{

class ComResponse;

// Minimal class to track the lifetime of a query.
// TODO add documentation
class PacketTracker
{
public:
    enum class State {FirstPacket, Field, FieldEof, Row,
                      ComFieldList, ComStatistics, ComStmtFetch,
                      Done, ErrorPacket, Error};

    PacketTracker() = default;
    explicit PacketTracker(GWBUF* pQuery);      // Track this query

    void  update(GWBUF* pPacket);       // Update as packets are received.
    State state() const;
    bool  expecting_more_packets() const;

private:
    // State functions.
    State first_packet(const ComResponse& response);
    State field(const ComResponse& response);
    State field_eof(const ComResponse& response);
    State row(const ComResponse& response);
    State com_field_list(const ComResponse& response);
    State com_statistics(const ComResponse& response);
    State com_stmt_fetch(const ComResponse& response);

    State expect_no_more(const ComResponse& response);          // states: Done, ErrorPacket, Error

    State m_state = State::Error;
    bool  m_client_packet_internal = false;
    bool  m_server_packet_internal = false;

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
