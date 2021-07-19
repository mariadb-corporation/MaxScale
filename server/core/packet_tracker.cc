/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// LIMITATION: local infile not handled yet.

#include <maxscale/packet_tracker.hh>
#include <maxscale/mysql_plus.hh>
#include <maxscale/modutil.hh>
#include <maxbase/log.hh>

#include <array>

namespace maxsql
{

static const std::array<std::string, 11> state_names = {
    "FirstPacket",  "Field",         "FieldEof",     "Row",
    "ComFieldList", "ComStatistics", "ComStmtFetch",
    "Done",         "ErrorPacket",   "Error"
};

std::ostream& operator<<(std::ostream& os, PacketTracker::State state)
{
    auto ind = size_t(state);
    return os << ((ind < state_names.size()) ? state_names[ind] : "UNKNOWN");
}

PacketTracker::PacketTracker(GWBUF* pPacket)
{
    ComRequest request(ComPacket(pPacket, &m_client_com_packet_internal));
    m_command = request.command();
    m_expect_more_split_query_packets = request.is_split_leader();

    MXS_SDEBUG("PacketTracker Command: " << STRPACKETTYPE(m_command));

    if (request.server_will_respond())
    {
        switch (m_command)
        {
        case MXS_COM_FIELD_LIST:
            m_state = State::ComFieldList;
            break;

        case MXS_COM_STATISTICS:
            m_state = State::ComStatistics;
            break;

        case MXS_COM_STMT_FETCH:
            m_state = State::ComStmtFetch;
            break;

        default:
            m_state = State::FirstPacket;
            break;
        }
    }
    else
    {
        m_state = State::Done;
    }
}

bool PacketTracker::update_request(GWBUF* pPacket)
{
    MXS_SDEBUG("PacketTracker update_request: " << STRPACKETTYPE(m_command));
    ComPacket com_packet(pPacket, &m_client_com_packet_internal);

    if (!m_expect_more_split_query_packets)
    {
        MXS_SERROR("PacketTracker::update_request() called while not expecting splits");
        mxb_assert(!true);
        m_state = State::Error;
    }
    else if (!com_packet.is_split_continuation())
    {
        MXS_SERROR("PacketTracker::update_request() received a non-split packet");
        mxb_assert(!true);
        m_state = State::Error;
    }

    if (com_packet.is_split_trailer())
    {
        m_expect_more_split_query_packets = false;
    }

    return m_state != State::Error;
}

bool PacketTracker::expecting_request_packets() const
{
    return m_expect_more_split_query_packets;
}

bool PacketTracker::expecting_response_packets() const
{
    switch (m_state)
    {
    case State::Done:
    case State::ErrorPacket:
    case State::Error:
        return false;

    default:
        return true;
    }
}

bool PacketTracker::expecting_more_packets() const
{
    return expecting_response_packets() || expecting_request_packets();
}

static constexpr std::array<PacketTracker::State, 5> data_states {
    PacketTracker::State::Field,
    PacketTracker::State::Row,
    PacketTracker::State::ComFieldList,
    PacketTracker::State::ComStatistics,
    PacketTracker::State::ComStmtFetch
};

void PacketTracker::update_response(GWBUF* pPacket)
{
    ComPacket com_packet(pPacket, &m_server_com_packet_internal);

    bool expect_data_only = std::find(begin(data_states), end(data_states), m_state) != end(data_states);
    ComResponse response(com_packet, expect_data_only);

    if (response.is_split_continuation())
    {   // no state change, just more of the same data
        MXS_SDEBUG("PacketTracker::update_response IGNORE trailing split packets");
        return;
    }

    if (response.is_err())
    {
        m_state = State::ErrorPacket;
        return;
    }

    switch (m_state)
    {
    case State::FirstPacket:
        m_state = first_packet(response);
        break;

    case State::Field:
        m_state = field(response);
        break;

    case State::FieldEof:
        m_state = field_eof(response);
        break;

    case State::Row:
        m_state = row(response);
        break;

    case State::ComFieldList:
        m_state = com_field_list(response);
        break;

    case State::ComStatistics:
        m_state = com_statistics(response);
        break;

    case State::ComStmtFetch:
        m_state = com_stmt_fetch(response);
        break;

    case State::Done:
    case State::ErrorPacket:
    case State::Error:
        m_state = expect_no_response_packets(response);
        break;
    }
}

PacketTracker::State PacketTracker::first_packet(const ComResponse& response)
{
    State new_state = m_state;

    if (response.is_data())
    {
        m_field_count = 0;
        m_total_fields = ComQueryResponse(response).nFields();
        new_state = State::Field;
    }
    else if (response.is_ok())
    {
        new_state = (ComOK(response).more_results_exist()) ? State::FirstPacket : State::Done;
    }
    else if (response.is_local_infile())
    {
        MXS_SERROR("TODO handle local infile packet");
        mxb_assert(!true);
        new_state = State::Error;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::field(const ComResponse& response)
{
    State new_state = m_state;

    if (!response.is_data())
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }
    else if (++m_field_count == m_total_fields)
    {
        new_state = State::FieldEof;
    }

    return new_state;
}

PacketTracker::State PacketTracker::field_eof(const ComResponse& response)
{
    State new_state = m_state;

    if (response.is_eof())
    {
        new_state = State::Row;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::row(const ComResponse& response)
{
    State new_state = m_state;

    if (response.is_data())
    {
        // ok
    }
    else if (response.is_eof())
    {
        new_state = (ComEOF(response).more_results_exist()) ? State::FirstPacket : State::Done;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::com_field_list(const ComResponse& response)
{
    State new_state = m_state;

    if (response.is_data())
    {
        // ok
    }
    else if (response.is_eof())
    {
        new_state = State::Done;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::com_statistics(const maxsql::ComResponse& response)
{
    State new_state = m_state;

    if (response.is_data())
    {
        new_state = State::Done;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::com_stmt_fetch(const maxsql::ComResponse& response)
{
    State new_state = m_state;

    if (response.is_data())
    {
        // ok
    }
    else if (response.is_eof())
    {
        new_state = (ComEOF(response).more_results_exist()) ? State::ComStmtFetch : State::Done;
    }
    else
    {
        MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
        new_state = State::Error;
    }

    return new_state;
}

PacketTracker::State PacketTracker::expect_no_response_packets(const ComResponse& response)
{
    MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
    return State::Error;
}
}
