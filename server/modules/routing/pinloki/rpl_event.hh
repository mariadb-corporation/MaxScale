/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "maria_rpl_event.hh"
#include "ifstream_reader.hh"
#include <cstring>
#include <array>

#include <maxbase/secrets.hh>
#include <maxbase/exception.hh>
#include <memory>

namespace maxsql
{

DEFINE_EXCEPTION(EncryptionError);

struct EncryptCtx;

struct FormatDescription
{
    std::array<char, 50> server_version;
    bool                 checksum;
};

struct Rotate
{
    bool        is_fake;
    bool        is_artificial;
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

struct StartEncryptionEvent
{
    uint32_t                key_version;
    std::array<uint8_t, 16> iv;
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

    /**
     * @brief RplEvent from a raw buffer
     *
     * @param raw       The in-memory data of the event
     * @param real_size The size on disk that this event would take
     */
    explicit RplEvent(std::vector<char>&& raw, size_t real_size);

    RplEvent(RplEvent&& rhs) = default;
    RplEvent& operator=(RplEvent&& rhs) = default;

    bool     is_empty() const;
    explicit operator bool() const;

    /**
     * @brief read_event
     * @param file       - file to read from
     * @param *file_pos  - file position to start reading from,
     *                     and set to the next position to read from.
     *
     * @return RplEvent. If there was not enough data to read
     *                   is_empty() == true and *file_pos is unchanged.
     */
    static RplEvent read_event(pinloki::IFStreamReader& file, long* file_pos);

    /**
     * Reads one event and decrypts it if needed
     *
     * @param file File to read from
     * @param enc  Encryption context to use
     *
     * @return The event if one was successfully read
     */
    static RplEvent read_event(pinloki::IFStreamReader& file, const std::unique_ptr<mxq::EncryptCtx>& enc);

    /**
     * @brief  read_header_only. Use read_body() to get the full event.
     * @param  file      - to read from
     * @param *file_pos  - file position to start reading from, and set to
     *                     the position where the body should be read from.
     *                     If the intention is to read the next header, it
     *                     is at position next_event_pos().
     * @return RplEvent. If there was not enough data to read
     *                   is_empty() == true and *file_pos is unchanged.
     */
    static RplEvent read_header_only(pinloki::IFStreamReader& file, long* file_pos);

    /* Functions that are valid after the header has been read. */
    const char* pBuffer() const;
    size_t      buffer_size() const;
    const char* pHeader() const;
    const char* pEnd() const;

    /**
     * The real length of the event, including any overhead added by encryption
     *
     * @return The size in bytes that this event occupied when stored on disk. If the event was replicated or
     *         it was stored unencrypted, the return value is the same as buffer_size(). For encrypted events
     *         this value might be slightly larger than the logical size of the event.
     */
    size_t real_size() const;

    mariadb_rpl_event event_type() const;
    unsigned int      timestamp() const;
    unsigned int      server_id() const;
    unsigned int      event_length() const;
    uint32_t          next_event_pos() const;
    unsigned short    flags() const;

    /**
     * @brief read_body  - completes the event when only the header was read.
     *                    No effect if the body has already been read.
     * @param file       - file to read from
     * @param *file_pos  - file position to start reading from, and set
     *                     to the next position to read an event from.
     *
     * @return true if the body could be read. NOTE: if the body could not be read
     *              this instance is invalidated and is_empty() will return true.
     *
     */
    bool read_body(pinloki::IFStreamReader&, long* file_pos);

    /** Functions that are valid after the body has been read */
    const char*  pBody() const;
    unsigned int checksum() const;

    Rotate               rotate() const;
    GtidEvent            gtid_event() const;
    GtidListEvent        gtid_list() const;
    StartEncryptionEvent start_encryption_event() const;
    FormatDescription format_description() const;
    bool                 is_commit() const;

    /** For the writer */
    void            set_next_pos(uint32_t next_pos);
    static uint32_t get_event_length(const std::vector<char>& header);
    void            set_real_size(size_t size);

private:
    // Initialize the raw buffer to size sz. Used with read_header_only()
    // Note that the event will not be is_empty() after this call.
    RplEvent(size_t sz);

    void        init(bool with_body = true);
    void        recalculate_crc();
    std::string query_event_sql() const;

    // Underlying is either MariaRplEvent or raw data (or neither)
    MariaRplEvent     m_maria_rpl;
    std::vector<char> m_raw;

    size_t            m_real_size{0};
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

// Encryption context for handling encrypted binlogs
struct EncryptCtx
{
    EncryptCtx(mxb::Cipher::AesMode mode,
               const std::vector<uint8_t>& enc_key,
               const std::array<uint8_t, 16>& enc_iv)
        : cipher(mode, enc_key.size() * 8)
        , key(enc_key)
        , iv(enc_iv)
    {
    }

    mxb::Cipher             cipher;
    std::vector<uint8_t>    key;
    std::array<uint8_t, 16> iv;

    std::vector<char> decrypt_event(std::vector<char> input, uint32_t pos);
    std::vector<char> encrypt_event(std::vector<char> input, uint32_t pos);
};

enum class Kind {Real, Artificial};

std::vector<char> create_rotate_event(const std::string& file_name,
                                      uint32_t server_id,
                                      uint32_t pos,
                                      Kind kind);

std::vector<char> create_start_encryption_event(uint32_t server_id, uint32_t key_version,
                                                uint32_t current_pos);

std::unique_ptr<mxq::EncryptCtx> create_encryption_ctx(const std::string& key_id,
                                                       mxb::Cipher::AesMode cipher,
                                                       const std::string& filename,
                                                       const mxq::RplEvent& event);

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
