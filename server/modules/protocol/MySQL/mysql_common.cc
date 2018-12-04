/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 */

#include <netinet/tcp.h>

#include <set>
#include <sstream>
#include <map>

#include <maxscale/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/log.h>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/utils.h>
#include <maxscale/protocol/mariadb_client.hh>
#include <maxscale/poll.hh>
#include <maxscale/routingworker.h>


uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN] = "";

MYSQL_session* mysql_session_alloc()
{
    MYSQL_session* ses = (MYSQL_session*)MXS_CALLOC(1, sizeof(MYSQL_session));

    return ses;
}

MySQLProtocol* mysql_protocol_init(DCB* dcb, int fd)
{
    MySQLProtocol* p;

    p = (MySQLProtocol*) MXS_CALLOC(1, sizeof(MySQLProtocol));
    mxb_assert(p != NULL);

    if (p == NULL)
    {
        goto return_p;
    }
    p->protocol_state = MYSQL_PROTOCOL_ALLOC;
    p->protocol_auth_state = MXS_AUTH_STATE_INIT;
    p->current_command = MXS_COM_UNDEFINED;
    p->stored_query = NULL;
    p->extra_capabilities = 0;
    p->ignore_replies = 0;
    p->collect_result = false;
    p->changing_user = false;
    p->num_eof_packets = 0;
    p->large_query = false;
    /*< Assign fd with protocol */
    p->fd = fd;
    p->owner_dcb = dcb;
    p->protocol_state = MYSQL_PROTOCOL_ACTIVE;
return_p:
    return p;
}

bool mysql_protocol_done(DCB* dcb)
{
    bool rval = false;
    MySQLProtocol* p = (MySQLProtocol*)dcb->protocol;

    if (p->protocol_state == MYSQL_PROTOCOL_ACTIVE)
    {
        gwbuf_free(p->stored_query);
        p->protocol_state = MYSQL_PROTOCOL_DONE;
        rval = true;
    }

    return rval;
}

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

void mysql_protocol_set_current_command(DCB* dcb, mxs_mysql_cmd_t cmd)
{
    MySQLProtocol* proto = (MySQLProtocol*)dcb->protocol;
    proto->current_command = cmd;
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

int mysql_send_com_quit(DCB* dcb,
                        int  packet_number,
                        GWBUF* bufparam)
{
    GWBUF* buf;
    int nbytes = 0;

    mxb_assert(packet_number <= 255);

    if (dcb == NULL)
    {
        return 0;
    }
    if (bufparam == NULL)
    {
        buf = mysql_create_com_quit(NULL, packet_number);
    }
    else
    {
        buf = bufparam;
    }

    if (buf == NULL)
    {
        return 0;
    }
    nbytes = dcb->func.write(dcb, buf);

    return nbytes;
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
 * @brief Create a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param packet_number Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param error_message Text message to be included
 * @return GWBUF        A buffer containing the error message, ready to send
 */
GWBUF* mysql_create_standard_error(int packet_number,
                                   int error_number,
                                   const char* error_message)
{
    uint8_t* outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t mysql_error_number[2];
    uint8_t* mysql_handshake_payload = NULL;
    GWBUF* buf;

    mysql_payload_size = 1 + sizeof(mysql_error_number) + strlen(error_message);

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        return NULL;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with mysql_payload_size
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);

    // write packet number, now is 0
    mysql_packet_header[3] = packet_number;
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    // current buffer pointer
    mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

    // write 0xff which is the error indicator
    *mysql_handshake_payload = 0xff;
    mysql_handshake_payload++;

    // write error number
    gw_mysql_set_byte2(mysql_handshake_payload, error_number);
    mysql_handshake_payload += 2;

    // write error message
    memcpy(mysql_handshake_payload, error_message, strlen(error_message));

    return buf;
}

/**
 * @brief Send a standard MariaDB error message, emulating real server
 *
 * Supports the sending to a client of a standard database error, for
 * circumstances where the error is generated within MaxScale but should
 * appear like a backend server error. First introduced to support connection
 * throttling, to send "Too many connections" error.
 *
 * @param dcb           The client DCB to which error is to be sent
 * @param packet_number Packet number for header
 * @param error_number  Standard error number as for MariaDB
 * @param error_message Text message to be included
 * @return      0 on failure, 1 on success
 */
int mysql_send_standard_error(DCB* dcb,
                              int  packet_number,
                              int  error_number,
                              const char* error_message)
{
    GWBUF* buf;
    buf = mysql_create_standard_error(packet_number, error_number, error_message);
    return buf ? dcb->func.write(dcb, buf) : 0;
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
                            int  packet_number,
                            int  in_affected_rows,
                            const char* mysql_message)
{
    GWBUF* buf;

    buf = mysql_create_custom_error(packet_number, in_affected_rows, mysql_message);

    return dcb->func.write(dcb, buf);
}

/**
 * mysql_send_auth_error
 *
 * Send a MySQL protocol ERR message, for gateway authentication error to the dcb
 *
 * @param dcb descriptor Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param in_affected_rows
 * @param mysql_message
 * @return packet length
 *
 */
int mysql_send_auth_error(DCB* dcb,
                          int  packet_number,
                          int  in_affected_rows,
                          const char* mysql_message)
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

    GWBUF* buf;

    if (dcb->state != DCB_STATE_POLLING)
    {
        MXS_DEBUG("dcb %p is in a state %s, and it is not in epoll set anymore. Skip error sending.",
                  dcb,
                  STRDCBSTATE(dcb->state));
        return 0;
    }
    mysql_error_msg = "Access denied!";
    mysql_state = "28000";

    field_count = 0xff;
    gw_mysql_set_byte2(mysql_err,    /*mysql_errno */ 1045);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    if (mysql_message != NULL)
    {
        mysql_error_msg = mysql_message;
    }

    mysql_payload_size =
        sizeof(field_count) + sizeof(mysql_err) + sizeof(mysql_statemsg) + strlen(mysql_error_msg);

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        return 0;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with packet number
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
    mysql_packet_header[3] = packet_number;

    // write header
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    mysql_payload = outbuf + sizeof(mysql_packet_header);

    // write field
    memcpy(mysql_payload, &field_count, sizeof(field_count));
    mysql_payload = mysql_payload + sizeof(field_count);

    // write errno
    memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
    mysql_payload = mysql_payload + sizeof(mysql_err);

    // write sqlstate
    memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
    mysql_payload = mysql_payload + sizeof(mysql_statemsg);

    // write err messg
    memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

    // writing data in the Client buffer queue
    dcb->func.write(dcb, buf);

    return sizeof(mysql_packet_header) + mysql_payload_size;
}

char* create_auth_failed_msg(GWBUF* readbuf,
                             char*  hostaddr,
                             uint8_t* sha1)
{
    char* errstr;
    char* uname = (char*)GWBUF_DATA(readbuf) + 5;
    const char* ferrstr = "Access denied for user '%s'@'%s' (using password: %s)";

    /** -4 comes from 2X'%s' minus terminating char */
    errstr = (char*)MXS_MALLOC(strlen(uname) + strlen(ferrstr) + strlen(hostaddr) + strlen("YES") - 6 + 1);

    if (errstr != NULL)
    {
        sprintf(errstr, ferrstr, uname, hostaddr, (*sha1 == '\0' ? "NO" : "YES"));
    }

    return errstr;
}

/**
 * Create a message error string to send via MySQL ERR packet.
 *
 * @param       username        The MySQL user
 * @param       hostaddr        The client IP
 * @param       password        If client provided a password
 * @param       db              The default database the client requested
 * @param       errcode         Authentication error code
 *
 * @return      Pointer to the allocated string or NULL on failure
 */
char* create_auth_fail_str(char* username,
                           char* hostaddr,
                           bool  password,
                           char* db,
                           int   errcode)
{
    char* errstr;
    const char* ferrstr;
    int db_len;

    if (db != NULL)
    {
        db_len = strlen(db);
    }
    else
    {
        db_len = 0;
    }

    if (db_len > 0)
    {
        ferrstr = "Access denied for user '%s'@'%s' (using password: %s) to database '%s'";
    }
    else if (errcode == MXS_AUTH_FAILED_SSL)
    {
        ferrstr = "Access without SSL denied";
    }
    else
    {
        ferrstr = "Access denied for user '%s'@'%s' (using password: %s)";
    }
    errstr = (char*)MXS_MALLOC(strlen(username) + strlen(ferrstr)
                               + strlen(hostaddr) + strlen("YES") - 6
                               + db_len + ((db_len > 0) ? (strlen(" to database ") + 2) : 0) + 1);

    if (errstr == NULL)
    {
        goto retblock;
    }

    if (db_len > 0)
    {
        sprintf(errstr, ferrstr, username, hostaddr, password ? "YES" : "NO", db);
    }
    else if (errcode == MXS_AUTH_FAILED_SSL)
    {
        sprintf(errstr, "%s", ferrstr);
    }
    else
    {
        sprintf(errstr, ferrstr, username, hostaddr, password ? "YES" : "NO");
    }

retblock:
    return errstr;
}

/**
 * @brief Read a complete packet from a DCB
 *
 * Read a complete packet from a connected DCB. If data was read, @c readbuf
 * will point to the head of the read data. If no data was read, @c readbuf will
 * be set to NULL.
 *
 * @param dcb DCB to read from
 * @param readbuf Pointer to a buffer where the data is stored
 * @return True on success, false if an error occurred while data was being read
 */
bool read_complete_packet(DCB* dcb, GWBUF** readbuf)
{
    bool rval = false;
    GWBUF* localbuf = NULL;

    if (dcb_read(dcb, &localbuf, 0) >= 0)
    {
        rval = true;
        dcb->last_read = mxs_clock();
        GWBUF* packets = modutil_get_complete_packets(&localbuf);

        if (packets)
        {
            /** A complete packet was read */
            *readbuf = packets;
        }

        if (localbuf)
        {
            /** Store any extra data in the DCB's readqueue */

            dcb_readq_append(dcb, localbuf);
        }
    }

    return rval;
}

/**
 * Copy shared session authentication info
 *
 * @param dcb A backend DCB
 * @param session Destination where authentication data is copied
 * @return bool true = success, false = fail
 */
bool gw_get_shared_session_auth_info(DCB* dcb, MYSQL_session* session)
{
    bool rval = true;

    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        // The shared session data can be extracted at any time if the client DCB is used.
        mxb_assert(dcb->data);
        memcpy(session, dcb->data, sizeof(MYSQL_session));
    }
    else if (dcb->session->state != SESSION_STATE_ALLOC)
    {
        memcpy(session, dcb->session->client_dcb->data, sizeof(MYSQL_session));
    }
    else
    {
        mxb_assert(false);
        MXS_ERROR("Couldn't get session authentication info. Session in a wrong state %s.",
                  STRSESSIONSTATE(dcb->session->state));
        rval = false;
    }

    return rval;
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
    return dcb->func.write(dcb, buf);
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
static int response_length(bool with_ssl,
                           bool ssl_established,
                           char* user,
                           uint8_t* passwd,
                           char* dbname,
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

/**
 * Calculates the a hash from a scramble and a password
 *
 * The algorithm used is: `SHA1(scramble + SHA1(SHA1(password))) ^ SHA1(password)`
 *
 * @param scramble The 20 byte scramble sent by the server
 * @param passwd   The SHA1(password) sent by the client
 * @param output   Pointer where the resulting 20 byte hash is stored
 */
static void calculate_hash(uint8_t* scramble, uint8_t* passwd, uint8_t* output)
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
 * @param conn DCB Protocol object
 * @param payload Destination where hashed password is written
 * @param passwd Client's double SHA1 password
 *
 * @return Address of the next byte after the end of the stored password
 */
static uint8_t* load_hashed_password(uint8_t* scramble, uint8_t* payload, uint8_t* passwd)
{
    *payload++ = GW_MYSQL_SCRAMBLE_SIZE;
    calculate_hash(scramble, passwd, payload);
    return payload + GW_MYSQL_SCRAMBLE_SIZE;
}

/**
 * @brief Computes the capabilities bit mask for connecting to backend DB
 *
 * We start by taking the default bitmask and removing any bits not set in
 * the bitmask contained in the connection structure. Then add SSL flag if
 * the connection requires SSL (set from the MaxScale configuration). The
 * compression flag may be set, although compression is NOT SUPPORTED. If a
 * database name has been specified in the function call, the relevant flag
 * is set.
 *
 * @param conn  The MySQLProtocol structure for the connection
 * @param db_specified Whether the connection request specified a database
 * @param compress Whether compression is requested - NOT SUPPORTED
 * @return Bit mask (32 bits)
 * @note Capability bits are defined in maxscale/protocol/mysql.h
 */
static uint32_t create_capabilities(MySQLProtocol* conn,
                                    bool with_ssl,
                                    bool db_specified,
                                    uint64_t capabilities)
{
    uint32_t final_capabilities;

    /** Copy client's flags to backend but with the known capabilities mask */
    final_capabilities = (conn->client_capabilities & (uint32_t)GW_MYSQL_CAPABILITIES_CLIENT);

    if (with_ssl)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL;
        /*
         * Unclear whether we should include this
         * Maybe it should depend on whether CA certificate is provided
         * final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT;
         */
    }

    if (rcap_type_required(capabilities, RCAP_TYPE_SESSION_STATE_TRACKING))
    {
        /** add session track */
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SESSION_TRACK;
    }

    /** support multi statments  */
    final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;

    if (db_specified)
    {
        /* With database specified */
        final_capabilities |= (int)GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
    }
    else
    {
        /* Without database specified */
        final_capabilities &= ~(int)GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
    }

    final_capabilities |= (int)GW_MYSQL_CAPABILITIES_PLUGIN_AUTH;

    return final_capabilities;
}

GWBUF* gw_generate_auth_response(MYSQL_session* client,
                                 MySQLProtocol* conn,
                                 bool with_ssl,
                                 bool ssl_established,
                                 uint64_t service_capabilities)
{
    uint8_t client_capabilities[4] = {0, 0, 0, 0};
    uint8_t* curr_passwd = NULL;

    if (memcmp(client->client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) != 0)
    {
        curr_passwd = client->client_sha1;
    }

    uint32_t capabilities = create_capabilities(conn, with_ssl, client->db[0], service_capabilities);
    gw_mysql_set_byte4(client_capabilities, capabilities);

    /**
     * Use the default authentication plugin name. If the server is using a
     * different authentication mechanism, it will send an AuthSwitchRequest
     * packet.
     */
    const char* auth_plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;

    long bytes = response_length(with_ssl,
                                 ssl_established,
                                 client->user,
                                 curr_passwd,
                                 client->db,
                                 auth_plugin_name);

    // allocating the GWBUF
    GWBUF* buffer = gwbuf_alloc(bytes);
    uint8_t* payload = GWBUF_DATA(buffer);

    // clearing data
    memset(payload, '\0', bytes);

    // put here the paylod size: bytes to write - 4 bytes packet header
    gw_mysql_set_byte3(payload, (bytes - 4));

    // set packet # = 1
    payload[3] = ssl_established ? '\x02' : '\x01';
    payload += 4;

    // set client capabilities
    memcpy(payload, client_capabilities, 4);

    // set now the max-packet size
    payload += 4;
    gw_mysql_set_byte4(payload, 16777216);

    // set the charset
    payload += 4;
    *payload = conn->charset;

    payload++;

    // 19 filler bytes of 0
    payload += 19;

    // Either MariaDB 10.2 extra capabilities or 4 bytes filler
    memcpy(payload, &conn->extra_capabilities, sizeof(conn->extra_capabilities));
    payload += 4;

    if (!with_ssl || ssl_established)
    {
        // 4 + 4 + 4 + 1 + 23 = 36, this includes the 4 bytes packet header
        memcpy(payload, client->user, strlen(client->user));
        payload += strlen(client->user);
        payload++;

        if (curr_passwd)
        {
            payload = load_hashed_password(conn->scramble, payload, curr_passwd);
        }
        else
        {
            payload++;
        }

        // if the db is not NULL append it
        if (client->db[0])
        {
            memcpy(payload, client->db, strlen(client->db));
            payload += strlen(client->db);
            payload++;
        }

        memcpy(payload, auth_plugin_name, strlen(auth_plugin_name));
    }

    return buffer;
}

/**
 * Write MySQL authentication packet to backend server
 *
 * @param dcb  Backend DCB
 * @return Authentication state after sending handshake response
 */
mxs_auth_state_t gw_send_backend_auth(DCB* dcb)
{
    mxs_auth_state_t rval = MXS_AUTH_STATE_FAILED;

    if (dcb->session == NULL
        || (dcb->session->state != SESSION_STATE_READY
            && dcb->session->state != SESSION_STATE_ROUTER_READY)
        || (dcb->server->server_ssl
            && dcb->ssl_state == SSL_HANDSHAKE_FAILED))
    {
        return rval;
    }

    bool with_ssl = dcb->server->server_ssl;
    bool ssl_established = dcb->ssl_state == SSL_ESTABLISHED;

    MYSQL_session client;
    gw_get_shared_session_auth_info(dcb->session->client_dcb, &client);

    GWBUF* buffer = gw_generate_auth_response(&client,
                                              (MySQLProtocol*)dcb->protocol,
                                              with_ssl,
                                              ssl_established,
                                              dcb->service->capabilities);
    mxb_assert(buffer);

    if (with_ssl && !ssl_established)
    {
        if (dcb_write(dcb, buffer) && dcb_connect_SSL(dcb) >= 0)
        {
            rval = MXS_AUTH_STATE_CONNECTED;
        }
    }
    else if (dcb_write(dcb, buffer))
    {
        rval = MXS_AUTH_STATE_RESPONSE_SENT;
    }

    return rval;
}

int send_mysql_native_password_response(DCB* dcb)
{
    MySQLProtocol* proto = (MySQLProtocol*) dcb->protocol;
    MYSQL_session local_session;
    gw_get_shared_session_auth_info(dcb, &local_session);

    uint8_t* curr_passwd = memcmp(local_session.client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) ?
        local_session.client_sha1 : null_client_sha1;

    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + GW_MYSQL_SCRAMBLE_SIZE);
    uint8_t* data = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(data, GW_MYSQL_SCRAMBLE_SIZE);
    data[3] = 2;    // This is the third packet after the COM_CHANGE_USER
    calculate_hash(proto->scramble, curr_passwd, data + MYSQL_HEADER_LEN);

    return dcb_write(dcb, buffer);
}

bool send_auth_switch_request_packet(DCB* dcb)
{
    MySQLProtocol* proto = (MySQLProtocol*) dcb->protocol;
    const char plugin[] = DEFAULT_MYSQL_AUTH_PLUGIN;
    uint32_t len = 1 + sizeof(plugin) + GW_MYSQL_SCRAMBLE_SIZE;
    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + len);

    uint8_t* data = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(data, len);
    data[3] = 1;    // First response to the COM_CHANGE_USER
    data[MYSQL_HEADER_LEN] = MYSQL_REPLY_AUTHSWITCHREQUEST;
    memcpy(data + MYSQL_HEADER_LEN + 1, plugin, sizeof(plugin));
    memcpy(data + MYSQL_HEADER_LEN + 1 + sizeof(plugin), proto->scramble, GW_MYSQL_SCRAMBLE_SIZE);

    return dcb_write(dcb, buffer) != 0;
}

/**
 * Decode mysql server handshake
 *
 * @param conn The MySQLProtocol structure
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 *
 */
int gw_decode_mysql_server_handshake(MySQLProtocol* conn, uint8_t* payload)
{
    uint8_t* server_version_end = NULL;
    uint16_t mysql_server_capabilities_one = 0;
    uint16_t mysql_server_capabilities_two = 0;
    uint8_t scramble_data_1[GW_SCRAMBLE_LENGTH_323] = "";
    uint8_t scramble_data_2[GW_MYSQL_SCRAMBLE_SIZE - GW_SCRAMBLE_LENGTH_323] = "";
    uint8_t capab_ptr[4] = "";
    int scramble_len = 0;
    uint8_t mxs_scramble[GW_MYSQL_SCRAMBLE_SIZE] = "";
    int protocol_version = 0;

    protocol_version = payload[0];

    if (protocol_version != GW_MYSQL_PROTOCOL_VERSION)
    {
        return -1;
    }

    payload++;

    // Get server version (string)
    server_version_end = (uint8_t*) gw_strend((char*) payload);

    payload = server_version_end + 1;

    // get ThreadID: 4 bytes
    uint32_t tid = gw_mysql_get_byte4(payload);
    /* TODO: Correct value of thread id could be queried later from backend if
     * there is any worry it might be larger than 32bit allows. */
    conn->thread_id = tid;

    payload += 4;

    // scramble_part 1
    memcpy(scramble_data_1, payload, GW_SCRAMBLE_LENGTH_323);
    payload += GW_SCRAMBLE_LENGTH_323;

    // 1 filler
    payload++;

    mysql_server_capabilities_one = gw_mysql_get_byte2(payload);

    // Get capabilities_part 1 (2 bytes) + 1 language + 2 server_status
    payload += 5;

    mysql_server_capabilities_two = gw_mysql_get_byte2(payload);

    conn->server_capabilities = mysql_server_capabilities_one | mysql_server_capabilities_two << 16;

    // 2 bytes shift
    payload += 2;

    // get scramble len
    if (payload[0] > 0)
    {
        scramble_len = payload[0] - 1;
        mxb_assert(scramble_len > GW_SCRAMBLE_LENGTH_323);
        mxb_assert(scramble_len <= GW_MYSQL_SCRAMBLE_SIZE);

        if ((scramble_len < GW_SCRAMBLE_LENGTH_323)
            || scramble_len > GW_MYSQL_SCRAMBLE_SIZE)
        {
            /* log this */
            return -2;
        }
    }
    else
    {
        scramble_len = GW_MYSQL_SCRAMBLE_SIZE;
    }
    // skip 10 zero bytes
    payload += 11;

    // copy the second part of the scramble
    memcpy(scramble_data_2, payload, scramble_len - GW_SCRAMBLE_LENGTH_323);

    memcpy(mxs_scramble, scramble_data_1, GW_SCRAMBLE_LENGTH_323);
    memcpy(mxs_scramble + GW_SCRAMBLE_LENGTH_323, scramble_data_2, scramble_len - GW_SCRAMBLE_LENGTH_323);

    // full 20 bytes scramble is ready
    memcpy(conn->scramble, mxs_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    return 0;
}

/**
 * Read the backend server MySQL handshake
 *
 * @param dcb  Backend DCB
 * @return true on success, false on failure
 */
bool gw_read_backend_handshake(DCB* dcb, GWBUF* buffer)
{
    MySQLProtocol* proto = (MySQLProtocol*)dcb->protocol;
    bool rval = false;
    uint8_t* payload = GWBUF_DATA(buffer) + 4;

    if (gw_decode_mysql_server_handshake(proto, payload) >= 0)
    {
        rval = true;
    }

    return rval;
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

bool mxs_mysql_is_result_set(GWBUF* buffer)
{
    bool rval = false;
    uint8_t cmd;

    if (gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd))
    {
        switch (cmd)
        {

        case MYSQL_REPLY_OK:
        case MYSQL_REPLY_ERR:
        case MYSQL_REPLY_LOCAL_INFILE:
        case MYSQL_REPLY_EOF:
            /** Not a result set */
            break;

        default:
            rval = true;
            break;
        }
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
        ptr += mxs_leint_bytes(ptr);
        ptr += mxs_leint_bytes(ptr);
        uint16_t* status = (uint16_t*)ptr;
        rval = (*status) & SERVER_MORE_RESULTS_EXIST;
    }

    return rval;
}

mxs_mysql_cmd_t mxs_mysql_current_command(MXS_SESSION* session)
{
    MySQLProtocol* proto = (MySQLProtocol*)session->client_dcb->protocol;
    return proto->current_command;
}

const char* mxs_mysql_get_current_db(MXS_SESSION* session)
{
    MYSQL_session* data = (MYSQL_session*)session->client_dcb->data;
    return data->db;
}

void mxs_mysql_set_current_db(MXS_SESSION* session, const char* db)
{
    MYSQL_session* data = (MYSQL_session*)session->client_dcb->data;
    snprintf(data->db, sizeof(data->db), "%s", db);
}

bool mxs_mysql_extract_ps_response(GWBUF* buffer, MXS_PS_RESPONSE* out)
{
    bool rval = false;
    uint8_t id[MYSQL_PS_ID_SIZE];
    uint8_t cols[MYSQL_PS_ID_SIZE];
    uint8_t params[MYSQL_PS_ID_SIZE];
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

namespace
{

// Servers and queries to execute on them
typedef std::map<SERVER*, std::string> TargetList;

struct KillInfo
{
    typedef  bool (* DcbCallback)(DCB* dcb, void* data);

    KillInfo(std::string query, MXS_SESSION* ses, DcbCallback callback)
        : origin(mxs_rworker_get_current_id())
        , query_base(query)
        , protocol(*(MySQLProtocol*)ses->client_dcb->protocol)
        , cb(callback)
    {
        gw_get_shared_session_auth_info(ses->client_dcb, &session);
    }

    int           origin;
    std::string   query_base;
    MYSQL_session session;
    MySQLProtocol protocol;
    DcbCallback   cb;
    TargetList    targets;
};

static bool kill_func(DCB* dcb, void* data);

struct ConnKillInfo : public KillInfo
{
    ConnKillInfo(uint64_t id, std::string query, MXS_SESSION* ses)
        : KillInfo(query, ses, kill_func)
        , target_id(id)
    {
    }

    uint64_t target_id;
};

static bool kill_user_func(DCB* dcb, void* data);

struct UserKillInfo : public KillInfo
{
    UserKillInfo(std::string name, std::string query, MXS_SESSION* ses)
        : KillInfo(query, ses, kill_user_func)
        , user(name)
    {
    }

    std::string user;
};

static bool kill_func(DCB* dcb, void* data)
{
    ConnKillInfo* info = static_cast<ConnKillInfo*>(data);

    if (dcb->session->ses_id == info->target_id && dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
    {
        MySQLProtocol* proto = (MySQLProtocol*)dcb->protocol;

        if (proto->thread_id)
        {
            // DCB is connected and we know the thread ID so we can kill it
            std::stringstream ss;
            ss << info->query_base << proto->thread_id;
            info->targets[dcb->server] = ss.str();
        }
        else
        {
            // DCB is not yet connected, send a hangup to forcibly close it
            dcb->session->close_reason = SESSION_CLOSE_KILLED;
            poll_fake_hangup_event(dcb);
        }
    }

    return true;
}

static bool kill_user_func(DCB* dcb, void* data)
{
    UserKillInfo* info = (UserKillInfo*)data;

    if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER
        && strcasecmp(dcb->session->client_dcb->user, info->user.c_str()) == 0)
    {
        info->targets[dcb->server] = info->query_base;
    }

    return true;
}

static void worker_func(int thread_id, void* data)
{
    KillInfo* info = static_cast<KillInfo*>(data);
    dcb_foreach_local(info->cb, info);

    for (TargetList::iterator it = info->targets.begin();
         it != info->targets.end(); it++)
    {
        LocalClient* client = LocalClient::create(&info->session, &info->protocol, it->first);
        GWBUF* buffer = modutil_create_query(it->second.c_str());
        client->queue_query(buffer);
        gwbuf_free(buffer);

        // The LocalClient needs to delete itself once the queries are done
        client->self_destruct();
    }

    delete info;
}
}

void mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id, kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query;

    for (int i = 0; i < config_threadcount(); i++)
    {
        MXB_WORKER* worker = mxs_rworker_get(i);
        mxb_assert(worker);
        mxb_worker_post_message(worker,
                                MXB_WORKER_MSG_CALL,
                                (intptr_t)worker_func,
                                (intptr_t) new ConnKillInfo(target_id, ss.str(), issuer));
    }

    mxs_mysql_send_ok(issuer->client_dcb, 1, 0, NULL);
}

void mxs_mysql_execute_kill_user(MXS_SESSION* issuer, const char* user, kill_type_t type)
{
    const char* hard = (type & KT_HARD) ? "HARD " : (type & KT_SOFT) ? "SOFT " : "";
    const char* query = (type & KT_QUERY) ? "QUERY " : "";
    std::stringstream ss;
    ss << "KILL " << hard << query << "USER " << user;

    for (int i = 0; i < config_threadcount(); i++)
    {
        MXB_WORKER* worker = mxs_rworker_get(i);
        mxb_assert(worker);
        mxb_worker_post_message(worker,
                                MXB_WORKER_MSG_CALL,
                                (intptr_t)worker_func,
                                (intptr_t) new UserKillInfo(user, ss.str(), issuer));
    }

    mxs_mysql_send_ok(issuer->client_dcb, 1, 0, NULL);
}

/**
 *  Parse ok packet to get session track info, save to buff properties
 *  @param buff           Buffer contain multi compelte packets
 *  @param packet_offset  Ok packet offset in this buff
 *  @param packet_len     Ok packet lengh
 */
void mxs_mysql_parse_ok_packet(GWBUF* buff, size_t packet_offset, size_t packet_len)
{
    uint8_t local_buf[packet_len];
    uint8_t* ptr = local_buf;
    char* trx_info, * var_name, * var_value;

    gwbuf_copy_data(buff, packet_offset, packet_len, local_buf);
    ptr += (MYSQL_HEADER_LEN + 1);  // Header and Command type
    mxs_leint_consume(&ptr);        // Affected rows
    mxs_leint_consume(&ptr);        // Last insert-id
    uint16_t server_status = gw_mysql_get_byte2(ptr);
    ptr += 2;   // status
    ptr += 2;   // number of warnings

    if (ptr < (local_buf + packet_len))
    {
        size_t size;
        mxs_lestr_consume(&ptr, &size);     // info

        if (server_status & SERVER_SESSION_STATE_CHANGED)
        {
            MXB_AT_DEBUG(uint64_t data_size = ) mxs_leint_consume(&ptr);    // total
                                                                            // SERVER_SESSION_STATE_CHANGED
                                                                            // length
            mxb_assert(data_size == packet_len - (ptr - local_buf));

            while (ptr < (local_buf + packet_len))
            {
                enum_session_state_type type =
                    (enum enum_session_state_type)mxs_leint_consume(&ptr);
#if defined (SS_DEBUG)
                mxb_assert(type <= SESSION_TRACK_TRANSACTION_TYPE);
#endif
                switch (type)
                {
                case SESSION_TRACK_STATE_CHANGE:
                case SESSION_TRACK_SCHEMA:
                    size = mxs_leint_consume(&ptr);     // Length of the overall entity.
                    ptr += size;
                    break;

                case SESSION_TRACK_GTIDS:
                    mxs_leint_consume(&ptr);    // Length of the overall entity.
                    mxs_leint_consume(&ptr);    // encoding specification
                    var_value = mxs_lestr_consume_dup(&ptr);
                    gwbuf_add_property(buff, MXS_LAST_GTID, var_value);
                    MXS_FREE(var_value);
                    break;

                case SESSION_TRACK_TRANSACTION_CHARACTERISTICS:
                    mxs_leint_consume(&ptr);    // length
                    var_value = mxs_lestr_consume_dup(&ptr);
                    gwbuf_add_property(buff, "trx_characteristics", var_value);
                    MXS_FREE(var_value);
                    break;

                case SESSION_TRACK_SYSTEM_VARIABLES:
                    mxs_leint_consume(&ptr);    // lenth
                    // system variables like autocommit, schema, charset ...
                    var_name = mxs_lestr_consume_dup(&ptr);
                    var_value = mxs_lestr_consume_dup(&ptr);
                    gwbuf_add_property(buff, var_name, var_value);
                    MXS_DEBUG("SESSION_TRACK_SYSTEM_VARIABLES, name:%s, value:%s", var_name, var_value);
                    MXS_FREE(var_name);
                    MXS_FREE(var_value);
                    break;

                case SESSION_TRACK_TRANSACTION_TYPE:
                    mxs_leint_consume(&ptr);    // length
                    trx_info = mxs_lestr_consume_dup(&ptr);
                    MXS_DEBUG("get trx_info:%s", trx_info);
                    gwbuf_add_property(buff, (char*)"trx_state", trx_info);
                    MXS_FREE(trx_info);
                    break;

                default:
                    mxs_lestr_consume(&ptr, &size);
                    MXS_WARNING("recieved unexpecting session track type:%d", type);
                    break;
                }
            }
        }
    }
}

/**
 *  Check every packet type, if is ok packet then parse it
 *  @param buff                 Buffer contain multi compelte packets
 *  @param server_capabilities  Server capabilities
 */
void mxs_mysql_get_session_track_info(GWBUF* buff, MySQLProtocol* proto)
{
    size_t offset = 0;
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    if (proto->server_capabilities & GW_MYSQL_CAPABILITIES_SESSION_TRACK)
    {
        while (gwbuf_copy_data(buff,
                               offset,
                               MYSQL_HEADER_LEN + 1,
                               header_and_command) == (MYSQL_HEADER_LEN + 1))
        {
            size_t packet_len = gw_mysql_get_byte3(header_and_command) + MYSQL_HEADER_LEN;
            uint8_t cmd = header_and_command[MYSQL_COM_OFFSET];

            if (packet_len > MYSQL_OK_PACKET_MIN_LEN
                && cmd == MYSQL_REPLY_OK
                && (proto->num_eof_packets % 2) == 0)
            {
                buff->gwbuf_type |= GWBUF_TYPE_REPLY_OK;
                mxs_mysql_parse_ok_packet(buff, offset, packet_len);
            }

            if ((proto->current_command == MXS_COM_QUERY
                 || proto->current_command == MXS_COM_STMT_FETCH
                 || proto->current_command == MXS_COM_STMT_EXECUTE)
                && cmd == MYSQL_REPLY_EOF)
            {
                proto->num_eof_packets++;
            }
            offset += packet_len;
        }
    }
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
