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

// TODO handle client split packets
// TODO handle https://mariadb.com/kb/en/library/com_statistics/
// TODO handle https://mariadb.com/kb/en/library/com_stmt_fetch/
// TODO handle local infile local packet

#include <maxsql/packet_tracker.hh>
#include <maxsql/mysql_plus.hh>
#include <maxscale/modutil.hh>

#include <array>

namespace maxsql
{

static const std::array<std::string, 8> state_names = {
    "FirstPacket", "Field", "FieldEof",    "ComFieldList",
    "Row",         "Done",  "ErrorPacket", "Error"
};

std::ostream& operator<<(std::ostream& os, PacketTracker::State state)
{
    auto ind = size_t(state);
    return os << ((ind < state_names.size()) ? state_names[ind] : "UNKNOWN");
}

PacketTracker::PacketTracker(GWBUF* pPacket)
    : m_command(ComRequest(ComPacket(pPacket, &m_client_packet_bool)).command())
{
    MXS_SINFO("PacketTracker Command: " << STRPACKETTYPE(m_command));   // TODO remove or change to debug

    // TODO mxs_mysql_command_will_respond() => ComRequest::mariadb_will_respond();
    m_state = (m_command == COM_FIELD_LIST) ? State::ComFieldList : State::FirstPacket;
}

bool PacketTracker::expecting_more_packets() const
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

static const std::array<PacketTracker::State, 3> data_states {
    PacketTracker::State::Field, PacketTracker::State::ComFieldList, PacketTracker::State::Row
};

void PacketTracker::update(GWBUF* pPacket)
{
    ComPacket com_packet(pPacket, &m_server_packet_bool);

    bool expect_data_only = std::find(begin(data_states), end(data_states), m_state) != end(data_states);
    ComResponse response(com_packet, expect_data_only);

    if (response.is_err())
    {
        m_state = State::ErrorPacket;
        return;
    }

    if (!response.is_split_continuation())
    {
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

        case State::ComFieldList:
            m_state = com_field_list(response);
            break;

        case State::Row:
            m_state = row(response);
            break;

        case State::Done:
        case State::ErrorPacket:
        case State::Error:
            m_state = expect_no_more(response);
            break;
        }
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

PacketTracker::State PacketTracker::com_field_list(const ComResponse& response)
{
    State new_state = m_state;

    if (response.is_eof())
    {
        new_state = State::Done;
    }
    else if (!response.is_data())
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

PacketTracker::State PacketTracker::expect_no_more(const ComResponse& response)
{
    MXS_SERROR("PacketTracker unexpected " << response.type() << " in state " << m_state);
    return State::Error;
}
}
