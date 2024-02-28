/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rpl_event.hh"
#include "dbconnection.hh"

#include <maxscale/protocol/mariadb/mysql.hh>

#include <zlib.h>
#include <chrono>
#include <iostream>
#include <iomanip>

#include <openssl/rand.h>
#include <maxscale/utils.hh>
#include <maxscale/key_manager.hh>

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace
{

// Offset of the event length in the binlog event header
constexpr int RPL_EV_LEN_OFFSET = 9;

std::string get_rotate_name(const char* ptr, size_t len)
{
    // 19 byte header and 8 bytes of constant data
    // see: https://mariadb.com/kb/en/rotate_event/
    const size_t NAME_OFFSET = 19 + 8;
    auto given = std::string(ptr + NAME_OFFSET, len - NAME_OFFSET);

    // This is a very uncomfortable hack around the lack of checksum information we have at this point.
    // Deducing whether checksums are enabled by calculating it and comparing it to the stored checksum works
    // in most cases but we can't be sure whether there are edge cases where the valid checksum of the start
    // of the event results in a checksum that matches the last four bytes of it.
    uint32_t orig_checksum = mariadb::get_byte4((const uint8_t*)ptr + len - 4);
    uint32_t checksum = crc32(0, (const uint8_t*)ptr, len - 4);

    if (orig_checksum == checksum)
    {
        given = given.substr(0, given.length() - 4);
    }

    return given;
}

std::vector<char> create_header(uint32_t payload_len,
                                uint32_t ts,
                                uint8_t event_type,
                                uint32_t server_id,
                                uint32_t next_pos,
                                uint16_t flags)
{

    std::vector<char> data(mxq::RPL_HEADER_LEN + payload_len);
    uint8_t* ptr = (uint8_t*)&data[0];

    // Timestamp
    mariadb::set_byte4(ptr, ts);
    ptr += 4;

    // Event type
    *ptr++ = event_type;

    // server_id
    mariadb::set_byte4(ptr, server_id);
    ptr += 4;

    // Event length
    mariadb::set_byte4(ptr, data.size());
    ptr += 4;

    // Next pos
    mariadb::set_byte4(ptr, next_pos);
    ptr += 4;

    // Flags
    mariadb::set_byte2(ptr, flags);
    ptr += 2;

    return data;
}
}


namespace maxsql
{

uint32_t RplEvent::get_event_length(const std::vector<char>& header)
{
    return mariadb::get_byte4(reinterpret_cast<const uint8_t*>(header.data()) + 4 + 1 + 4);
}

RplEvent::RplEvent(MariaRplEvent&& maria_event)
    : m_maria_rpl(std::move(maria_event))
{
    if (!m_maria_rpl.is_empty())
    {
        init();
    }
}

RplEvent::RplEvent(std::vector<char>&& raw)
    : RplEvent(std::move(raw), raw.size())
{
}

RplEvent::RplEvent(std::vector<char>&& raw, size_t real_size)
    : m_raw(std::move(raw))
    , m_real_size(real_size)
{
    if (!m_raw.empty())
    {
        init();
    }
}

RplEvent::RplEvent(size_t sz)
    : m_raw(sz)
{
}

const char* RplEvent::pBuffer() const
{
    if (!m_maria_rpl.is_empty())
    {
        return m_maria_rpl.raw_data();
    }
    else
    {
        return m_raw.data();
    }
}

size_t RplEvent::buffer_size() const
{
    if (!m_maria_rpl.is_empty())
    {
        return m_maria_rpl.raw_data_size();
    }
    else
    {
        return m_raw.size();
    }
}

bool maxsql::RplEvent::is_empty() const
{
    return m_maria_rpl.is_empty() && m_raw.empty();
}

void RplEvent::init(bool with_body)
{
    auto buf = reinterpret_cast<const uint8_t*>(pBuffer());

    m_timestamp = mariadb::get_byte4(buf);
    buf += 4;
    m_event_type = mariadb_rpl_event(*buf);
    buf += 1;
    m_server_id = mariadb::get_byte4(buf);
    buf += 4;
    m_event_length = mariadb::get_byte4(buf);
    buf += 4;
    m_next_event_pos = mariadb::get_byte4(buf);
    buf += 4;
    m_flags = mariadb::get_byte2(buf);

    if (with_body)
    {
        if (!m_real_size)
        {
            m_real_size = buffer_size();
        }

        auto pCrc = reinterpret_cast<const uint8_t*>(pEnd() - 4);
        m_checksum = mariadb::get_byte4(pCrc);
    }
}

void RplEvent::set_next_pos(uint32_t next_pos)
{
    m_next_event_pos = next_pos;

    auto buf = reinterpret_cast<const uint8_t*>(pBuffer() + 4 + 1 + 4 + 4);
    mariadb::set_byte4(const_cast<uint8_t*>(buf), m_next_event_pos);

    recalculate_crc();
}

size_t RplEvent::real_size() const
{
    return m_real_size;
}

void RplEvent::set_real_size(size_t size)
{
    m_real_size = size;
}

void RplEvent::recalculate_crc()
{
    auto crc_pos = (uint8_t*) pEnd() - 4;
    m_checksum = crc32(0, (uint8_t*) pBuffer(), buffer_size() - 4);
    mariadb::set_byte4(crc_pos, m_checksum);
}

Rotate RplEvent::rotate() const
{
    Rotate rot;
    rot.is_fake = m_timestamp == 0;
    rot.is_artifical = m_flags & LOG_EVENT_ARTIFICIAL_F;
    rot.file_name = get_rotate_name(pBuffer(), buffer_size());

    return rot;
}

FormatDescription RplEvent::format_description() const
{
    FormatDescription fde;
    // The checksum algorithm is the second to last field and the last field is a 4 byte checksum
    fde.checksum = pBuffer()[buffer_size() - 4 - 1];

    // string<50> The MariaDB server version (example: 10.2.1-debug-log), padded with 0x00 bytes on the right.
    memcpy(fde.server_version.data(), pBuffer() + RPL_HEADER_LEN + 2, 50);
    return fde;
}

bool RplEvent::is_commit() const
{
    return strcasecmp(query_event_sql().c_str(), "COMMIT") == 0;
}

const char* RplEvent::pHeader() const
{
    return pBuffer();
}

const char* RplEvent::pBody() const
{
    return pHeader() + RPL_HEADER_LEN;
}

const char* RplEvent::pEnd() const
{
    return pBuffer() + buffer_size();
}

std::string RplEvent::query_event_sql() const
{
    // FIXME move into is_commit(), only needed there
    std::string sql;

    if (event_type() == QUERY_EVENT)
    {
        constexpr int DBNM_OFF = 4 + 4;                     // Database name offset
        constexpr int VBLK_OFF = 4 + 4 + 1 + 2;             // Varblock offset
        constexpr int FIXED_DATA_LEN = 4 + 4 + 1 + 2 + 2;   // Fixed data length of query event
        constexpr int CRC_LEN = 4;

        const uint8_t* ptr = (const uint8_t*) pBody();
        int dblen = ptr[DBNM_OFF];
        int vblklen = mariadb::get_byte2(ptr + VBLK_OFF);

        size_t data_len = pEnd() - pBody();
        size_t sql_offs = FIXED_DATA_LEN + vblklen + 1 + dblen;
        int sql_len = data_len - sql_offs - CRC_LEN;
        sql.assign((const char*) ptr + sql_offs, sql_len);
    }

    return sql;
}

std::ostream& operator<<(std::ostream& os, const Rotate& rot)
{
    os << rot.file_name << "  is_ariticial=" << rot.is_artifical << "  is_fake=" << rot.is_fake;
    return os;
}

GtidEvent RplEvent::gtid_event() const
{
    const uint8_t* dptr = (const uint8_t*)pBody();

    auto sequence_nr = mariadb::get_byte8(dptr);
    dptr += 8;
    auto domain_id = mariadb::get_byte4(dptr);
    dptr += 4;
    uint8_t flags = *dptr;
    dptr += 1;

    uint64_t commit_id = 0;
    if (flags & FL_GROUP_COMMIT_ID)
    {
        commit_id = mariadb::get_byte8(dptr);
    }

    return GtidEvent({domain_id, m_server_id, sequence_nr}, flags, commit_id);
}

std::ostream& operator<<(std::ostream& os, const GtidEvent& ev)
{
    os << ev.gtid;
    return os;
}

GtidListEvent RplEvent::gtid_list() const
{
    auto dptr = (const uint8_t*)pBody();

    std::vector<Gtid> gtids;
    uint32_t count = mariadb::get_byte4(dptr);
    dptr += 4;
    for (uint32_t i = 0; i < count; ++i)
    {
        auto domain_id = mariadb::get_byte4(dptr);
        dptr += 4;
        auto server_id = mariadb::get_byte4(dptr);
        dptr += 4;
        auto sequence_nr = mariadb::get_byte8(dptr);
        dptr += 8;
        gtids.push_back({domain_id, server_id, sequence_nr});
    }
    return GtidListEvent(std::move(gtids));
}

StartEncryptionEvent RplEvent::start_encryption_event() const
{
    StartEncryptionEvent event;
    const uint8_t* ptr = (uint8_t*)pBody();

    // START_ENCRYPTION_EVENT:
    //
    // key schema  [1]
    // key version [4]
    // IV          [12]

    event.key_version = mariadb::get_byte4(ptr + 1);

    // Store the 12 byte IV in the event at an offset of 4 bytes: the first part will be substituted with the
    // current position of each encrypted binlog event.
    memcpy(event.iv.data() + 4, ptr + 5, 12);

    return event;
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

RplEvent RplEvent::read_event(pinloki::IFStreamReader& file, long* file_pos)
{
    RplEvent rpl = read_header_only(file, file_pos);
    if (rpl)
    {
        rpl.read_body(file, file_pos);
    }

    return rpl;
}

RplEvent RplEvent::read_header_only(pinloki::IFStreamReader& file, long* file_pos)
{
    RplEvent rpl(RPL_HEADER_LEN);

    if (!file.read_n(rpl.m_raw.data(), RPL_HEADER_LEN))
    {
        return rpl;
    }

    rpl.init(false);

    *file_pos += RPL_HEADER_LEN;

    return rpl;
}

bool RplEvent::read_body(pinloki::IFStreamReader& file, long* file_pos)
{
    mxb_assert(m_maria_rpl.is_empty());

    auto event_length = maxsql::RplEvent::get_event_length(m_raw);

    m_raw.resize(event_length);
    if (!file.read_n(m_raw.data() + RPL_HEADER_LEN, event_length - RPL_HEADER_LEN))
    {
        m_raw.resize(RPL_HEADER_LEN);
        file.put_back(std::move(m_raw));
        return false;
    }

    auto pCrc = reinterpret_cast<const uint8_t*>(pEnd() - 4);
    m_checksum = mariadb::get_byte4(pCrc);

    *file_pos = next_event_pos();

    m_real_size = m_raw.size();

    return true;
}

mxq::RplEvent RplEvent::read_event(pinloki::IFStreamReader& file, const std::unique_ptr<mxq::EncryptCtx>& enc)
{
    std::vector<char> raw(RPL_HEADER_LEN);

    long pos = 0;

    pos = file.bytes_read();

    if (!file.read_n(raw.data(), RPL_HEADER_LEN))
    {
        return mxq::RplEvent();
    }

    auto event_length = maxsql::RplEvent::get_event_length(raw);

    raw.resize(event_length);

    if (!file.read_n(raw.data() + RPL_HEADER_LEN, event_length - RPL_HEADER_LEN))
    {
        raw.resize(RPL_HEADER_LEN);
        file.put_back(std::move(raw));
        return mxq::RplEvent();
    }

    if (enc)
    {
        raw = enc->decrypt_event(raw, pos);
    }

    return mxq::RplEvent(std::move(raw), event_length);
}

std::ostream& operator<<(std::ostream& os, const RplEvent& rpl_msg)
{
    os << dump_rpl_msg(rpl_msg, Verbosity::All);
    return os;
}

std::vector<char> create_rotate_event(const std::string& file_name,
                                      uint32_t server_id,
                                      uint32_t pos,
                                      Kind kind)
{
    std::vector<char> data = create_header(file_name.size() + 12, 0, ROTATE_EVENT, server_id, pos,
                                           kind == Kind::Artificial ? LOG_EVENT_ARTIFICIAL_F : 0);
    uint8_t* ptr = (uint8_t*)data.data() + RPL_HEADER_LEN;

    // PAYLOAD
    // The position in the new file. Always sizeof magic.
    mariadb::set_byte8(ptr, 4);
    ptr += 8;

    // The binlog name  (not null-terminated)
    memcpy(ptr, file_name.c_str(), file_name.size());
    ptr += file_name.size();

    // Checksum of the whole event
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    return data;
}

std::vector<char> create_start_encryption_event(uint32_t server_id, uint32_t key_version,
                                                uint32_t current_pos)
{
    const uint32_t PAYLOAD_LEN = 1 + 4 + 12 + 4;
    uint32_t next_pos = current_pos + RPL_HEADER_LEN + PAYLOAD_LEN;
    std::vector<char> data = create_header(PAYLOAD_LEN, 0, START_ENCRYPTION_EVENT, server_id, next_pos, 0);
    uint8_t* ptr = (uint8_t*)data.data() + RPL_HEADER_LEN;

    // PAYLOAD
    // Encryption scheme, always 1 for binlogs
    *ptr++ = 1;

    // Key version
    mariadb::set_byte4(ptr, key_version);
    ptr += 4;

    // 12 byte nonce
    int rc = RAND_bytes(ptr, 12);

    if (rc != 1)
    {
        MXB_THROW(mxq::EncryptionError, "Failed to create random nonce.");
    }

    ptr += 12;

    // Checksum of the whole event
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    return data;
}

std::vector<char> EncryptCtx::decrypt_event(std::vector<char> input, uint32_t pos)
{
    std::vector<char> output(input.size());
    uint8_t* in = reinterpret_cast<uint8_t*>(input.data());
    uint8_t* out = reinterpret_cast<uint8_t*>(output.data());

    // Store the event length and replace it with the encrypted timestamp
    uint32_t len = mariadb::get_byte4(in + RPL_EV_LEN_OFFSET);
    mariadb::set_byte4(in + RPL_EV_LEN_OFFSET, mariadb::get_byte4(in));

    // The first four bytes of the IV are the current event position
    mariadb::set_byte4(iv.data(), pos);

    int output_len = 0;
    bool ok = cipher.decrypt(key.data(), iv.data(), in + 4, input.size() - 4, out + 4, &output_len);

    if (!ok)
    {
        MXB_THROW(EncryptionError, "Failed to decrypt event: " << cipher.get_errors());
    }

    // Restore the unencrypted length in the correct place
    mariadb::set_byte4(out, mariadb::get_byte4(out + RPL_EV_LEN_OFFSET));
    mariadb::set_byte4(out + RPL_EV_LEN_OFFSET, output_len + 4);
    output.resize(output_len + 4);
    return output;
}

std::vector<char> EncryptCtx::encrypt_event(std::vector<char> input, uint32_t pos)
{
    // The length isn't encrypted which means we subtract that from the overall length. However, we still need
    // to store it so we add it back into the resulting length once we know how long the encrypted data is.
    // This means that for example with AES-CBC, the data is always multiple of 16 + 4 bytes length.
    size_t enc_len = cipher.encrypted_size(input.size() - 4) + 4;
    std::vector<char> output(enc_len);
    uint8_t* in = reinterpret_cast<uint8_t*>(input.data());
    uint8_t* out = reinterpret_cast<uint8_t*>(output.data());

    // Replace the event length with the timestamp
    MXB_AT_DEBUG(uint32_t len = mariadb::get_byte4(in + RPL_EV_LEN_OFFSET));
    uint32_t timestamp = mariadb::get_byte4(in);
    mariadb::set_byte4(in + RPL_EV_LEN_OFFSET, timestamp);

    // The first four bytes of the IV are the current event position
    mariadb::set_byte4(iv.data(), pos);

    int output_len = 0;
    bool ok = cipher.encrypt(key.data(), iv.data(), in + 4, input.size() - 4, out + 4, &output_len);

    if (!ok)
    {
        MXB_THROW(EncryptionError, "Failed to encrypt event: " << cipher.get_errors());
    }

    // Move the timestamp back to the correct place and store the encrypted length in event length offset
    mariadb::set_byte4(out, mariadb::get_byte4(out + RPL_EV_LEN_OFFSET));
    mariadb::set_byte4(out + RPL_EV_LEN_OFFSET, output_len + 4);

    MXB_AT_DEBUG(mariadb::set_byte4(in, timestamp));
    MXB_AT_DEBUG(mariadb::set_byte4(in + RPL_EV_LEN_OFFSET, len));
    mxb_assert(decrypt_event(output, pos) == input);

    return output;
}

std::unique_ptr<mxq::EncryptCtx> create_encryption_ctx(const std::string& key_id,
                                                       mxb::Cipher::AesMode cipher,
                                                       const std::string& filename,
                                                       const mxq::RplEvent& event)
{
    std::unique_ptr<mxq::EncryptCtx> rval;

    if (key_id.empty())
    {
        MXB_THROW(EncryptionError,
                  "Encrypted binlog '" << filename <<
                  "' found but 'encryption_key_id' is not configured");
    }
    else if (auto key_manager = mxs::key_manager())
    {
        auto start_encryption = event.start_encryption_event();
        auto [ok, version, key] = key_manager->get_key(key_id, start_encryption.key_version);

        if (!ok)
        {
            MXB_THROW(EncryptionError,
                      "Version " << start_encryption.key_version
                                 << " of key '" << key_id
                                 << "' was not found, cannot "
                                 << "open encrypted binlog '" << filename << "'");
        }

        rval = std::make_unique<mxq::EncryptCtx>(cipher, key, start_encryption.iv);
    }
    else
    {
        MXB_THROW(EncryptionError,
                  "Encrypted binlog '" << filename << "' found but key manager is not configured");
    }

    return rval;
}
}

std::string to_string(mariadb_rpl_event ev)
{
    switch (ev)
    {
    case START_EVENT_V3:
        return "START_EVENT_V3";

    case QUERY_EVENT:
        return "QUERY_EVENT";

    case STOP_EVENT:
        return "STOP_EVENT";

    case ROTATE_EVENT:
        return "ROTATE_EVENT";

    case INTVAR_EVENT:
        return "INTVAR_EVENT";

    case LOAD_EVENT:
        return "LOAD_EVENT";

    case SLAVE_EVENT:
        return "SLAVE_EVENT";

    case CREATE_FILE_EVENT:
        return "CREATE_FILE_EVENT";

    case APPEND_BLOCK_EVENT:
        return "APPEND_BLOCK_EVENT";

    case EXEC_LOAD_EVENT:
        return "EXEC_LOAD_EVENT";

    case DELETE_FILE_EVENT:
        return "DELETE_FILE_EVENT";

    case NEW_LOAD_EVENT:
        return "NEW_LOAD_EVENT";

    case RAND_EVENT:
        return "RAND_EVENT";

    case USER_VAR_EVENT:
        return "USER_VAR_EVENT";

    case FORMAT_DESCRIPTION_EVENT:
        return "FORMAT_DESCRIPTION_EVENT";

    case XID_EVENT:
        return "XID_EVENT";

    case BEGIN_LOAD_QUERY_EVENT:
        return "BEGIN_LOAD_QUERY_EVENT";

    case EXECUTE_LOAD_QUERY_EVENT:
        return "EXECUTE_LOAD_QUERY_EVENT";

    case TABLE_MAP_EVENT:
        return "TABLE_MAP_EVENT";

    case PRE_GA_WRITE_ROWS_EVENT:
        return "PRE_GA_WRITE_ROWS_EVENT";

    case PRE_GA_UPDATE_ROWS_EVENT:
        return "PRE_GA_UPDATE_ROWS_EVENT";

    case PRE_GA_DELETE_ROWS_EVENT:
        return "PRE_GA_DELETE_ROWS_EVENT";

    case WRITE_ROWS_EVENT_V1:
        return "WRITE_ROWS_EVENT_V1";

    case UPDATE_ROWS_EVENT_V1:
        return "UPDATE_ROWS_EVENT_V1";

    case DELETE_ROWS_EVENT_V1:
        return "DELETE_ROWS_EVENT_V1";

    case INCIDENT_EVENT:
        return "INCIDENT_EVENT";

    case HEARTBEAT_LOG_EVENT:
        return "HEARTBEAT_LOG_EVENT";

    case IGNORABLE_LOG_EVENT:
        return "IGNORABLE_LOG_EVENT";

    case ROWS_QUERY_LOG_EVENT:
        return "ROWS_QUERY_LOG_EVENT";

    case WRITE_ROWS_EVENT:
        return "WRITE_ROWS_EVENT";

    case UPDATE_ROWS_EVENT:
        return "UPDATE_ROWS_EVENT";

    case DELETE_ROWS_EVENT:
        return "DELETE_ROWS_EVENT";

    case GTID_LOG_EVENT:
        return "GTID_LOG_EVENT";

    case ANONYMOUS_GTID_LOG_EVENT:
        return "ANONYMOUS_GTID_LOG_EVENT";

    case PREVIOUS_GTIDS_LOG_EVENT:
        return "PREVIOUS_GTIDS_LOG_EVENT";

    case TRANSACTION_CONTEXT_EVENT:
        return "TRANSACTION_CONTEXT_EVENT";

    case VIEW_CHANGE_EVENT:
        return "VIEW_CHANGE_EVENT";

    case XA_PREPARE_LOG_EVENT:
        return "XA_PREPARE_LOG_EVENT";

    case ANNOTATE_ROWS_EVENT:
        return "ANNOTATE_ROWS_EVENT";

    case BINLOG_CHECKPOINT_EVENT:
        return "BINLOG_CHECKPOINT_EVENT";

    case GTID_EVENT:
        return "GTID_EVENT";

    case GTID_LIST_EVENT:
        return "GTID_LIST_EVENT";

    case START_ENCRYPTION_EVENT:
        return "START_ENCRYPTION_EVENT";

    case QUERY_COMPRESSED_EVENT:
        return "QUERY_COMPRESSED_EVENT";

    case WRITE_ROWS_COMPRESSED_EVENT_V1:
        return "WRITE_ROWS_COMPRESSED_EVENT_V1";

    case UPDATE_ROWS_COMPRESSED_EVENT_V1:
        return "UPDATE_ROWS_COMPRESSED_EVENT_V1";

    case DELETE_ROWS_COMPRESSED_EVENT_V1:
        return "DELETE_ROWS_COMPRESSED_EVENT_V1";

    case WRITE_ROWS_COMPRESSED_EVENT:
        return "WRITE_ROWS_COMPRESSED_EVENT";

    case UPDATE_ROWS_COMPRESSED_EVENT:
        return "UPDATE_ROWS_COMPRESSED_EVENT";

    case DELETE_ROWS_COMPRESSED_EVENT:
        return "DELETE_ROWS_COMPRESSED_EVENT";

    default:
        return "UNKNOWN_EVENT";
    }

    abort();
}
