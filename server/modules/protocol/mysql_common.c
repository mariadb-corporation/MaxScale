/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 *
 * Revision History
 * Date         Who                     Description
 * 17/06/2013   Massimiliano Pinto      Common MySQL protocol routines
 * 02/06/2013   Massimiliano Pinto      MySQL connect asynchronous phases
 * 04/09/2013   Massimiliano Pinto      Added dcb NULL assert in mysql_send_custom_error
 * 12/09/2013   Massimiliano Pinto      Added checks in gw_decode_mysql_server_handshake and
 *                                      gw_read_backend_handshake
 * 10/02/2014   Massimiliano Pinto      Added MySQL Authentication with user@host
 * 10/09/2014   Massimiliano Pinto      Added MySQL Authentication option enabling localhost
 *                                      match with any host (wildcard %)
 *                                      Backend server configuration may differ so default is 0,
 *                                      don't match and an explicit
 *                                      localhost entry should be added for the selected user
 *                                      in the backends.
 *                                      Setting to 1 allow localhost (127.0.0.1 or socket) to
 *                                      match the any host grant via
 *                                      user@%
 * 29/09/2014   Massimiliano Pinto      Added Mysql user@host authentication with wildcard in IPv4 hosts:
 *                                      x.y.z.%, x.y.%.%, x.%.%.%
 * 03/10/2014   Massimiliano Pinto      Added netmask for wildcard in IPv4 hosts.
 * 24/10/2014   Massimiliano Pinto      Added Mysql user@host @db authentication support
 * 10/11/2014   Massimiliano Pinto      Charset at connect is passed to backend during authentication
 * 07/07/2015   Martin Brampton         Fix problem recognising null password
 * 07/02/2016   Martin Brampton         Remove authentication functions to mysql_auth.c
 * 31/05/2016   Martin Brampton         Add mysql_create_standard_error function
 *
 */

#include <gw.h>
#include <utils.h>
#include <mysql_client_server_protocol.h>
#include <maxscale/alloc.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <netinet/tcp.h>
#include <modutil.h>

uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN] = "";

static server_command_t* server_command_init(server_command_t* srvcmd, mysql_server_cmd_t cmd);

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
 * mysql_protocol_done
 *
 * free protocol allocations.
 *
 * @param dcb owner DCB
 *
 */
void mysql_protocol_done(DCB* dcb)
{
    MySQLProtocol* p;
    server_command_t* scmd;
    server_command_t* scmd2;

    p = (MySQLProtocol *)dcb->protocol;

    spinlock_acquire(&p->protocol_lock);

    if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
    {
        goto retblock;
    }
    scmd = p->protocol_cmd_history;

    while (scmd != NULL)
    {
        scmd2 = scmd->scom_next;
        MXS_FREE(scmd);
        scmd = scmd2;
    }
    p->protocol_state = MYSQL_PROTOCOL_DONE;

retblock:
    spinlock_release(&p->protocol_lock);
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
    switch(state)
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

    if (dcb == NULL || dcb->state == DCB_STATE_ZOMBIE)
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
    mysql_statemsg[0]='#';
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
    mysql_packet_header[3]= 0;
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
        MXS_DEBUG("%lu [mysql_send_auth_error] dcb %p is in a state %s, "
                  "and it is not in epoll set anymore. Skip error sending.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
        return 0;
    }
    mysql_error_msg = "Access denied!";
    mysql_state = "28000";

    field_count = 0xff;
    gw_mysql_set_byte2(mysql_err, /*mysql_errno */ 1045);
    mysql_statemsg[0]='#';
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
    packetlen = MYSQL_GET_PACKET_LEN(data) + 4;

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

    spinlock_acquire(&p->protocol_lock);

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
    spinlock_release(&p->protocol_lock);
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
    spinlock_acquire(&p->protocol_lock);

    if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
    {
        goto retblock;
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
retblock:
    spinlock_release(&p->protocol_lock);
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
    spinlock_acquire(&p->protocol_lock);
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

    spinlock_release(&p->protocol_lock);
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
    MXS_DEBUG("%lu [protocol_get_srv_command] Read command %s for fd %d.",
              pthread_self(),
              STRPACKETTYPE(cmd),
              p->owner_dcb->fd);
    return cmd;
}


/**
 * Examine command type and the readbuf. Conclude response
 * packet count from the command type or from the first packet
 * content.
 * Fails if read buffer doesn't include enough data to read the
 * packet length.
 */
void init_response_status(GWBUF*             buf,
                          mysql_server_cmd_t cmd,
                          int*               npackets,
                          ssize_t*           nbytes_left)
{
    uint8_t readbuf[3];
    int nparam = 0;
    int nattr = 0;
    uint8_t* data;

    ss_dassert(gwbuf_length(buf) >= 3);

    /** Read command byte */
    gwbuf_copy_data(buf, 4, 1, readbuf);

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
            *npackets = 1 + nparam + MIN(1, nparam) + nattr + MIN(nattr, 1);
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
    *nbytes_left = gw_mysql_get_byte3(readbuf) + MYSQL_HEADER_LEN;
    /**
     * There is at least one complete packet in the buffer so buffer is bigger
     * than packet
     */
    ss_dassert(*nbytes_left > 0);
    ss_dassert(*npackets > 0);
}



/**
 * Read how many packets are left from current response and how many bytes there
 * is still to be read from the current packet.
 */
bool protocol_get_response_status(MySQLProtocol* p,
                                  int* npackets,
                                  ssize_t* nbytes)
{
    bool succp;

    CHK_PROTOCOL(p);

    spinlock_acquire(&p->protocol_lock);
    *npackets = p->protocol_command.scom_nresponse_packets;
    *nbytes   = (ssize_t)p->protocol_command.scom_nbytes_to_read;
    spinlock_release(&p->protocol_lock);

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
                                  ssize_t nbytes)
{
    CHK_PROTOCOL(p);

    spinlock_acquire(&p->protocol_lock);

    p->protocol_command.scom_nbytes_to_read = nbytes;
    ss_dassert(p->protocol_command.scom_nbytes_to_read >= 0);

    p->protocol_command.scom_nresponse_packets = npackets_left;

    spinlock_release(&p->protocol_lock);
}

char* create_auth_failed_msg(GWBUF*readbuf,
                             char* hostaddr,
                             uint8_t* sha1)
{
    char* errstr;
    char* uname=(char *)GWBUF_DATA(readbuf) + 5;
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
 * @param       username        the MySQL user
 * @param       hostaddr        the client IP
 * @param       sha1            authentication scramble data
 * @param       db              the MySQL db to connect to
 *
 * @return      Pointer to the allocated string or NULL on failure
 */
char *create_auth_fail_str(char *username,
                           char *hostaddr,
                           char *sha1,
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
        sprintf(errstr, ferrstr, username, hostaddr, (*sha1 == '\0' ? "NO" : "YES"), db);
    }
    else if (errcode == MXS_AUTH_FAILED_SSL)
    {
        sprintf(errstr, "%s", ferrstr);
    }
    else
    {
        sprintf(errstr, ferrstr, username, hostaddr, (*sha1 == '\0' ? "NO" : "YES"));
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
            spinlock_acquire(&dcb->authlock);
            dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, localbuf);
            spinlock_release(&dcb->authlock);
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

    spinlock_acquire(&dcb->session->ses_lock);

    if (dcb->session->state != SESSION_STATE_ALLOC &&
        dcb->session->state != SESSION_STATE_DUMMY)
    {
        memcpy(session, dcb->session->client_dcb->data, sizeof(MYSQL_session));
    }
    else
    {
        ss_dassert(false);
        MXS_ERROR("%lu [gw_get_shared_session_auth_info] Couldn't get "
                  "session authentication info. Session in a wrong state %d.",
                  pthread_self(), dcb->session->state);
        rval = false;
    }
    spinlock_release(&dcb->session->ses_lock);
    return rval;
}
