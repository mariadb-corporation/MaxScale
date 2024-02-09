/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 */

#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>

#include <openssl/sha.h>
#include <maxbase/format.hh>
#include <maxbase/hexdump.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/service.hh>
#include <maxscale/target.hh>
#include <maxscale/utils.hh>
#include "packet_parser.hh"

#include <mysql.h>

using std::string;
using std::move;
using mxs::ReplyState;
using UserEntry = mariadb::UserEntry;

namespace
{
// Helper function for debug assertions
bool only_one_packet(const GWBUF& buffer)
{
    auto header = mariadb::get_header(buffer.data());
    auto buffer_len = buffer.length();
    return header.pl_length + MYSQL_HEADER_LEN == buffer_len;
}

bool MYSQL_IS_ERROR_PACKET(const uint8_t* header)
{
    return mariadb::get_command(header) == MYSQL_REPLY_ERR;
}

// Lookup table for the set of valid commands
constexpr std::array<bool, std::numeric_limits<uint8_t>::max()> create_command_lut()
{
    std::array<bool, std::numeric_limits<uint8_t>::max()> commands{};

    commands[MXS_COM_SLEEP] = true;
    commands[MXS_COM_QUIT] = true;
    commands[MXS_COM_INIT_DB] = true;
    commands[MXS_COM_QUERY] = true;
    commands[MXS_COM_FIELD_LIST] = true;
    commands[MXS_COM_CREATE_DB] = true;
    commands[MXS_COM_DROP_DB] = true;
    commands[MXS_COM_REFRESH] = true;
    commands[MXS_COM_SHUTDOWN] = true;
    commands[MXS_COM_STATISTICS] = true;
    commands[MXS_COM_PROCESS_INFO] = true;
    commands[MXS_COM_CONNECT] = true;
    commands[MXS_COM_PROCESS_KILL] = true;
    commands[MXS_COM_DEBUG] = true;
    commands[MXS_COM_PING] = true;
    commands[MXS_COM_TIME] = true;
    commands[MXS_COM_DELAYED_INSERT] = true;
    commands[MXS_COM_CHANGE_USER] = true;
    commands[MXS_COM_BINLOG_DUMP] = true;
    commands[MXS_COM_TABLE_DUMP] = true;
    commands[MXS_COM_CONNECT_OUT] = true;
    commands[MXS_COM_REGISTER_SLAVE] = true;
    commands[MXS_COM_STMT_PREPARE] = true;
    commands[MXS_COM_STMT_EXECUTE] = true;
    commands[MXS_COM_STMT_SEND_LONG_DATA] = true;
    commands[MXS_COM_STMT_CLOSE] = true;
    commands[MXS_COM_STMT_RESET] = true;
    commands[MXS_COM_SET_OPTION] = true;
    commands[MXS_COM_STMT_FETCH] = true;
    commands[MXS_COM_DAEMON] = true;
    commands[MXS_COM_UNSUPPORTED] = true;
    commands[MXS_COM_RESET_CONNECTION] = true;
    commands[MXS_COM_XPAND_REPL] = true;
    commands[MXS_COM_STMT_BULK_EXECUTE] = true;
    commands[MXS_COM_MULTI] = true;

    return commands;
}

constexpr const auto s_valid_commands = create_command_lut();

static_assert(s_valid_commands[MXS_COM_QUERY], "COM_QUERY should be valid");
static_assert(s_valid_commands[MXS_COM_PING], "COM_PING should be valid");
static_assert(!s_valid_commands[0x50], "0x50 should not be a valid command");

/**
 * Extract the SQL state from an error packet.
 *
 * @param pBuffer  Pointer to an error packet.
 * @param ppState  On return will point to the state in @c pBuffer.
 * @param pnState  On return the pointed to value will be 6.
 */
void extract_error_state(const uint8_t* pBuffer, const uint8_t** ppState, uint16_t* pnState)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state. In this context
    // the marker and the state itself are combined.
    *ppState = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    // The SQLSTATE is optional and, if present, starts with the hash sign
    *pnState = **ppState == '#' ? 6 : 0;
}

/**
 * Extract the message from an error packet.
 *
 * @param pBuffer    Pointer to an error packet.
 * @param ppMessage  On return will point to the start of the message in @c pBuffer.
 * @param pnMessage  On return the pointed to value will be the length of the message.
 */
void extract_error_message(const uint8_t* pBuffer, const uint8_t** ppMessage,
                           uint16_t* pnMessage)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    int packet_len = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(pBuffer);

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state, followed by
    // a message until the end of the packet.
    *ppMessage = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    *pnMessage = packet_len - MYSQL_HEADER_LEN - 1 - 2;

    if (**ppMessage == '#')     // The SQLSTATE is optional
    {
        (*ppMessage) += 6;
        (*pnMessage) -= 6;
    }
}
}

GWBUF mysql_create_com_quit()
{
    uint8_t packet[] = {0x1, 0x0, 0x0, 0x0, 0x1};
    return GWBUF(packet, sizeof(packet));
}

GWBUF mysql_create_custom_error(int packet_number, int affected_rows, uint16_t errnum, const char* errmsg)
{
    uint8_t mysql_packet_header[4];
    uint8_t field_count = 0xff;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    const char* mysql_state = "HY000";

    mariadb::set_byte2(mysql_err, errnum);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);


    uint32_t mysql_payload_size =
        sizeof(field_count)
        + sizeof(mysql_err)
        + sizeof(mysql_statemsg)
        + strlen(errmsg);

    /** allocate memory for packet header + payload */
    GWBUF errbuf(sizeof(mysql_packet_header) + mysql_payload_size);
    uint8_t* outbuf = errbuf.data();

    /** write packet header and packet number */
    mariadb::set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = packet_number;

    /** write header */
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    uint8_t* mysql_payload = outbuf + sizeof(mysql_packet_header);

    /** write field */
    memcpy(mysql_payload, &field_count, sizeof(field_count));
    mysql_payload = mysql_payload + sizeof(field_count);

    /** write errno */
    memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
    mysql_payload = mysql_payload + sizeof(mysql_err);

    /** write sqlstate */
    memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
    mysql_payload = mysql_payload + sizeof(mysql_statemsg);

    /** write error message */
    memcpy(mysql_payload, errmsg, strlen(errmsg));

    return errbuf;
}

// TODO: Collect all the protocol related utility functions in the same place, now they are
// spread out in multiple places.
size_t leint_prefix_bytes(size_t len)
{
    if (len < 251)
    {
        return 1;
    }
    else if (len < 0xffff)
    {
        return 3;
    }
    else if (len < 0xffffff)
    {
        return 4;
    }

    return 9;
}

void encode_leint(uint8_t* ptr, size_t prefix_size, size_t value)
{
    switch (prefix_size)
    {
    case 1:
        *ptr = value;
        break;

    case 3:
        *ptr++ = 0xfc;
        mariadb::set_byte2(ptr, value);
        break;

    case 4:
        *ptr++ = 0xfd;
        mariadb::set_byte3(ptr, value);
        break;

    case 9:
        *ptr++ = 0xfe;
        mariadb::set_byte8(ptr, value);
        break;

    default:
        mxb_assert(!true);
        break;
    }
}

uint8_t* mxs_mysql_calculate_hash(const uint8_t* scramble, const std::vector<uint8_t>& pw_sha1,
                                  uint8_t* output)
{
    const uint8_t* hash1 = pw_sha1.data();

    uint8_t hash2[SHA_DIGEST_LENGTH];   // SHA1(SHA1(password))
    SHA1(hash1, SHA_DIGEST_LENGTH, hash2);

    uint8_t new_sha[SHA_DIGEST_LENGTH];     // SHA1(CONCAT(scramble, hash2))
    gw_sha1_2_str(scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, SHA_DIGEST_LENGTH, new_sha);

    // Final token is SHA1(CONCAT(scramble, hash2)) ⊕ SHA1(password)
    mxs::bin_bin_xor(new_sha, hash1, SHA_DIGEST_LENGTH, output);
    return output + SHA_DIGEST_LENGTH;
}

bool mxs_mysql_is_valid_command(uint8_t command)
{
    return s_valid_commands[command];
}

bool mxs_mysql_is_ok_packet(const GWBUF& buffer)
{
    return buffer.length() > MYSQL_HEADER_LEN && buffer[MYSQL_HEADER_LEN] == MYSQL_REPLY_OK;
}

bool mxs_mysql_is_err_packet(const GWBUF& buffer)
{
    return buffer.length() > MYSQL_HEADER_LEN && buffer[MYSQL_HEADER_LEN] == MYSQL_REPLY_ERR;
}

uint16_t mxs_mysql_get_mysql_errno(const GWBUF& buffer)
{
    uint16_t rval = 0;

    if (mxs_mysql_is_err_packet(buffer))
    {
        rval = mariadb::get_byte2(buffer.data() + MYSQL_HEADER_LEN + 1);
    }

    return rval;
}

bool mxs_mysql_is_ps_command(uint8_t cmd)
{
    return cmd == MXS_COM_STMT_EXECUTE
           || cmd == MXS_COM_STMT_BULK_EXECUTE
           || cmd == MXS_COM_STMT_SEND_LONG_DATA
           || cmd == MXS_COM_STMT_CLOSE
           || cmd == MXS_COM_STMT_FETCH
           || cmd == MXS_COM_STMT_RESET;
}

uint32_t mxs_mysql_extract_ps_id(const GWBUF& buffer)
{
    uint32_t rval = 0;
    uint8_t id[MYSQL_PS_ID_SIZE];
    size_t sz = buffer.copy_data(MYSQL_PS_ID_OFFSET, sizeof(id), id);

    if (sz == sizeof(id))
    {
        rval = mariadb::get_byte4(id);
    }
    else
    {
        MXB_WARNING("Malformed binary protocol packet: %s",
                    mxb::hexdump(buffer.data(), buffer.length()).c_str());
        mxb_assert(false);
    }

    return rval;
}

bool mxs_mysql_command_will_respond(uint8_t cmd)
{
    return cmd != MXS_COM_STMT_SEND_LONG_DATA
           && cmd != MXS_COM_QUIT
           && cmd != MXS_COM_STMT_CLOSE;
}

bool MYSQL_session::ssl_capable() const
{
    return (client_caps.basic_capabilities & GW_MYSQL_CAPABILITIES_SSL) != 0;
}

std::string MYSQL_session::user_and_host() const
{
    return mxb::string_printf("'%s'@'%s'", auth_data->user.c_str(), remote.c_str());
}

bool MYSQL_session::will_respond(const GWBUF& buffer) const
{
    return mxs_mysql_command_will_respond(mariadb::get_command(buffer));
}

bool MYSQL_session::can_recover_state() const
{
    return m_history.can_recover_state();
}

bool MYSQL_session::are_multi_statements_allowed() const
{
    return (this->client_caps.basic_capabilities & GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS) != 0;
}

size_t MYSQL_session::amend_memory_statistics(json_t* memory) const
{
    size_t sescmd_history;
    size_t exec_metadata;
    size_t rv = get_size(&sescmd_history, &exec_metadata);

    json_object_set_new(memory, "sescmd_history", json_integer(sescmd_history));
    json_object_set_new(memory, "exec_metadata", json_integer(exec_metadata));

    return rv;
}

size_t MYSQL_session::static_size() const
{
    return sizeof(*this);
}

size_t MYSQL_session::varying_size() const
{
    return get_size(nullptr, nullptr);
}

size_t MYSQL_session::get_size(size_t* sescmd_history_size, size_t* exec_metadata_size) const
{
    size_t rv = 0;

    size_t sescmd_history = this->history().runtime_size();
    rv += sescmd_history;

    size_t exec_metadata = 0;
    for (const auto& kv : this->exec_metadata)
    {
        exec_metadata += sizeof(decltype(this->exec_metadata)::value_type);
        exec_metadata += kv.second.capacity();
    }

    rv += exec_metadata;

    if (sescmd_history_size)
    {
        *sescmd_history_size = sescmd_history;
    }

    if (exec_metadata_size)
    {
        *exec_metadata_size = exec_metadata;
    }

    return rv;
}

namespace mariadb
{
uint64_t AuthenticatorModule::capabilities() const
{
    return 0;
}

mariadb::AuthByteVec AuthenticatorModule::generate_token(std::string_view password)
{
    // Simply write the password as is. This works for PAM and GSSApi (in theory).
    return mariadb::AuthByteVec(password.begin(), password.end());
}

bool UserEntry::operator==(const UserEntry& rhs) const
{
    return username == rhs.username && host_pattern == rhs.host_pattern && plugin == rhs.plugin
           && password == rhs.password && auth_string == rhs.auth_string
           && ssl == rhs.ssl
           && super_priv == rhs.super_priv && global_db_priv == rhs.global_db_priv
           && proxy_priv == rhs.proxy_priv
           && is_role == rhs.is_role && default_role == rhs.default_role;
}

bool UserEntry::host_pattern_is_more_specific(const UserEntry& lhs, const UserEntry& rhs)
{
    // Order entries according to https://mariadb.com/kb/en/library/create-user/
    const string& lhost = lhs.host_pattern;
    const string& rhost = rhs.host_pattern;
    const char wildcards[] = "%_";
    auto lwc_pos = lhost.find_first_of(wildcards);
    auto rwc_pos = rhost.find_first_of(wildcards);
    bool lwc = (lwc_pos != string::npos);
    bool rwc = (rwc_pos != string::npos);

    bool rval;
    // The host without wc:s sorts earlier than the one with them,
    if (lwc != rwc)
    {
        rval = !lwc;
    }
    // ... and if both have wc:s, the one with the later wc wins (ties broken by strcmp),
    else if (lwc)
    {
        rval = ((lwc_pos > rwc_pos) || (lwc_pos == rwc_pos && lhost < rhost));
    }
    // ... and if neither have wildcards, use string order.
    else
    {
        rval = (lhost < rhost);
    }
    return rval;
}

/**
 * Read a complete MySQL-protocol packet. Returns false on read error. At least the header +
 * command byte part is contiguous. If a packet was not yet available, returns success and empty
 * buffer.
 *
 * @param dcb Dcb to read from
 * @return Result structure. Success, if reading succeeded. Also returns success if the entire packet was
 * not yet available and the function should be called again later.
 */
std::tuple<bool, GWBUF> read_protocol_packet(DCB* dcb)
{
    const int MAX_PACKET_SIZE = MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

    size_t bytes_to_read;
    uint8_t header_data[MYSQL_HEADER_LEN];
    if (dcb->readq_peek(MYSQL_HEADER_LEN, header_data) == MYSQL_HEADER_LEN)
    {
        bytes_to_read = mariadb::get_packet_length(header_data);
    }
    else
    {
        bytes_to_read = MAX_PACKET_SIZE;
    }

    auto rval = dcb->read(MYSQL_HEADER_LEN, bytes_to_read);
    auto& [read_ok, buffer] = rval;

    if (!buffer.empty())
    {
        // Got enough that the entire packet may be available.
        auto buffer_len = buffer.length();
        auto prot_packet_len = mariadb::get_packet_length(buffer.data());

        // Protocol packet length read. Either received more than the packet, the exact packet or
        // a partial packet.
        if (prot_packet_len < buffer_len)
        {
            // Got more than needed, save extra to DCB and trigger a read.
            auto first_packet = buffer.split(prot_packet_len);

            dcb->unread(move(buffer));
            buffer = move(first_packet);
            dcb->trigger_read_event();
        }
        else if (prot_packet_len == buffer_len)
        {
            // Read exact packet. Return it.
        }
        else
        {
            // Could not read enough, try again later. Save results to dcb.
            dcb->unread(move(buffer));
            buffer.clear();
        }
    }
    return rval;
}

uint8_t* write_header(uint8_t* buffer, uint32_t pl_size, uint8_t seq)
{
    mxb_assert(pl_size <= 0xFFFFFF);
    uint32_t seq_expanded = (seq << 24u);
    uint32_t host_bytes = seq_expanded | pl_size;
    set_byte4(buffer, host_bytes);
    return buffer + 4;
}

uint8_t* copy_bytes(uint8_t* dest, const uint8_t* src, size_t n)
{
    return static_cast<uint8_t*>(mempcpy(dest, src, n));
}

uint8_t* copy_chars(uint8_t* dest, const char* src, size_t n)
{
    return static_cast<uint8_t*>(mempcpy(dest, src, n));
}

uint8_t* set_bytes(uint8_t* dest, uint8_t val, size_t n)
{
    memset(dest, val, n);
    return dest + n;
}

uint32_t get_packet_length(const uint8_t* buffer)
{
    auto header = get_header(buffer);
    return MYSQL_HEADER_LEN + header.pl_length;
}

bool command_will_respond(uint32_t cmd)
{
    return mxs_mysql_command_will_respond(cmd);
}

bool is_com_query(const GWBUF& buf)
{
    return buf.length() > MYSQL_HEADER_LEN && buf[MYSQL_HEADER_LEN] == MXS_COM_QUERY;
}

bool is_com_prepare(const GWBUF& buf)
{
    return buf.length() > MYSQL_HEADER_LEN && buf[MYSQL_HEADER_LEN] == MXS_COM_STMT_PREPARE;
}

bool is_com_query_or_prepare(const GWBUF& buf)
{
    bool rval = false;
    if (buf.length() > MYSQL_HEADER_LEN)
    {
        auto cmd = buf[MYSQL_HEADER_LEN];
        rval = (cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_PREPARE);
    }
    return rval;
}

BackendAuthData::BackendAuthData(const char* srv_name)
    : servername(srv_name)
{
}

AuthSwitchReqContents parse_auth_switch_request(const GWBUF& input)
{
    int datalen = input.length() - MYSQL_HEADER_LEN;
    mxb_assert(datalen >= 0);
    packet_parser::ByteVec data;
    data.resize(datalen);
    input.copy_data(MYSQL_HEADER_LEN, datalen, data.data());
    return packet_parser::parse_auth_switch_request(data);
}

uint32_t total_sysvar_bytes(const std::map<std::string, std::string>& attrs)
{
    size_t total = 0;

    for (const auto& [key, value] : attrs)
    {
        // Each system variable is encoded as:
        // int<1>         Change type (0)
        // int<lenenc>    Total length
        // string<lenenc> Variable name
        // string<lenenc> Variable value
        size_t payload_bytes = leint_prefix_bytes(key.size()) + key.size()
            + leint_prefix_bytes(value.size()) + value.size();
        size_t payload_prefix_bytes = leint_prefix_bytes(payload_bytes);
        total += 1 + payload_prefix_bytes + payload_bytes;
    }

    return total;
}

uint8_t* encode_sysvar(uint8_t* ptr, std::string_view key, std::string_view value)
{
    size_t key_prefix_bytes = leint_prefix_bytes(key.size());
    size_t value_prefix_bytes = leint_prefix_bytes(value.size());
    size_t payload_bytes = key_prefix_bytes + key.size() + value_prefix_bytes + value.size();
    size_t payload_prefix_bytes = leint_prefix_bytes(payload_bytes);

    // The state change type, SESSION_TRACK_SYSTEM_VARIABLES
    // See: https://mariadb.com/kb/en/ok_packet/#session-state-info
    *ptr++ = 0;

    // The total length of the payload
    encode_leint(ptr, payload_prefix_bytes, payload_bytes);
    ptr += payload_prefix_bytes;

    // The variable name
    encode_leint(ptr, key_prefix_bytes, key.size());
    ptr += key_prefix_bytes;
    memcpy(ptr, key.data(), key.size());
    ptr += key.size();

    // The variable value
    encode_leint(ptr, value_prefix_bytes, value.size());
    ptr += value_prefix_bytes;
    memcpy(ptr, value.data(), value.size());
    ptr += value.size();

    return ptr;
}

GWBUF create_ok_packet(uint8_t sequence, uint64_t affected_rows,
                       const std::map<std::string, std::string>& attrs)
{
    /* Basic ok packet is
     * 4 bytes header
     * 1 byte 0
     * 1 to 9 bytes for affected rows
     * 1 byte insert id = 0
     * 2 bytes server status
     * 2 bytes warning counter
     * 1 byte info (empty length-encoded string)
     * Total 4 + 7 + affected_rows size (1 to 9 bytes)
     *
     * If the session state change attributes are not empty, they take up some extra space.
     */
    auto affected_rows_size = leint_prefix_bytes(affected_rows);
    uint32_t attr_size = 0;
    uint32_t attr_prefix_size = 0;
    uint32_t attr_total_size = 0;
    uint16_t server_status = SERVER_STATUS_AUTOCOMMIT;

    if (!attrs.empty())
    {
        attr_size = total_sysvar_bytes(attrs);
        attr_prefix_size = leint_prefix_bytes(attr_size);
        attr_total_size = attr_size + attr_prefix_size;
        server_status |= SERVER_SESSION_STATE_CHANGED;
    }

    const uint32_t pl_size = 6 + affected_rows_size + 1 + attr_total_size;
    const uint32_t total_size = MYSQL_HEADER_LEN + pl_size;
    GWBUF buffer(total_size);
    auto ptr = mariadb::write_header(buffer.data(), pl_size, sequence);
    *ptr++ = 0;
    encode_leint(ptr, affected_rows_size, affected_rows);
    ptr += affected_rows_size;
    *ptr++ = 0;
    ptr += mariadb::set_byte2(ptr, server_status);  // Server status
    ptr += mariadb::set_byte2(ptr, 0);              // No warnings
    *ptr++ = 0;                                     // No info

    if (attr_total_size)
    {
        encode_leint(ptr, attr_prefix_size, attr_size);
        ptr += attr_prefix_size;

        for (const auto& [key, value] : attrs)
        {
            ptr = encode_sysvar(ptr, key, value);
        }
    }

    mxb_assert(ptr - buffer.data() == total_size);
    return buffer;
}

/**
 * Create a COM_QUERY packet from a string.
 *
 * @param query Query to create.
 * @return Result GWBUF
 */
GWBUF create_query(std::string_view query)
{
    size_t plen = query.length() + 1;       // Query plus the command byte
    size_t total_len = MYSQL_HEADER_LEN + plen;
    GWBUF rval(total_len);
    auto ptr = mariadb::write_header(rval.data(), plen, 0);
    *ptr++ = MXS_COM_QUERY;
    ptr = mariadb::copy_chars(ptr, query.data(), query.length());
    mxb_assert(ptr - rval.data() == (ptrdiff_t)total_len);
    return rval;
}

GWBUF create_packet(uint8_t seq, const void* ptr, size_t len)
{
    size_t total_len = MYSQL_HEADER_LEN + len;
    GWBUF rval(total_len);
    auto dest = mariadb::write_header(rval.data(), len, seq);
    memcpy(dest, ptr, len);
    return rval;
}
/**
 * Split the buffer into complete and partial packets.
 *
 * @param buffer Buffer to split. Can be left empty.
 * @return Complete packets
 */
GWBUF get_complete_packets(GWBUF& buffer)
{
    const auto* start = buffer.data();
    size_t offset = 0;
    auto len_remaining = buffer.length();
    while (len_remaining >= MYSQL_HEADER_LEN)
    {
        auto header = get_header(start + offset);
        auto packet_len = MYSQL_HEADER_LEN + header.pl_length;
        if (len_remaining >= packet_len)
        {
            len_remaining -= packet_len;
            offset += packet_len;
        }
        else
        {
            len_remaining = 0;
        }
    }

    return buffer.split(offset);
}

/**
 * Return the first packet from a buffer.
 *
 * @param buffer If the GWBUF contains a complete packet, after the call it will have been updated to
 * begin at the byte following the packet.
 *
 * @return The first complete packet, or empty
 */
GWBUF get_next_MySQL_packet(GWBUF& buffer)
{
    GWBUF packet;
    size_t totalbuflen = buffer.length();
    if (totalbuflen >= MYSQL_HEADER_LEN)
    {
        auto packetlen = MYSQL_HEADER_LEN + get_header(buffer.data()).pl_length;
        if (packetlen <= totalbuflen)
        {
            packet = buffer.split(packetlen);
        }
    }

    mxb_assert(packet.empty() || only_one_packet(packet));
    return packet;
}

GWBUF create_error_packet(uint8_t sequence, uint16_t err_num, std::string_view sqlstate, std::string_view msg)
{
    mxb_assert(sqlstate.size() == 5 && !msg.empty());
    // Command byte [1](0xff)
    // Error number [2]
    // SQLSTATE marker [1](#)
    // SQLSTATE [5]
    // Error message [EOF]
    size_t payload_len = 1 + 2 + 1 + sqlstate.size() + msg.size();

    GWBUF buffer(payload_len + MYSQL_HEADER_LEN);
    uint8_t* ptr = buffer.data();

    ptr = mariadb::write_header(ptr, payload_len, sequence);
    *ptr++ = 0xff;
    ptr += mariadb::set_byte2(ptr, err_num);
    *ptr++ = '#';
    memcpy(ptr, sqlstate.data(), sqlstate.size());
    ptr += sqlstate.size();
    memcpy(ptr, msg.data(), msg.size());

    return buffer;
}

// See: https://mariadb.com/kb/en/ok_packet/
GWBUF create_ok_packet()
{
    uint8_t ok[] =
    {0x7, 0x0, 0x0, 0x1,// packet header
     0x0,               // OK header byte
     0x0,               // affected rows
     0x0,               // last_insert_id
     0x0, 0x0,          // server status
     0x0, 0x0           // warnings
    };

    return GWBUF(ok, sizeof(ok));
}

std::string extract_error(const GWBUF& buffer)
{
    std::string rval;
    auto* data = buffer.data();
    if (MYSQL_IS_ERROR_PACKET(data))
    {
        const uint8_t* pState;
        uint16_t nState;
        extract_error_state(data, &pState, &nState);

        const uint8_t* pMessage;
        uint16_t nMessage;
        extract_error_message(data, &pMessage, &nMessage);

        std::string err((const char*)pState, nState);
        std::string msg((const char*)pMessage, nMessage);

        rval = err.empty() ? msg : err + ": " + msg;
    }

    return rval;
}

std::string_view get_sql(const GWBUF& packet)
{
    std::string_view rv;

    if (is_com_query_or_prepare(packet))
    {
        size_t nHeader = MYSQL_HEADER_LEN + 1;

        const char* pSql = reinterpret_cast<const char*>(packet.data() + nHeader);
        size_t nSql = packet.length() - nHeader;

        rv = std::string_view {pSql, nSql};
    }

    return rv;
}

const char* bypass_whitespace(std::string_view sql)
{
    const char* i = sql.data();
    const char* end = i + sql.length();

    while (i != end)
    {
        if (isspace(*i))
        {
            ++i;
        }
        else if (*i == '/')     // Might be a comment
        {
            if ((i + 1 != end) && (*(i + 1) == '*'))    // Indeed it was
            {
                i += 2;

                while (i != end)
                {
                    if (*i == '*')      // Might be the end of the comment
                    {
                        ++i;

                        if (i != end)
                        {
                            if (*i == '/')      // Indeed it was
                            {
                                ++i;
                                break;      // Out of this inner while.
                            }
                        }
                    }
                    else
                    {
                        // It was not the end of the comment.
                        ++i;
                    }
                }
            }
            else
            {
                // Was not a comment, so we'll bail out.
                break;
            }
        }
        else if (*i == '-')     // Might be the start of a comment to the end of line
        {
            bool is_comment = false;

            if (i + 1 != end)
            {
                if (*(i + 1) == '-')    // Might be, yes.
                {
                    if (i + 2 != end)
                    {
                        if (isspace(*(i + 2)))      // Yes, it is.
                        {
                            is_comment = true;

                            i += 3;

                            while ((i != end) && (*i != '\n'))
                            {
                                ++i;
                            }

                            if (i != end)
                            {
                                mxb_assert(*i == '\n');
                                ++i;
                            }
                        }
                    }
                }
            }

            if (!is_comment)
            {
                break;
            }
        }
        else if (*i == '#')
        {
            ++i;

            while ((i != end) && (*i != '\n'))
            {
                ++i;
            }

            if (i != end)
            {
                mxb_assert(*i == '\n');
                ++i;
            }
            break;
        }
        else
        {
            // Neither whitespace not start of a comment, so we bail out.
            break;
        }
    }

    return i;
}

namespace
{
const char format_str[] = "COM_UNKNOWN(%02hhx)";

// The message always fits inside the buffer
thread_local char unknown_type[sizeof(format_str)] = "";
}

const char* cmd_to_string(int cmd)
{
    switch (cmd)
    {
    case MXS_COM_SLEEP:
        return "COM_SLEEP";

    case MXS_COM_QUIT:
        return "COM_QUIT";

    case MXS_COM_INIT_DB:
        return "COM_INIT_DB";

    case MXS_COM_QUERY:
        return "COM_QUERY";

    case MXS_COM_FIELD_LIST:
        return "COM_FIELD_LIST";

    case MXS_COM_CREATE_DB:
        return "COM_CREATE_DB";

    case MXS_COM_DROP_DB:
        return "COM_DROP_DB";

    case MXS_COM_REFRESH:
        return "COM_REFRESH";

    case MXS_COM_SHUTDOWN:
        return "COM_SHUTDOWN";

    case MXS_COM_STATISTICS:
        return "COM_STATISTICS";

    case MXS_COM_PROCESS_INFO:
        return "COM_PROCESS_INFO";

    case MXS_COM_CONNECT:
        return "COM_CONNECT";

    case MXS_COM_PROCESS_KILL:
        return "COM_PROCESS_KILL";

    case MXS_COM_DEBUG:
        return "COM_DEBUG";

    case MXS_COM_PING:
        return "COM_PING";

    case MXS_COM_TIME:
        return "COM_TIME";

    case MXS_COM_DELAYED_INSERT:
        return "COM_DELAYED_INSERT";

    case MXS_COM_CHANGE_USER:
        return "COM_CHANGE_USER";

    case MXS_COM_BINLOG_DUMP:
        return "COM_BINLOG_DUMP";

    case MXS_COM_TABLE_DUMP:
        return "COM_TABLE_DUMP";

    case MXS_COM_CONNECT_OUT:
        return "COM_CONNECT_OUT";

    case MXS_COM_REGISTER_SLAVE:
        return "COM_REGISTER_SLAVE";

    case MXS_COM_STMT_PREPARE:
        return "COM_STMT_PREPARE";

    case MXS_COM_STMT_EXECUTE:
        return "COM_STMT_EXECUTE";

    case MXS_COM_STMT_SEND_LONG_DATA:
        return "COM_STMT_SEND_LONG_DATA";

    case MXS_COM_STMT_CLOSE:
        return "COM_STMT_CLOSE";

    case MXS_COM_STMT_RESET:
        return "COM_STMT_RESET";

    case MXS_COM_SET_OPTION:
        return "COM_SET_OPTION";

    case MXS_COM_STMT_FETCH:
        return "COM_STMT_FETCH";

    case MXS_COM_DAEMON:
        return "COM_DAEMON";

    case MXS_COM_RESET_CONNECTION:
        return "COM_RESET_CONNECTION";

    case MXS_COM_STMT_BULK_EXECUTE:
        return "COM_STMT_BULK_EXECUTE";

    case MXS_COM_MULTI:
        return "COM_MULTI";

    case MXS_COM_XPAND_REPL:
        return "COM_XPAND_REPL";
    }

    snprintf(unknown_type, sizeof(unknown_type), format_str, static_cast<unsigned char>(cmd));

    return unknown_type;
}

bool trim_quotes(char* s)
{
    bool dequoted = true;

    char* i = s;
    char* end = s + strlen(s);

    // Remove space from the beginning
    while (*i && isspace(*i))
    {
        ++i;
    }

    if (*i)
    {
        // Remove space from the end
        while (isspace(*(end - 1)))
        {
            *(end - 1) = 0;
            --end;
        }

        mxb_assert(end > i);

        char quote;

        switch (*i)
        {
        case '\'':
        case '"':
        case '`':
            quote = *i;
            ++i;
            break;

        default:
            quote = 0;
        }

        if (quote)
        {
            --end;

            if (*end == quote)
            {
                *end = 0;

                memmove(s, i, end - i + 1);
            }
            else
            {
                dequoted = false;
            }
        }
        else if (i != s)
        {
            memmove(s, i, end - i + 1);
        }
    }
    else
    {
        *s = 0;
    }

    return dequoted;
}
}
