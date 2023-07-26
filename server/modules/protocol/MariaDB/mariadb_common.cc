/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
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

#include <mysql.h>
#include <maxbase/format.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/service.hh>
#include <maxscale/target.hh>
#include "packet_parser.hh"

using std::string;
using std::move;
using mxs::ReplyState;
using UserEntry = mariadb::UserEntry;

GWBUF* mysql_create_com_quit(GWBUF* bufparam,
                             int packet_number)
{
    uint8_t* data;
    GWBUF* buf;

    if (bufparam == NULL)
    {
        buf = gwbuf_alloc(COM_QUIT_PACKET_SIZE);
    }
    else
    {
        buf = bufparam;
    }

    if (buf == NULL)
    {
        return 0;
    }
    mxb_assert(gwbuf_link_length(buf) == COM_QUIT_PACKET_SIZE);

    data = GWBUF_DATA(buf);

    *data++ = 0x1;
    *data++ = 0x0;
    *data++ = 0x0;
    *data++ = packet_number;
    *data = 0x1;

    return buf;
}

GWBUF* mysql_create_custom_error(int packet_number, int affected_rows, uint16_t errnum, const char* errmsg)
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
    GWBUF* errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    uint8_t* outbuf = GWBUF_DATA(errbuf);

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

/**
 * @brief Computes the size of the response to the DB initial handshake
 *
 * When the connection is to be SSL, but an SSL connection has not yet been
 * established, only a basic 36 byte response is sent, including the SSL
 * capability flag.
 *
 * Otherwise, the packet size is computed, based on the minimum size and
 * increased by the optional or variable elements.
 *
 * @param with_ssl        SSL is used
 * @param ssl_established SSL is established
 * @param user            Name of the user seeking to connect
 * @param passwd          Password for the user seeking to connect
 * @param dbname          Name of the database to be made default, if any
 *
 * @return The length of the response packet
 */
int response_length(bool with_ssl, bool ssl_established, const char* user,
                    const uint8_t* passwd, const char* dbname, const char* auth_module)
{
    long bytes;

    if (with_ssl && !ssl_established)
    {
        return MYSQL_AUTH_PACKET_BASE_SIZE;
    }

    // Protocol MySQL HandshakeResponse for CLIENT_PROTOCOL_41
    // 4 bytes capabilities + 4 bytes max packet size + 1 byte charset + 23 '\0' bytes
    // 4 + 4 + 1 + 23  = 32
    bytes = 32;

    if (user)
    {
        bytes += strlen(user);
    }
    // the NULL
    bytes++;

    // next will be + 1 (scramble_len) + 20 (fixed_scramble) + 1 (user NULL term) + 1 (db NULL term)

    if (passwd)
    {
        bytes += GW_MYSQL_SCRAMBLE_SIZE;
    }
    bytes++;

    if (dbname && strlen(dbname))
    {
        bytes += strlen(dbname);
        bytes++;
    }

    bytes += strlen(auth_module);
    bytes++;

    // the packet header
    bytes += 4;

    return bytes;
}

void mxs_mysql_calculate_hash(const uint8_t* scramble, const uint8_t* passwd, uint8_t* output)
{
    uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE] = "";
    uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE] = "";
    uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE] = "";

    // hash1 is the function input, SHA1(real_password)
    memcpy(hash1, passwd, GW_MYSQL_SCRAMBLE_SIZE);

    // hash2 is the SHA1(input data), where input_data = SHA1(real_password)
    gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);

    // new_sha is the SHA1(CONCAT(scramble, hash2)
    gw_sha1_2_str(scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, GW_MYSQL_SCRAMBLE_SIZE, new_sha);

    // compute the xor in client_scramble
    mxs::bin_bin_xor(new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE, output);
}

/**
 * @brief Helper function to load hashed password
 *
 * @param payload Destination where hashed password is written
 * @param passwd Client's double SHA1 password
 *
 * @return Address of the next byte after the end of the stored password
 */
uint8_t* load_hashed_password(const uint8_t* scramble, uint8_t* payload, const uint8_t* passwd)
{
    *payload++ = GW_MYSQL_SCRAMBLE_SIZE;
    mxs_mysql_calculate_hash(scramble, passwd, payload);
    return payload + GW_MYSQL_SCRAMBLE_SIZE;
}

bool mxs_mysql_is_ok_packet(GWBUF* buffer)
{
    uint8_t cmd = 0xff;     // Default should differ from the OK packet
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    return cmd == MYSQL_REPLY_OK;
}

bool mxs_mysql_is_err_packet(GWBUF* buffer)
{
    uint8_t cmd = 0x00;     // Default should differ from the ERR packet
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    return cmd == MYSQL_REPLY_ERR;
}

uint16_t mxs_mysql_get_mysql_errno(GWBUF* buffer)
{
    uint16_t rval = 0;

    if (mxs_mysql_is_err_packet(buffer))
    {
        uint8_t buf[2];
        // First two bytes after the 0xff byte are the error code
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN + 1, 2, buf);
        rval = mariadb::get_byte2(buf);
    }

    return rval;
}

bool mxs_mysql_is_local_infile(GWBUF* buffer)
{
    uint8_t cmd = 0xff;     // Default should differ from the OK packet
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    return cmd == MYSQL_REPLY_LOCAL_INFILE;
}

bool mxs_mysql_is_prep_stmt_ok(GWBUF* buffer)
{
    bool rval = false;
    uint8_t cmd;

    if (gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd)
        && cmd == MYSQL_REPLY_OK)
    {
        rval = true;
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

uint8_t mxs_mysql_get_command(const GWBUF* buffer)
{
    // This function is sometimes called with 0-length packets. Should perhaps be fixed by modifying
    // the callers.
    uint8_t rval = MXS_COM_UNDEFINED;
    if (buffer->length() > MYSQL_HEADER_LEN)
    {
        rval = (*buffer)[MYSQL_HEADER_LEN];
    }
    return rval;
}

uint32_t mxs_mysql_extract_ps_id(GWBUF* buffer)
{
    uint32_t rval = 0;
    uint8_t id[MYSQL_PS_ID_SIZE];
    size_t sz = gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id);

    if (sz == sizeof(id))
    {
        rval = mariadb::get_byte4(id);
    }
    else
    {
        MXB_WARNING("Malformed binary protocol packet.");
        gwbuf_hexdump_pretty(buffer, LOG_WARNING);
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

uint32_t MYSQL_session::client_capabilities() const
{
    return client_caps.basic_capabilities;
}

uint32_t MYSQL_session::extra_capabilities() const
{
    return client_caps.ext_capabilities;
}

uint64_t MYSQL_session::full_capabilities() const
{
    return client_capabilities() | (uint64_t)extra_capabilities() << 32;
}

std::string MYSQL_session::user_and_host() const
{
    return mxb::string_printf("'%s'@'%s'", auth_data->user.c_str(), remote.c_str());
}

bool MYSQL_session::is_trx_read_only() const
{
    return trx_state & TrxState::TRX_READ_ONLY;
}

bool MYSQL_session::is_trx_ending() const
{
    return trx_state & TrxState::TRX_ENDING;
}

bool MYSQL_session::is_trx_starting() const
{
    return trx_state & TrxState::TRX_STARTING;
}

bool MYSQL_session::is_trx_active() const
{
    return trx_state & TrxState::TRX_ACTIVE;
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

    size_t sescmd_history = 0;
    for (const GWBUF& buf : this->history)
    {
        sescmd_history += buf.runtime_size();
    }

    // The map overhead is ignored.
    sescmd_history += this->history_responses.size() * sizeof(decltype(this->history_responses)::value_type);
    sescmd_history += this->history_info.size() * sizeof(decltype(this->history_info)::value_type);

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

mariadb::AuthByteVec AuthenticatorModule::generate_token(const std::string& password)
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
            buffer = GWBUF();
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

HeaderData get_header(const uint8_t* buffer)
{
    auto bytes = get_byte4(buffer);
    HeaderData rval;
    rval.pl_length = (bytes & 0xFFFFFFu);
    rval.seq = bytes >> 24u;
    return rval;
}

uint32_t get_packet_length(const uint8_t* buffer)
{
    auto header = get_header(buffer);
    return MYSQL_HEADER_LEN + header.pl_length;
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

GWBUF create_ok_packet(uint8_t sequence, uint8_t affected_rows)
{
    mxb_assert(affected_rows < 0xFB);

    /* Basic ok packet is
     * 4 bytes header
     * 1 byte 0
     * 1 byte affected rows (assuming that value is < 0xFB)
     * 1 byte insert id = 0
     * 2 bytes server status
     * 2 bytes warning counter
     * Total 4 + 7
     */

    const uint32_t pl_size = 7;
    const uint32_t total_size = MYSQL_HEADER_LEN + pl_size;
    GWBUF buffer(total_size);
    auto ptr = mariadb::write_header(buffer.data(), pl_size, sequence);
    *ptr++ = 0;
    *ptr++ = affected_rows;
    *ptr++ = 0;
    ptr += mariadb::set_byte2(ptr, 2);   // autocommit is on
    ptr += mariadb::set_byte2(ptr, 0);   // no warnings
    buffer.write_complete(ptr - buffer.data());
    return buffer;
}

/**
 * Create a COM_QUERY packet from a string.
 *
 * @param query Query to create.
 * @return Result GWBUF
 */
GWBUF create_query(const string& query)
{
    size_t plen = query.length() + 1;       // Query plus the command byte
    size_t total_len = MYSQL_HEADER_LEN + plen;
    GWBUF rval(total_len);
    auto ptr = mariadb::write_header(rval.data(), plen, 0);
    *ptr++ = MXS_COM_QUERY;
    ptr = mariadb::copy_chars(ptr, query.c_str(), query.length());
    rval.write_complete(ptr - rval.data());
    mxb_assert(rval.length() == total_len);
    return rval;
}
}
