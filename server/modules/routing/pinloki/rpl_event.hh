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

#pragma once

#include "maria_rpl_event.hh"

namespace maxsql
{
/** The following are essentially the same as events in mariadb_rpl.h. Simple
 *  as they are, there is not much code, and they are more suitable for pinloki.
 *  The other way to do this, would be to modify mariadb_rpl to provide the same
 *  (from a raw buffer). Just a matter of splitting up mariadb_rpl_fetch()
 *  into two functions.
 */
struct Rotate
{
    bool        is_fake;
    bool        is_artifical;
    std::string file_name;
};

struct GtidEvent
{
    GtidEvent(const Gtid& gtid, uint8_t flags, uint64_t commit_id)
        : gtid(gtid)
        , flags(flags)
        , commit_id(commit_id)
    {
    }
    Gtid     gtid;
    uint8_t  flags;
    uint64_t commit_id = 0;
};

struct GtidListEvent
{
    GtidListEvent(const std::vector<Gtid>&& gl)
        : gtid_list(std::move(gl))
    {
    }

    GtidList gtid_list;
};

class RplEvent
{
public:
    RplEvent() = default;   // => is_empty() == true
    RplEvent(RplEvent&&) = default;
    RplEvent& operator=(RplEvent&&) = default;

    /**
     * @brief RplEvent
     * @param raw - the full buffer: header and data
     */
    RplEvent(std::vector<char>&& raw);

    static int get_event_length(const std::vector<char>& header);

    auto is_empty() const
    {
        return m_raw.empty();
    }

    explicit operator bool() const
    {
        return !is_empty();
    }

    Rotate        rotate() const;
    GtidEvent     gtid_event() const;
    GtidListEvent gtid_list() const;

    auto event_type() const
    {
        return m_event_type;
    }
    auto timestamp() const
    {
        return m_timestamp;
    }
    auto server_id() const
    {
        return m_server_id;
    }
    auto event_length() const
    {
        return m_event_length;
    }
    auto next_event_pos() const
    {
        return m_next_event_pos;
    }
    auto flags() const
    {
        return m_flags;
    }
    auto checksum() const
    {
        return m_checksum;
    }

    auto pHeader() const
    {
        return &m_raw[0];
    }

    auto pBody() const
    {
        return &m_raw[RPL_HEADER_LEN];
    }

    auto pEnd() const
    {
        auto ret = &m_raw.back();
        return ++ret;
    }

    const std::vector<char>& data() const
    {
        return m_raw;
    }

private:
    mariadb_rpl_event m_event_type;
    unsigned int      m_timestamp;
    unsigned int      m_server_id;
    unsigned int      m_event_length;
    long              m_next_event_pos;
    unsigned short    m_flags;
    std::vector<char> m_raw;
    unsigned int      m_checksum;
};

// TODO, turn this into an iterator. Used in find_gtid, but not yet in file_reader.
maxsql::RplEvent read_event(std::istream& file, long* file_pos);

std::string   dump_rpl_msg(const RplEvent& rpl_event, Verbosity v);
std::ostream& operator<<(std::ostream& os, const RplEvent& rpl_msg);        // Verbosity::All
}
