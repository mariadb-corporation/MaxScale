/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rpl_event.hh"
#include "dbconnection.hh"

#include <chrono>
#include <iostream>
#include <iomanip>

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace maxsql
{
constexpr int HEADER_LEN = 19;

int RplEvent::get_event_length(const std::vector<char>& header)
{
    return *((uint32_t*) (header.data() + 4 + 1 + 4));
}

RplEvent::RplEvent(std::vector<char>&& raw_)
    : m_raw(std::move(raw_))
{
    if (m_raw.empty())
    {
        return;
    }
    // todo use ntohxxx, or whatever is in maxscale,
    // or use mariadb_rpl (with modifications)

    auto dptr = pHeader();
    m_timestamp = *((uint32_t*) dptr);
    dptr += 4;
    m_event_type = mariadb_rpl_event(*((unsigned char*) dptr));
    dptr += 1;
    m_server_id = *((uint32_t*) dptr);
    dptr += 4;
    m_event_length = *((uint32_t*) dptr);
    dptr += 4;
    m_next_event_pos = *((uint32_t*) dptr);
    dptr += 4;
    m_flags = *((uint32_t*) dptr);
    dptr += 2;
    m_checksum = *((uint32_t*) (m_raw.data() + m_raw.size() - 4));
}

Rotate RplEvent::rotate() const
{
    Rotate rot;
    rot.is_fake = m_timestamp == 0;
    rot.is_artifical = m_flags & LOG_EVENT_ARTIFICIAL_F;
    rot.file_name = get_rotate_name(m_raw.data(), m_raw.size());

    return rot;
}

std::ostream& operator<<(std::ostream& os, const Rotate& rot)
{
    os << rot.file_name << "  is_ariticial=" << rot.is_artifical << "  is_fake=" << rot.is_fake;
    return os;
}

GtidEvent RplEvent::gtid_event() const
{
    auto dptr = pBody();

    auto sequence_nr = *((uint64_t*) dptr);
    dptr += 8;
    auto domain_id = *((uint32_t*) dptr);
    dptr += 4;
    auto flags = *((uint8_t*) dptr);
    dptr += 1;

    uint64_t commit_id = 0;
    if (flags & FL_GROUP_COMMIT_ID)
    {
        commit_id = *((uint64_t*) dptr);
    }

    return GtidEvent({domain_id, 0, sequence_nr}, flags, commit_id);
}

std::ostream& operator<<(std::ostream& os, const GtidEvent& ev)
{
    os << ev.gtid;
    return os;
}

GtidListEvent RplEvent::gtid_list() const
{
    auto dptr = pBody();

    std::vector<Gtid> gtids;
    uint32_t count = *((uint32_t*) dptr);
    dptr += 4;
    for (uint32_t i = 0; i < count; ++i)
    {
        auto domain_id = *((uint32_t*) dptr);
        dptr += 4;
        auto server_id = *((uint32_t*) dptr);
        dptr += 4;
        auto sequence_nr = *((uint64_t*) dptr);
        dptr += 8;
        gtids.push_back({domain_id, server_id, sequence_nr});
    }
    return GtidListEvent(std::move(gtids));
}

std::ostream& operator<<(std::ostream& os, const GtidListEvent& ev)
{
    os << ev.gtid_list;
    return os;
}

std::string dump_rpl_msg(const RplEvent& rpl_event, Verbosity v)
{
    std::ostringstream oss;

    oss << to_string(rpl_event.event_type()) << '\n';

    if (v == Verbosity::All)
    {
        oss << "  timestamp      " << rpl_event.timestamp() << '\n';
        oss << "  event_type      " << rpl_event.event_type() << '\n';
        oss << "  event_length   " << rpl_event.event_length() << '\n';
        oss << "  server_id      " << rpl_event.server_id() << '\n';
        oss << "  next_event_pos " << rpl_event.next_event_pos() << '\n';
        oss << "  flags          " << std::hex << "0x" << rpl_event.flags() << std::dec << '\n';
        oss << "  checksum       " << std::hex << "0x" << rpl_event.checksum() << std::dec << '\n';
    }

    switch (rpl_event.event_type())
    {
    case ROTATE_EVENT:
        {
            auto event = rpl_event.rotate();
            oss << event << '\n';
        }
        break;

    case GTID_EVENT:
        {
            auto event = rpl_event.gtid_event();
            oss << event << '\n';
        }
        break;

    case GTID_LIST_EVENT:
        {
            auto event = rpl_event.gtid_list();
            oss << event << '\n';
        }
        break;

    case FORMAT_DESCRIPTION_EVENT:
        break;

    default:
        // pass
        break;
    }

    return oss.str();
}

// TODO, turn this into an iterator. Use in file_reader as well.
maxsql::RplEvent read_event(std::istream& file, long* file_pos)
{
    std::vector<char> raw(HEADER_LEN);

    file.read(raw.data(), HEADER_LEN);
    if (file.tellg() != *file_pos + HEADER_LEN)
    {
        return maxsql::RplEvent();      // trying to read passed end of file
    }

    auto event_length = maxsql::RplEvent::get_event_length(raw);

    raw.resize(event_length);
    file.read(raw.data() + HEADER_LEN, event_length - HEADER_LEN);

    if (file.tellg() != *file_pos + event_length)
    {
        return maxsql::RplEvent();      // trying to read passed end of file
    }

    maxsql::RplEvent rpl(std::move(raw));

    *file_pos = rpl.next_event_pos();

    return rpl;
}


std::ostream& operator<<(std::ostream& os, const RplEvent& rpl_msg)
{
    os << dump_rpl_msg(rpl_msg, Verbosity::All);
    return os;
}
}
