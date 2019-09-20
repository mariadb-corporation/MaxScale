/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 */

#include <maxscale/protocol/mariadb/protocol_classes.hh>

#include <set>
#include <sstream>
#include <map>
#include <netinet/tcp.h>
#include <mysql.h>
#include <maxsql/mariadb.hh>
#include <maxbase/alloc.h>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/utils.h>
#include <maxscale/poll.hh>
#include <maxscale/routing.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>
#include <maxscale/target.hh>

using mxs::ReplyState;

uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN] = "";

const char* gw_mysql_protocol_state2string(int state)
{
    switch (state)
    {
    case MXS_AUTH_STATE_INIT:
        return "Authentication initialized";

    case MXS_AUTH_STATE_PENDING_CONNECT:
        return "Network connection pending";

    case MXS_AUTH_STATE_CONNECTED:
        return "Network connection created";

    case MXS_AUTH_STATE_MESSAGE_READ:
        return "Read server handshake";

    case MXS_AUTH_STATE_RESPONSE_SENT:
        return "Response to handshake sent";

    case MXS_AUTH_STATE_FAILED:
        return "Authentication failed";

    case MXS_AUTH_STATE_COMPLETE:
        return "Authentication is complete.";

    default:
        return "MySQL (unknown protocol state)";
    }
}

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
    mxb_assert(GWBUF_LENGTH(buf) == COM_QUIT_PACKET_SIZE);

    data = GWBUF_DATA(buf);

    *data++ = 0x1;
    *data++ = 0x0;
    *data++ = 0x0;
    *data++ = packet_number;
    *data = 0x1;

    return buf;
}

GWBUF* mysql_create_custom_error(int packet_number,
                                 int affected_rows,
                                 const char* msg)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t* mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    const char* mysql_error_msg = NULL;
    const char* mysql_state = NULL;

    GWBUF* errbuf = NULL;

    mysql_error_msg = "An errorr occurred ...";
    mysql_state = "HY000";

    field_count = 0xff;
    gw_mysql_set_byte2(mysql_err,    /* mysql_errno */ 2003);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    if (msg != NULL)
    {
        mysql_error_msg = msg;
    }

    mysql_payload_size =
        sizeof(field_count)
        + sizeof(mysql_err)
        + sizeof(mysql_statemsg)
        + strlen(mysql_error_msg);

    /** allocate memory for packet header + payload */
    errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    mxb_assert(errbuf != NULL);

    if (errbuf == NULL)
    {
        return 0;
    }
    outbuf = GWBUF_DATA(errbuf);

    /** write packet header and packet number */
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = packet_number;

    /** write header */
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    mysql_payload = outbuf + sizeof(mysql_packet_header);

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
    memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

    return errbuf;
}

/**
 * mysql_send_custom_error
 *
 * Send a MySQL protocol Generic ERR message, to the dcb
 * Note the errno and state are still fixed now
 *
 * @param dcb Owner_Dcb Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param in_affected_rows
 * @param mysql_message
 * @return 1 Non-zero if data was sent
 *
 */
int mysql_send_custom_error(DCB* dcb,
                            int packet_number,
                            int in_affected_rows,
                            const char* mysql_message)
{
    GWBUF* buf = mysql_create_custom_error(packet_number, in_affected_rows, mysql_message);
    return dcb->protocol_write(buf);
}

/**
 * @brief Send a MySQL protocol OK message to the dcb (client)
 *
 * @param dcb DCB where packet is written
 * @param sequence Packet sequence number
 * @param affected_rows Number of affected rows
 * @param message SQL message
 * @return 1 on success, 0 on error
 *
 * @todo Support more than 255 affected rows
 */
int mxs_mysql_send_ok(DCB* dcb, int sequence, uint8_t affected_rows, const char* message)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t* mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t insert_id = 0;
    uint8_t mysql_server_status[2];
    uint8_t mysql_warning_counter[2];
    GWBUF* buf;


    mysql_payload_size =
        sizeof(field_count)
        + sizeof(affected_rows)
        + sizeof(insert_id)
        + sizeof(mysql_server_status)
        + sizeof(mysql_warning_counter);

    if (message != NULL)
    {
        mysql_payload_size += strlen(message);
    }

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        return 0;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with packet number
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = sequence;

    // write header
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    mysql_payload = outbuf + sizeof(mysql_packet_header);

    mysql_server_status[0] = 2;
    mysql_server_status[1] = 0;
    mysql_warning_counter[0] = 0;
    mysql_warning_counter[1] = 0;

    // write data
    memcpy(mysql_payload, &field_count, sizeof(field_count));
    mysql_payload = mysql_payload + sizeof(field_count);

    memcpy(mysql_payload, &affected_rows, sizeof(affected_rows));
    mysql_payload = mysql_payload + sizeof(affected_rows);

    memcpy(mysql_payload, &insert_id, sizeof(insert_id));
    mysql_payload = mysql_payload + sizeof(insert_id);

    memcpy(mysql_payload, mysql_server_status, sizeof(mysql_server_status));
    mysql_payload = mysql_payload + sizeof(mysql_server_status);

    memcpy(mysql_payload, mysql_warning_counter, sizeof(mysql_warning_counter));
    mysql_payload = mysql_payload + sizeof(mysql_warning_counter);

    if (message != NULL)
    {
        memcpy(mysql_payload, message, strlen(message));
    }

    // writing data in the Client buffer queue
    return dcb->protocol_write(buf);
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
int response_length(bool with_ssl, bool ssl_established, const char* user, uint8_t* passwd, char* dbname,
                    const char* auth_module)
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

void mxs_mysql_calculate_hash(uint8_t* scramble, uint8_t* passwd, uint8_t* output)
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
    gw_str_xor(output, new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE);
}

/**
 * @brief Helper function to load hashed password
 *
 * @param payload Destination where hashed password is written
 * @param passwd Client's double SHA1 password
 *
 * @return Address of the next byte after the end of the stored password
 */
uint8_t* load_hashed_password(uint8_t* scramble, uint8_t* payload, uint8_t* passwd)
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
        rval = gw_mysql_get_byte2(buf);
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

bool mxs_mysql_more_results_after_ok(GWBUF* buffer)
{
    bool rval = false;

    // Copy the header
    uint8_t header[MYSQL_HEADER_LEN + 1];
    gwbuf_copy_data(buffer, 0, sizeof(header), header);

    if (header[4] == MYSQL_REPLY_OK)
    {
        // Copy the payload without the command byte
        size_t len = gw_mysql_get_byte3(header);
        uint8_t data[len - 1];
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN + 1, sizeof(data), data);

        uint8_t* ptr = data;
        ptr += mxq::leint_bytes(ptr);
        ptr += mxq::leint_bytes(ptr);
        uint16_t* status = (uint16_t*)ptr;
        rval = (*status) & SERVER_MORE_RESULTS_EXIST;
    }

    return rval;
}

const char* mxs_mysql_get_current_db(MXS_SESSION* session)
{
    auto proto = static_cast<MySQLClientProtocol*>(session->client_dcb->protocol());
    return proto->session_data()->db;
}

void mxs_mysql_set_current_db(MXS_SESSION* session, const char* db)
{
    auto proto = static_cast<MySQLClientProtocol*>(session->client_dcb->protocol());
    MYSQL_session* data = proto->session_data();
    snprintf(data->db, sizeof(data->db), "%s", db);
}

bool mxs_mysql_extract_ps_response(GWBUF* buffer, MXS_PS_RESPONSE* out)
{
    bool rval = false;
    uint8_t id[MYSQL_PS_ID_SIZE];
    uint8_t cols[MYSQL_PS_COLS_SIZE];
    uint8_t params[MYSQL_PS_PARAMS_SIZE];
    uint8_t warnings[MYSQL_PS_WARN_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id) == sizeof(id)
        && gwbuf_copy_data(buffer, MYSQL_PS_COLS_OFFSET, sizeof(cols), cols) == sizeof(cols)
        && gwbuf_copy_data(buffer, MYSQL_PS_PARAMS_OFFSET, sizeof(params), params) == sizeof(params)
        && gwbuf_copy_data(buffer, MYSQL_PS_WARN_OFFSET, sizeof(warnings), warnings) == sizeof(warnings))
    {
        out->id = gw_mysql_get_byte4(id);
        out->columns = gw_mysql_get_byte2(cols);
        out->parameters = gw_mysql_get_byte2(params);
        out->warnings = gw_mysql_get_byte2(warnings);
        rval = true;
    }

    return rval;
}

uint32_t mxs_mysql_extract_ps_id(GWBUF* buffer)
{
    uint32_t rval = 0;
    uint8_t id[MYSQL_PS_ID_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id) == sizeof(id))
    {
        rval = gw_mysql_get_byte4(id);
    }

    return rval;
}

bool mxs_mysql_command_will_respond(uint8_t cmd)
{
    return cmd != MXS_COM_STMT_SEND_LONG_DATA
           && cmd != MXS_COM_QUIT
           && cmd != MXS_COM_STMT_CLOSE;
}

/***
 * As described in https://dev.mysql.com/worklog/task/?id=6631
 * When session transation state changed
 * SESSION_TRACK_TRANSACTION_TYPE (or SESSION_TRACK_TRANSACTION_STATE in MySQL) will
 * return an 8 bytes string to indicate the transaction state details
 * Place 1: Transaction.
 * T  explicitly started transaction ongoing
 * I  implicitly started transaction (@autocommit=0) ongoing
 * _  no active transaction
 *
 * Place 2: unsafe read
 * r  one/several non-transactional tables were read
 *    in the context of the current transaction
 * _  no non-transactional tables were read within
 *    the current transaction so far
 *
 * Place 3: transactional read
 * R  one/several transactional tables were read
 * _  no transactional tables were read yet
 *
 * Place 4: unsafe write
 * w  one/several non-transactional tables were written
 * _  no non-transactional tables were written yet
 *
 * Place 5: transactional write
 * W  one/several transactional tables were written to
 * _  no transactional tables were written to yet
 *
 * Place 6: unsafe statements
 * s  one/several unsafe statements (such as UUID())
 *    were used.
 * _  no such statements were used yet.
 *
 * Place 7: result-set
 * S  a result set was sent to the client
 * _  statement had no result-set
 *
 * Place 8: LOCKed TABLES
 * L  tables were explicitly locked using LOCK TABLES
 * _  LOCK TABLES is not active in this session
 * */
mysql_tx_state_t parse_trx_state(const char* str)
{
    int s = TX_EMPTY;
    mxb_assert(str);
    do
    {
        switch (*str)
        {
        case 'T':
            s |= TX_EXPLICIT;
            break;

        case 'I':
            s |= TX_IMPLICIT;
            break;

        case 'r':
            s |= TX_READ_UNSAFE;
            break;

        case 'R':
            s |= TX_READ_TRX;
            break;

        case 'w':
            s |= TX_WRITE_UNSAFE;
            break;

        case 'W':
            s |= TX_WRITE_TRX;
            break;

        case 's':
            s |= TX_STMT_UNSAFE;
            break;

        case 'S':
            s |= TX_RESULT_SET;
            break;

        case 'L':
            s |= TX_LOCKED_TABLES;
            break;

        default:
            break;
        }
    }
    while (*(str++) != 0);

    return (mysql_tx_state_t)s;
}

MySQLProtocol::MySQLProtocol(MXS_SESSION* session, SERVER* server)
    : m_session(session)
    , m_reply(server)
    , m_version(service_get_version(session->service, SERVICE_VERSION_MIN))
{
}

MySQLProtocol::~MySQLProtocol()
{
    gwbuf_free(stored_query);
}

using Iter = MySQLProtocol::Iter;

uint64_t get_encoded_int(Iter it)
{
    uint64_t len = *it++;

    switch (len)
    {
    case 0xfc:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        break;

    case 0xfd:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        len |= ((uint64_t)*it++) << 16;
        break;

    case 0xfe:
        len = *it++;
        len |= ((uint64_t)*it++) << 8;
        len |= ((uint64_t)*it++) << 16;
        len |= ((uint64_t)*it++) << 24;
        len |= ((uint64_t)*it++) << 32;
        len |= ((uint64_t)*it++) << 40;
        len |= ((uint64_t)*it++) << 48;
        len |= ((uint64_t)*it++) << 56;
        break;

    default:
        break;
    }

    return len;
}

Iter skip_encoded_int(Iter it)
{
    switch (*it)
    {
    case 0xfc:
        it.advance(3);
        break;

    case 0xfd:
        it.advance(4);
        break;

    case 0xfe:
        it.advance(9);
        break;

    default:
        ++it;
        break;
    }

    return it;
}

bool is_last_ok(Iter it)
{
    ++it;                       // Skip the command byte
    it = skip_encoded_int(it);  // Affected rows
    it = skip_encoded_int(it);  // Last insert ID
    uint16_t status = *it++;
    status |= (*it++) << 8;
    return (status & SERVER_MORE_RESULTS_EXIST) == 0;
}

bool is_last_eof(Iter it)
{
    std::advance(it, 3);    // Skip the command byte and warning count
    uint16_t status = *it++;
    status |= (*it++) << 8;
    return (status & SERVER_MORE_RESULTS_EXIST) == 0;
}

static inline bool complete_ps_response(GWBUF* buffer)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(buffer));
    MXS_PS_RESPONSE resp;
    bool rval = false;

    if (mxs_mysql_extract_ps_response(buffer, &resp))
    {
        int expected_packets = 1;

        if (resp.columns > 0)
        {
            // Column definition packets plus one for the EOF
            expected_packets += resp.columns + 1;
        }

        if (resp.parameters > 0)
        {
            // Parameter definition packets plus one for the EOF
            expected_packets += resp.parameters + 1;
        }

        int n_packets = modutil_count_packets(buffer);

        MXS_DEBUG("Expecting %u packets, have %u", n_packets, expected_packets);

        rval = n_packets == expected_packets;
    }

    return rval;
}

void MySQLProtocol::track_query(GWBUF* buffer)
{
    mxb_assert(gwbuf_is_contiguous(buffer));
    uint8_t* data = GWBUF_DATA(buffer);

    if (changing_user)
    {
        // User reauthentication in progress, ignore the contents
        return;
    }

    if (session_is_load_active(m_session))
    {
        if (MYSQL_GET_PAYLOAD_LEN(data) == 0)
        {
            MXS_INFO("Load data ended");
            session_set_load_active(m_session, false);
            set_reply_state(ReplyState::START);
        }
    }
    else if (!m_large_query)
    {
        m_reply.clear();
        m_reply.set_command(MYSQL_GET_COMMAND(data));

        if (mxs_mysql_command_will_respond(m_reply.command()))
        {
            set_reply_state(ReplyState::START);
        }

        if (m_reply.command() == MXS_COM_STMT_EXECUTE)
        {
            // Extract the flag byte after the statement ID
            uint8_t flags = data[MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE];

            // Any non-zero flag value means that we have an open cursor
            m_opening_cursor = flags != 0;
        }
        else if (m_reply.command() == MXS_COM_STMT_FETCH)
        {
            // Number of rows to fetch is a 4 byte integer after the ID
            m_expected_rows = gw_mysql_get_byte4(data + MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE);
        }
    }

    /**
     * If the buffer contains a large query, we have to skip the command
     * byte extraction for the next packet. This way current_command always
     * contains the latest command executed on this backend.
     */
    m_large_query = MYSQL_GET_PAYLOAD_LEN(data) == MYSQL_PACKET_LENGTH_MAX;
}
