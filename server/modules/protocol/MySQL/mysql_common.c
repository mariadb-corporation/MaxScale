/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 */

#include <netinet/tcp.h>

#include <maxscale/utils.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>

uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN] = "";

static server_command_t* server_command_init(server_command_t* srvcmd, mysql_server_cmd_t cmd);

/**
 * @brief Allocate a new MySQL_session
 * @return New MySQL_session or NULL if memory allocation failed
 */
MYSQL_session* mysql_session_alloc()
{
    MYSQL_session *ses = MXS_CALLOC(1, sizeof(MYSQL_session));

    if (ses)
    {
#ifdef SS_DEBUG
        ses->myses_chk_top = CHK_NUM_MYSQLSES;
        ses->myses_chk_tail = CHK_NUM_MYSQLSES;
#endif
    }

    return ses;
}

/**
 * Creates MySQL protocol structure
 *
 * @param dcb *          Must be non-NULL.
 * @param fd
 *
 * @return
 *
 *
 * @details Protocol structure does not have fd because dcb is not
 * connected yet.
 *
 */
MySQLProtocol* mysql_protocol_init(DCB* dcb, int fd)
{
    MySQLProtocol* p;

    p = (MySQLProtocol *) MXS_CALLOC(1, sizeof(MySQLProtocol));
    ss_dassert(p != NULL);

    if (p == NULL)
    {
        goto return_p;
    }
    p->protocol_state = MYSQL_PROTOCOL_ALLOC;
    p->protocol_auth_state = MXS_AUTH_STATE_INIT;
    p->current_command = MYSQL_COM_UNDEFINED;
    p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
    p->protocol_command.scom_nresponse_packets = 0;
    p->protocol_command.scom_nbytes_to_read = 0;
    p->stored_query = NULL;
    p->extra_capabilities = 0;
    p->ignore_replies = 0;
#if defined(SS_DEBUG)
    p->protocol_chk_top = CHK_NUM_PROTOCOL;
    p->protocol_chk_tail = CHK_NUM_PROTOCOL;
#endif
    /*< Assign fd with protocol */
    p->fd = fd;
    p->owner_dcb = dcb;
    p->protocol_state = MYSQL_PROTOCOL_ACTIVE;
    CHK_PROTOCOL(p);
return_p:
    return p;
}

/**
 * Free protocol object
 *
 * @param dcb Owner DCB
 *
 * @return True if protocol was closed
 */
bool mysql_protocol_done(DCB* dcb)
{
    bool rval = false;
    MySQLProtocol* p = (MySQLProtocol *)dcb->protocol;

    if (p->protocol_state == MYSQL_PROTOCOL_ACTIVE)
    {
        server_command_t* scmd = p->protocol_cmd_history;

        while (scmd)
        {
            server_command_t* temp = scmd;
            scmd = scmd->scom_next;
            MXS_FREE(temp);
        }

        gwbuf_free(p->stored_query);

        p->protocol_state = MYSQL_PROTOCOL_DONE;
        rval = true;
    }

    return rval;
}

/**
 * Return a string representation of a MySQL protocol state.
 *
 * @param state The protocol state
 * @return String representation of the state
 *
 */
const char* gw_mysql_protocol_state2string (int state)
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
    ss_dassert(GWBUF_LENGTH(buf) == COM_QUIT_PACKET_SIZE);

    data = GWBUF_DATA(buf);

    *data++ = 0x1;
    *data++ = 0x0;
    *data++ = 0x0;
    *data++ = packet_number;
    *data   = 0x1;

    return buf;
}

int mysql_send_com_quit(DCB*   dcb,
                        int    packet_number,
                        GWBUF* bufparam)
{
    GWBUF *buf;
    int nbytes = 0;

    CHK_DCB(dcb);
    ss_dassert(packet_number <= 255);

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


GWBUF* mysql_create_custom_error(int         packet_number,
                                 int         affected_rows,
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
    gw_mysql_set_byte2(mysql_err, /* mysql_errno */ 2003);
    mysql_statemsg[0] = '#';
    memcpy(mysql_statemsg + 1, mysql_state, 5);

    if (msg != NULL)
    {
        mysql_error_msg = msg;
    }

    mysql_payload_size =
        sizeof(field_count) +
        sizeof(mysql_err) +
        sizeof(mysql_statemsg) +
        strlen(mysql_error_msg);

    /** allocate memory for packet header + payload */
    errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
    ss_dassert(errbuf != NULL);

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
GWBUF *
mysql_create_standard_error(int packet_number,
                            int error_number,
                            const char *error_message)
{
    uint8_t *outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t mysql_error_number[2];
    uint8_t *mysql_handshake_payload = NULL;
    GWBUF *buf;

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
    mysql_packet_header[3] = 0;
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
int
mysql_send_standard_error(DCB *dcb,
                          int packet_number,
                          int error_number,
                          const char *error_message)
{
    GWBUF *buf;
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
int mysql_send_custom_error(DCB       *dcb,
                            int        packet_number,
                            int        in_affected_rows,
                            const char *mysql_message)
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
int mysql_send_auth_error(DCB        *dcb,
                          int        packet_number,
                          int        in_affected_rows,
                          const char *mysql_message)
{
    uint8_t *outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t *mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t mysql_err[2];
    uint8_t mysql_statemsg[6];
    const char *mysql_error_msg = NULL;
    const char *mysql_state = NULL;

    GWBUF *buf;

    if (dcb->state != DCB_STATE_POLLING)
    {
        MXS_DEBUG("dcb %p is in a state %s, and it is not in epoll set anymore. Skip error sending.",
                  dcb, STRDCBSTATE(dcb->state));
        return 0;
    }
    mysql_error_msg = "Access denied!";
    mysql_state = "28000";

    field_count = 0xff;
    gw_mysql_set_byte2(mysql_err, /*mysql_errno */ 1045);
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


/**
 * Buffer contains at least one of the following:
 * complete [complete] [partial] mysql packet
 *
 * @param p_readbuf     Address of read buffer pointer
 *
 * @return pointer to gwbuf containing a complete packet or
 *   NULL if no complete packet was found.
 */
GWBUF* gw_MySQL_get_next_packet(GWBUF** p_readbuf)
{
    GWBUF* packetbuf;
    GWBUF* readbuf;
    size_t buflen;
    size_t packetlen;
    size_t totalbuflen;
    uint8_t* data;
    size_t nbytes_copied = 0;
    uint8_t* target;

    readbuf = *p_readbuf;

    if (readbuf == NULL)
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }
    CHK_GWBUF(readbuf);

    if (GWBUF_EMPTY(readbuf))
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }
    totalbuflen = gwbuf_length(readbuf);
    data = (uint8_t *)GWBUF_DATA((readbuf));
    packetlen = MYSQL_GET_PAYLOAD_LEN(data) + 4;

    /** packet is incomplete */
    if (packetlen > totalbuflen)
    {
        packetbuf = NULL;
        goto return_packetbuf;
    }

    packetbuf = gwbuf_alloc(packetlen);
    target = GWBUF_DATA(packetbuf);
    packetbuf->gwbuf_type = readbuf->gwbuf_type; /*< Copy the type too */
    /**
     * Copy first MySQL packet to packetbuf and leave posible other
     * packets to read buffer.
     */
    while (nbytes_copied < packetlen && totalbuflen > 0)
    {
        uint8_t* src = GWBUF_DATA((*p_readbuf));
        size_t   bytestocopy;

        buflen = GWBUF_LENGTH((*p_readbuf));
        bytestocopy = buflen < (packetlen - nbytes_copied) ? buflen : packetlen - nbytes_copied;

        memcpy(target + nbytes_copied, src, bytestocopy);
        *p_readbuf = gwbuf_consume((*p_readbuf), bytestocopy);
        totalbuflen = gwbuf_length((*p_readbuf));
        nbytes_copied += bytestocopy;
    }
    ss_dassert(buflen == 0 || nbytes_copied == packetlen);

return_packetbuf:
    return packetbuf;
}

/**
 * Move <npackets> from buffer pointed to by <*p_readbuf>.
 * Appears to be unused 11 May 2016 (Martin)
 */
GWBUF* gw_MySQL_get_packets(GWBUF** p_srcbuf,
                            int*    npackets)
{
    GWBUF* packetbuf;
    GWBUF* targetbuf = NULL;

    while (*npackets > 0 && (packetbuf = gw_MySQL_get_next_packet(p_srcbuf)) != NULL)
    {
        targetbuf = gwbuf_append(targetbuf, packetbuf);
        *npackets -= 1;
    }
    ss_dassert(*npackets < 128);
    ss_dassert(*npackets >= 0);

    return targetbuf;
}


static server_command_t* server_command_init(server_command_t* srvcmd,
                                             mysql_server_cmd_t cmd)
{
    server_command_t* c;

    if (srvcmd != NULL)
    {
        c = srvcmd;
    }
    else
    {
        c = (server_command_t *)MXS_MALLOC(sizeof(server_command_t));
    }
    if (c != NULL)
    {
        c->scom_cmd = cmd;
        c->scom_nresponse_packets = -1;
        c->scom_nbytes_to_read = 0;
        c->scom_next = NULL;
    }

    return c;
}

static server_command_t* server_command_copy(server_command_t* srvcmd)
{
    server_command_t* c = (server_command_t *)MXS_MALLOC(sizeof(server_command_t));
    if (c)
    {
        *c = *srvcmd;
    }

    return c;
}

#define MAX_CMD_HISTORY 10

void protocol_archive_srv_command(MySQLProtocol* p)
{
    server_command_t* s1;
    server_command_t* h1;
    int len = 0;

    CHK_PROTOCOL(p);

    if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
    {
        goto retblock;
    }

    s1 = &p->protocol_command;
#if defined(EXTRA_SS_DEBUG)
    MXS_INFO("Move command %s from fd %d to command history.",
             STRPACKETTYPE(s1->scom_cmd),
             p->owner_dcb->fd);
#endif
    /** Copy to history list */
    if ((h1 = p->protocol_cmd_history) == NULL)
    {
        p->protocol_cmd_history = server_command_copy(s1);
    }
    else /*< scan and count history commands */
    {
        len = 1;

        while (h1->scom_next != NULL)
        {
            h1 = h1->scom_next;
            len += 1;
        }
        h1->scom_next = server_command_copy(s1);
    }

    /** Keep history limits, remove oldest */
    if (len > MAX_CMD_HISTORY)
    {
        server_command_t* c = p->protocol_cmd_history;
        p->protocol_cmd_history = p->protocol_cmd_history->scom_next;
        MXS_FREE(c);
    }

    /** Remove from command list */
    if (s1->scom_next == NULL)
    {
        p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
    }
    else
    {
        p->protocol_command = *(s1->scom_next);
        MXS_FREE(s1->scom_next);
    }

retblock:
    CHK_PROTOCOL(p);
}


/**
 * If router expects to get separate, complete statements, add MySQL command
 * to MySQLProtocol structure. It is removed when response has arrived.
 */
void protocol_add_srv_command(MySQLProtocol*     p,
                              mysql_server_cmd_t cmd)
{
#if defined(EXTRA_SS_DEBUG)
    server_command_t* c;
#endif

    if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
    {
        return;
    }
    /** this is the only server command in protocol */
    if (p->protocol_command.scom_cmd == MYSQL_COM_UNDEFINED)
    {
        /** write into structure */
        server_command_init(&p->protocol_command, cmd);
    }
    else
    {
        /** add to the end of list */
        p->protocol_command.scom_next = server_command_init(NULL, cmd);
    }
#if defined(EXTRA_SS_DEBUG)
    MXS_INFO("Added command %s to fd %d.",
             STRPACKETTYPE(cmd),
             p->owner_dcb->fd);

    c = &p->protocol_command;

    while (c != NULL && c->scom_cmd != MYSQL_COM_UNDEFINED)
    {
        MXS_INFO("fd %d : %d %s",
                 p->owner_dcb->fd,
                 c->scom_cmd,
                 STRPACKETTYPE(c->scom_cmd));
        c = c->scom_next;
    }
#endif
}


/**
 * If router processes separate statements, every stmt has corresponding MySQL
 * command stored in MySQLProtocol structure.
 *
 * Remove current (=oldest) command.
 */
void protocol_remove_srv_command(MySQLProtocol* p)
{
    server_command_t* s;

    s = &p->protocol_command;
#if defined(EXTRA_SS_DEBUG)
    MXS_INFO("Removed command %s from fd %d.",
             STRPACKETTYPE(s->scom_cmd),
             p->owner_dcb->fd);
#endif
    if (s->scom_next == NULL)
    {
        p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
    }
    else
    {
        p->protocol_command = *(s->scom_next);
        MXS_FREE(s->scom_next);
    }
}

mysql_server_cmd_t protocol_get_srv_command(MySQLProtocol* p,
                                            bool           removep)
{
    mysql_server_cmd_t cmd;

    cmd = p->protocol_command.scom_cmd;

    if (removep)
    {
        protocol_remove_srv_command(p);
    }
    MXS_DEBUG("Read command %s for fd %d.", STRPACKETTYPE(cmd), p->owner_dcb->fd);
    return cmd;
}

void mysql_num_response_packets(GWBUF *buf, uint8_t cmd, int *npackets, size_t *nbytes)
{
    uint8_t readbuf[3];
    int nparam = 0;
    int nattr = 0;

    /** Read command byte */
    gwbuf_copy_data(buf, MYSQL_HEADER_LEN, 1, readbuf);

    if (readbuf[0] == 0xff) /*< error */
    {
        *npackets = 1;
    }
    else
    {
        switch (cmd)
        {
        case MYSQL_COM_STMT_PREPARE:
            gwbuf_copy_data(buf, 9, 2, readbuf);
            nparam = gw_mysql_get_byte2(readbuf);
            gwbuf_copy_data(buf, 11, 2, readbuf);
            nattr = gw_mysql_get_byte2(readbuf);
            *npackets = 1 + nparam + MXS_MIN(1, nparam) + nattr + MXS_MIN(nattr, 1);
            break;

        case MYSQL_COM_QUIT:
        case MYSQL_COM_STMT_SEND_LONG_DATA:
        case MYSQL_COM_STMT_CLOSE:
            *npackets = 0; /*< these don't reply anything */
            break;

        default:
            /**
             * assume that other session commands respond
             * OK or ERR
             */
            *npackets = 1;
            break;
        }
    }

    gwbuf_copy_data(buf, 0, 3, readbuf);
    *nbytes = gw_mysql_get_byte3(readbuf) + MYSQL_HEADER_LEN;
}

/**
 * Examine command type and the readbuf. Conclude response packet count
 * from the command type or from the first packet content.  Fails if read
 * buffer doesn't include enough data to read the packet length.
 */
void init_response_status(GWBUF* buf, uint8_t cmd, int *npackets, size_t *nbytes_left)
{
    ss_dassert(gwbuf_length(buf) >= 3);
    mysql_num_response_packets(buf, cmd, npackets, nbytes_left);
    ss_dassert(*nbytes_left > 0);
    ss_dassert(*npackets > 0);
}

/**
 * Read how many packets are left from current response and how many bytes there
 * is still to be read from the current packet.
 */
bool protocol_get_response_status(MySQLProtocol* p,
                                  int* npackets,
                                  size_t* nbytes)
{
    bool succp;

    CHK_PROTOCOL(p);

    *npackets = p->protocol_command.scom_nresponse_packets;
    *nbytes   = (size_t)p->protocol_command.scom_nbytes_to_read;

    if (*npackets < 0 && *nbytes == 0)
    {
        succp = false;
    }
    else
    {
        succp = true;
    }

    return succp;
}

void protocol_set_response_status(MySQLProtocol* p,
                                  int npackets_left,
                                  size_t nbytes)
{
    CHK_PROTOCOL(p);

    p->protocol_command.scom_nbytes_to_read = nbytes;
    ss_dassert(p->protocol_command.scom_nbytes_to_read >= 0);

    p->protocol_command.scom_nresponse_packets = npackets_left;
}

char* create_auth_failed_msg(GWBUF*readbuf,
                             char* hostaddr,
                             uint8_t* sha1)
{
    char* errstr;
    char* uname = (char *)GWBUF_DATA(readbuf) + 5;
    const char* ferrstr = "Access denied for user '%s'@'%s' (using password: %s)";

    /** -4 comes from 2X'%s' minus terminating char */
    errstr = (char *)MXS_MALLOC(strlen(uname) + strlen(ferrstr) + strlen(hostaddr) + strlen("YES") - 6 + 1);

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
char *create_auth_fail_str(char *username,
                           char *hostaddr,
                           bool password,
                           char *db,
                           int errcode)
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
    errstr = (char *)MXS_MALLOC(strlen(username) + strlen(ferrstr) +
                                strlen(hostaddr) + strlen("YES") - 6 +
                                db_len + ((db_len > 0) ? (strlen(" to database ") + 2) : 0) + 1);

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
bool read_complete_packet(DCB *dcb, GWBUF **readbuf)
{
    bool rval = false;
    GWBUF *localbuf = NULL;

    if (dcb_read(dcb, &localbuf, 0) >= 0)
    {
        rval = true;
        dcb->last_read = hkheartbeat;
        GWBUF *packets = modutil_get_complete_packets(&localbuf);

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
    CHK_DCB(dcb);
    CHK_SESSION(dcb->session);


    if (dcb->session->state != SESSION_STATE_ALLOC &&
        dcb->session->state != SESSION_STATE_DUMMY)
    {
        memcpy(session, dcb->session->client_dcb->data, sizeof(MYSQL_session));
    }
    else
    {
        ss_dassert(false);
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
 */
int mxs_mysql_send_ok(DCB *dcb, int sequence, uint8_t affected_rows, const char* message)
{
    uint8_t *outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t *mysql_payload = NULL;
    uint8_t field_count = 0;
    uint8_t insert_id = 0;
    uint8_t mysql_server_status[2];
    uint8_t mysql_warning_counter[2];
    GWBUF *buf;


    mysql_payload_size =
        sizeof(field_count) +
        sizeof(affected_rows) +
        sizeof(insert_id) +
        sizeof(mysql_server_status) +
        sizeof(mysql_warning_counter);

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
static int response_length(bool with_ssl, bool ssl_established, char *user, uint8_t *passwd,
                char *dbname, const char *auth_module)
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
static void calculate_hash(uint8_t *scramble, uint8_t *passwd, uint8_t *output)
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
static uint8_t *
load_hashed_password(uint8_t *scramble, uint8_t *payload, uint8_t *passwd)
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
static uint32_t
create_capabilities(MySQLProtocol *conn, bool with_ssl, bool db_specified, bool compress)
{
    uint32_t final_capabilities;

    /** Copy client's flags to backend but with the known capabilities mask */
    final_capabilities = (conn->client_capabilities & (uint32_t)GW_MYSQL_CAPABILITIES_CLIENT);

    if (with_ssl)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL;
        /* Unclear whether we should include this */
        /* Maybe it should depend on whether CA certificate is provided */
        /* final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT; */
    }

    /* Compression is not currently supported */
    ss_dassert(!compress);
    if (compress)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_COMPRESS;
    }

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

GWBUF* gw_generate_auth_response(MXS_SESSION* session, MySQLProtocol *conn,
                                 bool with_ssl, bool ssl_established)
{
    MYSQL_session client;
    gw_get_shared_session_auth_info(session->client_dcb, &client);

    uint8_t client_capabilities[4] = {0, 0, 0, 0};
    uint8_t *curr_passwd = NULL;

    if (memcmp(client.client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) != 0)
    {
        curr_passwd = client.client_sha1;
    }

    uint32_t capabilities = create_capabilities(conn, with_ssl, client.db[0], false);
    gw_mysql_set_byte4(client_capabilities, capabilities);

    /**
     * Use the default authentication plugin name. If the server is using a
     * different authentication mechanism, it will send an AuthSwitchRequest
     * packet.
     */
    const char* auth_plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;

    long bytes = response_length(with_ssl, ssl_established, client.user,
                                 curr_passwd, client.db, auth_plugin_name);

    // allocating the GWBUF
    GWBUF *buffer = gwbuf_alloc(bytes);
    uint8_t *payload = GWBUF_DATA(buffer);

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
        memcpy(payload, client.user, strlen(client.user));
        payload += strlen(client.user);
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
        if (client.db[0])
        {
            memcpy(payload, client.db, strlen(client.db));
            payload += strlen(client.db);
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
mxs_auth_state_t gw_send_backend_auth(DCB *dcb)
{
    mxs_auth_state_t rval = MXS_AUTH_STATE_FAILED;

    if (dcb->session == NULL ||
        (dcb->session->state != SESSION_STATE_READY &&
         dcb->session->state != SESSION_STATE_ROUTER_READY) ||
        (dcb->server->server_ssl &&
         dcb->ssl_state == SSL_HANDSHAKE_FAILED))
    {
        return rval;
    }

    bool with_ssl = dcb->server->server_ssl;
    bool ssl_established = dcb->ssl_state == SSL_ESTABLISHED;

    GWBUF* buffer = gw_generate_auth_response(dcb->session, dcb->protocol,
                                              with_ssl, ssl_established);
    ss_dassert(buffer);

    if (with_ssl)
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

    uint8_t *curr_passwd = memcmp(local_session.client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) ?
        local_session.client_sha1 : null_client_sha1;

    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + GW_MYSQL_SCRAMBLE_SIZE);
    uint8_t* data = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(data, GW_MYSQL_SCRAMBLE_SIZE);
    data[3] = 2; // This is the third packet after the COM_CHANGE_USER
    calculate_hash(proto->scramble, curr_passwd, data + MYSQL_HEADER_LEN);

    return dcb_write(dcb, buffer);
}

/**
 * Decode mysql server handshake
 *
 * @param conn The MySQLProtocol structure
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 *
 */
int gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload)
{
    uint8_t *server_version_end = NULL;
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
    server_version_end = (uint8_t *) gw_strend((char*) payload);

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

    //Get capabilities_part 1 (2 bytes) + 1 language + 2 server_status
    payload += 5;

    mysql_server_capabilities_two = gw_mysql_get_byte2(payload);

    memcpy(capab_ptr, &mysql_server_capabilities_one, 2);

    // get capabilities part 2 (2 bytes)
    memcpy(&capab_ptr[2], &mysql_server_capabilities_two, 2);

    // 2 bytes shift
    payload += 2;

    // get scramble len
    if (payload[0] > 0)
    {
        scramble_len = payload[0] - 1;
        ss_dassert(scramble_len > GW_SCRAMBLE_LENGTH_323);
        ss_dassert(scramble_len <= GW_MYSQL_SCRAMBLE_SIZE);

        if ((scramble_len < GW_SCRAMBLE_LENGTH_323) ||
            scramble_len > GW_MYSQL_SCRAMBLE_SIZE)
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
bool gw_read_backend_handshake(DCB *dcb, GWBUF *buffer)
{
    MySQLProtocol *proto = (MySQLProtocol *)dcb->protocol;
    bool rval = false;
    uint8_t *payload = GWBUF_DATA(buffer) + 4;

    if (gw_decode_mysql_server_handshake(proto, payload) >= 0)
    {
        rval = true;
    }

    return rval;
}

bool mxs_mysql_is_ok_packet(GWBUF *buffer)
{
    uint8_t cmd = 0xff; // Default should differ from the OK packet
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    return cmd == MYSQL_REPLY_OK;
}

bool mxs_mysql_is_err_packet(GWBUF *buffer)
{
    uint8_t cmd = 0x00; // Default should differ from the ERR packet
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    return cmd == MYSQL_REPLY_ERR;
}

bool mxs_mysql_is_result_set(GWBUF *buffer)
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

bool mxs_mysql_is_prep_stmt_ok(GWBUF *buffer)
{
    bool rval = false;
    uint8_t cmd;

    if (gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd) &&
        cmd == MYSQL_REPLY_OK)
    {
        rval = true;
    }

    return rval;
}

bool mxs_mysql_more_results_after_ok(GWBUF *buffer)
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

mysql_server_cmd_t mxs_mysql_current_command(MXS_SESSION* session)
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

uint8_t mxs_mysql_get_command(GWBUF* buffer)
{
    if (GWBUF_LENGTH(buffer) > MYSQL_HEADER_LEN)
    {
        return GWBUF_DATA(buffer)[4];
    }
    else
    {
        uint8_t command = 0;
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &command);
        return command;
    }
}

bool mxs_mysql_extract_ps_response(GWBUF* buffer, MXS_PS_RESPONSE* out)
{
    bool rval = false;
    uint8_t id[MYSQL_PS_ID_SIZE];
    uint8_t cols[MYSQL_PS_ID_SIZE];
    uint8_t params[MYSQL_PS_ID_SIZE];
    uint8_t warnings[MYSQL_PS_WARN_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id) == sizeof(id) &&
        gwbuf_copy_data(buffer, MYSQL_PS_COLS_OFFSET, sizeof(cols), cols) == sizeof(cols) &&
        gwbuf_copy_data(buffer, MYSQL_PS_PARAMS_OFFSET, sizeof(params), params) == sizeof(params) &&
        gwbuf_copy_data(buffer, MYSQL_PS_WARN_OFFSET, sizeof(warnings), warnings) == sizeof(warnings))
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
    return cmd != MYSQL_COM_STMT_SEND_LONG_DATA &&
           cmd != MYSQL_COM_QUIT &&
           cmd != MYSQL_COM_STMT_CLOSE &&
           cmd != MYSQL_COM_STMT_FETCH;
}
