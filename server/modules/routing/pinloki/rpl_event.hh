/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "maria_rpl_event.hh"
#include <cstring>

namespace maxsql
{
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

    /**
     * @brief RplEvent from a MariaRplEvent
     * @param MariaRplEvent
     */
    RplEvent(MariaRplEvent&& maria_event);

    /**
     * @brief RplEvent from a raw buffer
     * @param raw - the full buffer: header and data
     */
    explicit RplEvent(std::vector<char>&& raw);

    RplEvent(RplEvent&& rhs);
    RplEvent& operator=(RplEvent&& rhs);

    bool     is_empty() const;
    explicit operator bool() const;

    Rotate        rotate() const;
    GtidEvent     gtid_event() const;
    GtidListEvent gtid_list() const;
    bool          is_commit() const;

    mariadb_rpl_event event_type() const;
    unsigned int      timestamp() const;
    unsigned int      server_id() const;
    unsigned int      event_length() const;
    uint32_t          next_event_pos() const;
    unsigned short    flags() const;
    unsigned int      checksum() const;

    const char* pBuffer() const;
    size_t      buffer_size() const;
    const char* pHeader() const;
    const char* pBody() const;
    const char* pEnd() const;

    void       set_next_pos(uint32_t next_pos);
    static int get_event_length(const std::vector<char>& header);

private:
    void        init();
    void        recalculate_crc();
    std::string query_event_sql() const;

    // Underlying is either MariaRplEvent or raw data (or neither)
    MariaRplEvent     m_maria_rpl;
    std::vector<char> m_raw;

    mariadb_rpl_event m_event_type;
    unsigned int      m_timestamp;
    unsigned int      m_server_id;
    unsigned int      m_event_length;
    uint32_t          m_next_event_pos;
    unsigned short    m_flags;
    unsigned int      m_checksum;
};

inline bool operator==(const RplEvent& lhs, const RplEvent& rhs)
{
    return lhs.buffer_size() == rhs.buffer_size()
           && std::memcmp(lhs.pBuffer(), rhs.pBuffer(), lhs.buffer_size()) == 0;
}


// TODO, turn this into an iterator. Used in find_gtid, but not yet in file_reader.
maxsql::RplEvent read_event(std::istream& file, long* file_pos);

enum class Kind {Real, Artificial};

std::vector<char> create_rotate_event(const std::string& file_name,
                                      uint32_t server_id,
                                      uint32_t pos,
                                      Kind kind);

std::vector<char> create_binlog_checkpoint(const std::string& file_name, uint32_t server_id,
                                           uint32_t curr_pos);

enum class Verbosity {Name, Some, All};
std::string   dump_rpl_msg(const RplEvent& rpl_event, Verbosity v);
std::ostream& operator<<(std::ostream& os, const RplEvent& rpl_msg);        // Verbosity::All

inline RplEvent::operator bool() const
{
    return !is_empty();
}

inline mariadb_rpl_event RplEvent::event_type() const
{
    return m_event_type;
}

inline unsigned int RplEvent::timestamp() const
{
    return m_timestamp;
}

inline unsigned int RplEvent::server_id() const
{
    return m_server_id;
}

inline unsigned int RplEvent::event_length() const
{
    return m_event_length;
}

inline uint32_t RplEvent::next_event_pos() const
{
    return m_next_event_pos;
}

inline unsigned short RplEvent::flags() const
{
    return m_flags;
}

inline unsigned int RplEvent::checksum() const
{
    return m_checksum;
}
}

std::string to_string(mariadb_rpl_event ev);
