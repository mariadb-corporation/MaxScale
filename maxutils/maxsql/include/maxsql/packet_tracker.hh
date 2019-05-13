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
    enum class State {FirstPacket, Field, FieldEof, ComFieldList, Row, Done, ErrorPacket, Error};

    PacketTracker() = default;
    explicit PacketTracker(GWBUF* pQuery);      // Track this query

    void  update(GWBUF* pPacket);       // Update as packets are received.
    State state() const;
    bool  expecting_more_packets() const;

private:
    // State functions.
    State first_packet(const ComResponse& com_packet);
    State field(const ComResponse& com_packet);
    State field_eof(const ComResponse& com_packet);
    State com_field_list(const ComResponse& com_packet);
    State row(const ComResponse& com_packet);
    State expect_no_more(const ComResponse& com_packet);        // states: Done, ErrorPacket, Error

    State m_state = State::Error;
    bool  m_client_packet_bool = false;
    bool  m_server_packet_bool = false;

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
