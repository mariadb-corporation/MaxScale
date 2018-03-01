/*
 *
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mysql_client.c
 *
 * MySQL Protocol module for handling the protocol between the gateway
 * and the client.
 *
 * Revision History
 * Date         Who                     Description
 * 14/06/2013   Mark Riddoch            Initial version
 * 17/06/2013   Massimiliano Pinto      Added Client To MaxScale routines
 * 24/06/2013   Massimiliano Pinto      Added: fetch passwords from service users' hashtable
 * 02/09/2013   Massimiliano Pinto      Added: session refcount
 * 16/12/2013   Massimiliano Pinto      Added: client closed socket detection with recv(..., MSG_PEEK)
 * 24/02/2014   Massimiliano Pinto      Added: on failed authentication a new users' table is loaded
 *                                      with time and frequency limitations
 *                                      If current user is authenticated the new users' table will
 *                                      replace the old one
 * 28/02/2014   Massimiliano Pinto      Added: client IPv4 in dcb->ipv4 and inet_ntop for string
 *                                      representation
 * 11/03/2014   Massimiliano Pinto      Added: Unix socket support
 * 07/05/2014   Massimiliano Pinto      Added: specific version string in server handshake
 * 09/09/2014   Massimiliano Pinto      Added: 777 permission for socket path
 * 13/10/2014   Massimiliano Pinto      Added: dbname authentication check
 * 10/11/2014   Massimiliano Pinto      Added: client charset added to protocol struct
 * 29/05/2015   Markus Makela           Added SSL support
 * 11/06/2015   Martin Brampton         COM_QUIT suppressed for persistent connections
 * 04/09/2015   Martin Brampton         Introduce DUMMY session to fulfill guarantee DCB always has session
 * 09/09/2015   Martin Brampton         Modify error handler calls
 * 11/01/2016   Martin Brampton         Remove SSL write code, now handled at lower level;
 *                                      replace gwbuf_consume by gwbuf_free (multiple).
 * 07/02/2016   Martin Brampton         Split off authentication and SSL.
 * 31/05/2016   Martin Brampton         Implement connection throttling
 */

#define MXS_MODULE_NAME "MySQLClient"

#include <maxscale/protocol.h>
#include <maxscale/alloc.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/ssl.h>
#include <maxscale/poll.h>
#include <maxscale/modinfo.h>
#include <sys/stat.h>
#include <maxscale/modutil.h>
#include <netinet/tcp.h>
#include <maxscale/query_classifier.h>
#include <maxscale/authenticator.h>
#include <maxscale/session.h>

static int process_init(void);
static void process_finish(void);
static int thread_init(void);
static void thread_finish(void);

static int gw_MySQLAccept(DCB *listener);
static int gw_MySQLListener(DCB *listener, char *config_bind);
static int gw_read_client_event(DCB* dcb);
static int gw_write_client_event(DCB *dcb);
static int gw_MySQLWrite_client(DCB *dcb, GWBUF *queue);
static int gw_error_client_event(DCB *dcb);
static int gw_client_close(DCB *dcb);
static int gw_client_hangup_event(DCB *dcb);
static char *gw_default_auth();
static int gw_connection_limit(DCB *dcb, int limit);
static int MySQLSendHandshake(DCB* dcb);
static int route_by_statement(MXS_SESSION *, uint64_t, GWBUF **);
static void mysql_client_auth_error_handling(DCB *dcb, int auth_val, int packet_number);
static int gw_read_do_authentication(DCB *dcb, GWBUF *read_buffer, int nbytes_read);
static int gw_read_normal_data(DCB *dcb, GWBUF *read_buffer, int nbytes_read);
static int gw_read_finish_processing(DCB *dcb, GWBUF *read_buffer, uint64_t capabilities);
static bool ensure_complete_packet(DCB *dcb, GWBUF **read_buffer, int nbytes_read);
static void gw_process_one_new_client(DCB *client_dcb);

/*
 * The "module object" for the mysqld client protocol module.
 */


/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_PROTOCOL MyObject =
    {
        gw_read_client_event,                   /* Read - EPOLLIN handler        */
        gw_MySQLWrite_client,                   /* Write - data from gateway     */
        gw_write_client_event,                  /* WriteReady - EPOLLOUT handler */
        gw_error_client_event,                  /* Error - EPOLLERR handler      */
        gw_client_hangup_event,                 /* HangUp - EPOLLHUP handler     */
        gw_MySQLAccept,                         /* Accept                        */
        NULL,                                   /* Connect                       */
        gw_client_close,                        /* Close                         */
        gw_MySQLListener,                       /* Listen                        */
        NULL,                                   /* Authentication                */
        NULL,                                   /* Session                       */
        gw_default_auth,                        /* Default authenticator         */
        gw_connection_limit,                    /* Send error connection limit   */
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_GA,
        MXS_PROTOCOL_VERSION,
        "The client to MaxScale MySQL protocol implementation",
        "V1.1.0",
        &MyObject,
        process_init,
        process_finish,
        thread_init,
        thread_finish,
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
/*lint +e14 */

/**
 * Performs process wide initialization.
 *
 * @return 0 if successful, non-zero otherwise.
 */
static int process_init(void)
{
    int rv = mysql_library_init(0, NULL, NULL);

    if (rv != 0)
    {
        MXS_ERROR("MySQL initialization failed, MariaDB MaxScale will exit. "
                  "MySQL Error: %d, %s.", mysql_errno(NULL), mysql_error(NULL));
    }

    return rv;
}

/**
 * Performs process wide finalization.
 */
static void process_finish(void)
{
    mysql_library_end();
}

/**
 * Performs thread-specific initialization.
 *
 * @return 0 if successful, non-zero otherwise.
 */
static int thread_init(void)
{
    int rv = mysql_thread_init();

    if (rv != 0)
    {
        MXS_ERROR("MySQL thread initialization failed, the thread will exit.");
    }

    return rv;
}

/**
 * Performs thread specific finalization.
 */
static void thread_finish(void)
{
    mysql_thread_end();
}

/**
 * The default authenticator name for this protocol
 *
 * @return name of authenticator
 */
static char *gw_default_auth()
{
    return "MySQLAuth";
}

/**
 * MySQLSendHandshake
 *
 * @param dcb The descriptor control block to use for sending the handshake request
 * @return      The packet length sent
 */
int MySQLSendHandshake(DCB* dcb)
{
    uint8_t *outbuf = NULL;
    uint32_t mysql_payload_size = 0;
    uint8_t mysql_packet_header[4];
    uint8_t mysql_packet_id = 0;
    /* uint8_t mysql_filler = GW_MYSQL_HANDSHAKE_FILLER; not needed*/
    uint8_t mysql_protocol_version = GW_MYSQL_PROTOCOL_VERSION;
    uint8_t *mysql_handshake_payload = NULL;
    uint8_t mysql_thread_id_num[4];
    uint8_t mysql_scramble_buf[9] = "";
    uint8_t mysql_plugin_data[13] = "";
    uint8_t mysql_server_capabilities_one[2];
    uint8_t mysql_server_capabilities_two[2];
    uint8_t mysql_server_language = 8;
    uint8_t mysql_server_status[2];
    uint8_t mysql_scramble_len = 21;
    uint8_t mysql_filler_ten[10] = {};
    /* uint8_t mysql_last_byte = 0x00; not needed */
    char server_scramble[GW_MYSQL_SCRAMBLE_SIZE + 1] = "";
    char *version_string;
    int len_version_string = 0;
    int id_num;

    bool is_maria = false;

    if (dcb->service->dbref)
    {
        mysql_server_language = dcb->service->dbref->server->charset;

        if (dcb->service->dbref->server->server_string &&
            strstr(dcb->service->dbref->server->server_string, "10.2."))
        {
            /** The backend servers support the extended capabilities */
            is_maria = true;
        }
    }

    MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    GWBUF *buf;

    /* get the version string from service property if available*/
    if (dcb->service->version_string != NULL)
    {
        version_string = dcb->service->version_string;
        len_version_string = strlen(version_string);
    }
    else
    {
        version_string = GW_MYSQL_VERSION;
        len_version_string = strlen(GW_MYSQL_VERSION);
    }

    gw_generate_random_str(server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    // copy back to the caller
    memcpy(protocol->scramble, server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    if (is_maria)
    {
        /**
         * The new 10.2 capability flags are stored in the last 4 bytes of the
         * 10 byte filler block.
         */
        uint32_t new_flags = MXS_MARIA_CAP_STMT_BULK_OPERATIONS;
        memcpy(mysql_filler_ten + 6, &new_flags, sizeof(new_flags));
    }

    // thread id, now put thePID
    id_num = getpid() + dcb->fd;
    gw_mysql_set_byte4(mysql_thread_id_num, id_num);

    memcpy(mysql_scramble_buf, server_scramble, 8);

    memcpy(mysql_plugin_data, server_scramble + 8, 12);

    /**
     * Use the default authentication plugin name in the initial handshake. If the
     * authenticator needs to change the authentication method, it should send
     * an AuthSwitchRequest packet to the client.
     */
    const char* plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;
    int plugin_name_len = strlen(plugin_name);

    mysql_payload_size =
        sizeof(mysql_protocol_version) + (len_version_string + 1) + sizeof(mysql_thread_id_num) + 8 +
        sizeof(/* mysql_filler */ uint8_t) + sizeof(mysql_server_capabilities_one) + sizeof(mysql_server_language) +
        sizeof(mysql_server_status) + sizeof(mysql_server_capabilities_two) + sizeof(mysql_scramble_len) +
        sizeof(mysql_filler_ten) + 12 + sizeof(/* mysql_last_byte */ uint8_t) + plugin_name_len +
        sizeof(/* mysql_last_byte */ uint8_t);

    // allocate memory for packet header + payload
    if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
    {
        ss_dassert(buf != NULL);
        return 0;
    }
    outbuf = GWBUF_DATA(buf);

    // write packet header with mysql_payload_size
    gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);

    // write packet number, now is 0
    mysql_packet_header[3] = mysql_packet_id;
    memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

    // current buffer pointer
    mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

    // write protocol version
    memcpy(mysql_handshake_payload, &mysql_protocol_version, sizeof(mysql_protocol_version));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_protocol_version);

    // write server version plus 0 filler
    strcpy((char *)mysql_handshake_payload, version_string);
    mysql_handshake_payload = mysql_handshake_payload + len_version_string;

    *mysql_handshake_payload = 0x00;

    mysql_handshake_payload++;

    // write thread id
    memcpy(mysql_handshake_payload, mysql_thread_id_num, sizeof(mysql_thread_id_num));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_thread_id_num);

    // write scramble buf
    memcpy(mysql_handshake_payload, mysql_scramble_buf, 8);
    mysql_handshake_payload = mysql_handshake_payload + 8;
    *mysql_handshake_payload = GW_MYSQL_HANDSHAKE_FILLER;
    mysql_handshake_payload++;

    // write server capabilities part one
    mysql_server_capabilities_one[0] = (uint8_t)GW_MYSQL_CAPABILITIES_SERVER;
    mysql_server_capabilities_one[1] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 8);

    // Check that we match the old values
    ss_dassert(mysql_server_capabilities_one[0] = 0xff);
    ss_dassert(mysql_server_capabilities_one[1] = 0xf7);

    if (is_maria)
    {
        /** A MariaDB 10.2 server doesn't send the CLIENT_MYSQL capability
         * to signal that it supports extended capabilities */
        mysql_server_capabilities_one[0] &= ~(uint8_t)GW_MYSQL_CAPABILITIES_CLIENT_MYSQL;
    }

    if (ssl_required_by_dcb(dcb))
    {
        mysql_server_capabilities_one[1] |= (int)GW_MYSQL_CAPABILITIES_SSL >> 8;
    }

    memcpy(mysql_handshake_payload, mysql_server_capabilities_one, sizeof(mysql_server_capabilities_one));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_one);

    // write server language
    memcpy(mysql_handshake_payload, &mysql_server_language, sizeof(mysql_server_language));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_language);

    //write server status
    mysql_server_status[0] = 2;
    mysql_server_status[1] = 0;
    memcpy(mysql_handshake_payload, mysql_server_status, sizeof(mysql_server_status));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_status);

    //write server capabilities part two
    mysql_server_capabilities_two[0] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 16);
    mysql_server_capabilities_two[1] = (uint8_t)(GW_MYSQL_CAPABILITIES_SERVER >> 24);

    // Check that we match the old values
    ss_dassert(mysql_server_capabilities_two[0] == 15);
    /** NOTE: pre-2.1 versions sent the fourth byte of the capabilities as
     the value 128 even though there's no such capability. */

    memcpy(mysql_handshake_payload, mysql_server_capabilities_two, sizeof(mysql_server_capabilities_two));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_two);

    // write scramble_len
    memcpy(mysql_handshake_payload, &mysql_scramble_len, sizeof(mysql_scramble_len));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_scramble_len);

    //write 10 filler
    memcpy(mysql_handshake_payload, mysql_filler_ten, sizeof(mysql_filler_ten));
    mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_filler_ten);

    // write plugin data
    memcpy(mysql_handshake_payload, mysql_plugin_data, 12);
    mysql_handshake_payload = mysql_handshake_payload + 12;

    //write last byte, 0
    *mysql_handshake_payload = 0x00;
    mysql_handshake_payload++;

    // to be understanded ????
    memcpy(mysql_handshake_payload, plugin_name, plugin_name_len);
    mysql_handshake_payload = mysql_handshake_payload + plugin_name_len;

    //write last byte, 0
    *mysql_handshake_payload = 0x00;

    // writing data in the Client buffer queue
    dcb->func.write(dcb, buf);

    return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * Write function for client DCB: writes data from MaxScale to Client
 *
 * @param dcb   The DCB of the client
 * @param queue Queue of buffers to write
 */
int gw_MySQLWrite_client(DCB *dcb, GWBUF *queue)
{
    return dcb_write(dcb, queue);
}

/**
 * @brief Client read event triggered by EPOLLIN
 *
 * @param dcb   Descriptor control block
 * @return 0 if succeed, 1 otherwise
 */
int gw_read_client_event(DCB* dcb)
{
    MySQLProtocol *protocol;
    GWBUF *read_buffer = NULL;
    int return_code = 0;
    int nbytes_read = 0;
    int max_bytes = 0;

    CHK_DCB(dcb);
    if (dcb->dcb_role != DCB_ROLE_CLIENT_HANDLER)
    {
        MXS_ERROR("DCB must be a client handler for MySQL client protocol.");
        return 1;
    }

    protocol = (MySQLProtocol *)dcb->protocol;
    CHK_PROTOCOL(protocol);

#ifdef SS_DEBUG
    MXS_DEBUG("[gw_read_client_event] Protocol state: %s",
              gw_mysql_protocol_state2string(protocol->protocol_auth_state));

#endif

    /**
     * The use of max_bytes seems like a hack, but no better option is available
     * at the time of writing. When a MySQL server receives a new connection
     * request, it sends an Initial Handshake Packet. Where the client wants to
     * use SSL, it responds with an SSL Request Packet (in place of a Handshake
     * Response Packet). The SSL Request Packet contains only the basic header,
     * and not the user credentials. It is 36 bytes long.  The server then
     * initiates the SSL handshake (via calls to OpenSSL).
     *
     * In many cases, this is what happens. But occasionally, the client seems
     * to send a packet much larger than 36 bytes (in tests it was 333 bytes).
     * If the whole of the packet is read, it is then lost to the SSL handshake
     * process. Why this happens is presently unknown. Reading just 36 bytes
     * when the server requires SSL and SSL has not yet been negotiated seems
     * to solve the problem.
     *
     * If a neater solution can be found, so much the better.
     */
    if (ssl_required_but_not_negotiated(dcb))
    {
        max_bytes = 36;
    }
    return_code = dcb_read(dcb, &read_buffer, max_bytes);
    if (return_code < 0)
    {
        dcb_close(dcb);
    }
    if (0 == (nbytes_read = gwbuf_length(read_buffer)))
    {
        return return_code;
    }

    return_code = 0;

    switch (protocol->protocol_auth_state)
    {
    /**
     *
     * When a listener receives a new connection request, it creates a
     * request handler DCB to for the client connection. The listener also
     * sends the initial authentication request to the client. The first
     * time this function is called from the poll loop, the client reply
     * to the authentication request should be available.
     *
     * If the authentication is successful the protocol authentication state
     * will be changed to MYSQL_IDLE (see below).
     *
     */
    case MXS_AUTH_STATE_MESSAGE_READ:
        if (nbytes_read < 3 ||
            (0 == max_bytes && nbytes_read < MYSQL_GET_PACKET_LEN(read_buffer)) ||
            (0 != max_bytes && nbytes_read < max_bytes))
        {
            dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
        }
        else
        {
            if (nbytes_read > MYSQL_GET_PACKET_LEN(read_buffer))
            {
                // We read more data than was needed
                dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
                read_buffer = modutil_get_next_MySQL_packet(&dcb->dcb_readqueue);
            }

            return_code = gw_read_do_authentication(dcb, read_buffer, nbytes_read);
        }
        break;

    /**
     *
     * Once a client connection is authenticated, the protocol authentication
     * state will be MYSQL_IDLE and so every event of data received will
     * result in a call that comes to this section of code.
     *
     */
    case MXS_AUTH_STATE_COMPLETE:
        /* After this call read_buffer will point to freed data */
        return_code = gw_read_normal_data(dcb, read_buffer, nbytes_read);
        break;

    case MXS_AUTH_STATE_FAILED:
        gwbuf_free(read_buffer);
        return_code = 1;
        break;

    default:
        MXS_ERROR("In mysql_client.c unexpected protocol authentication state");
        break;
    }

    return return_code;
}

/**
 * @brief Store client connection information into the DCB
 * @param dcb Client DCB
 * @param buffer Buffer containing the handshake response packet
 */
static void store_client_information(DCB *dcb, GWBUF *buffer)
{
    size_t len = gwbuf_length(buffer);
    uint8_t data[len];
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    MYSQL_session *ses = (MYSQL_session*)dcb->data;

    gwbuf_copy_data(buffer, 0, len, data);
    ss_dassert(MYSQL_GET_PAYLOAD_LEN(data) + MYSQL_HEADER_LEN == len ||
               len == MYSQL_AUTH_PACKET_BASE_SIZE); // For SSL request packet

    proto->client_capabilities = gw_mysql_get_byte4(data + MYSQL_CLIENT_CAP_OFFSET);
    proto->charset = data[MYSQL_CHARSET_OFFSET];

    /** MariaDB 10.2 compatible clients don't set the first bit to signal that
     * there are extra capabilities stored in the last 4 bytes of the 23 byte filler. */
    if ((proto->client_capabilities & GW_MYSQL_CAPABILITIES_CLIENT_MYSQL) == 0)
    {
        proto->extra_capabilities = gw_mysql_get_byte4(data + MARIADB_CAP_OFFSET);
    }

    if (len > MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        strcpy(ses->user, (char*)data + MYSQL_AUTH_PACKET_BASE_SIZE);

        if (proto->client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB)
        {
            /** Client supports default database on connect */
            size_t userlen = strlen(ses->user) + 1;

            /** Skip the authentication token, it is handled by the authenticators */
            uint8_t authlen = data[MYSQL_AUTH_PACKET_BASE_SIZE + userlen];

            size_t dboffset = MYSQL_AUTH_PACKET_BASE_SIZE + userlen + authlen + 1;

            if (data[dboffset])
            {
                /** Client is connecting with a default database */
                strcpy(ses->db, (char*)data + dboffset);
            }
        }
    }
}

/**
 * @brief Debug check function for authentication packets
 *
 * Check that the packet is consistent with how the protocol works and that no
 * unexpected data is processed.
 *
 * @param dcb Client DCB
 * @param buf Buffer containing packet
 * @param bytes Number of bytes available
 */
static void check_packet(DCB *dcb, GWBUF *buf, int bytes)
{
    uint8_t hdr[MYSQL_HEADER_LEN];
    ss_dassert(gwbuf_copy_data(buf, 0, MYSQL_HEADER_LEN, hdr) == MYSQL_HEADER_LEN);

    int buflen = gwbuf_length(buf);
    int pktlen = MYSQL_GET_PAYLOAD_LEN(hdr) + MYSQL_HEADER_LEN;

    if (bytes == MYSQL_AUTH_PACKET_BASE_SIZE)
    {
        /** This is an SSL request packet */
        ss_dassert(dcb->listener->ssl);
        ss_dassert(buflen == bytes && pktlen >= buflen);
    }
    else
    {
        /** Normal packet */
        ss_dassert(buflen == pktlen);
    }
}

/**
 * @brief Client read event, process when client not yet authenticated
 *
 * @param dcb           Descriptor control block
 * @param read_buffer   A buffer containing the data read from client
 * @param nbytes_read   The number of bytes of data read
 * @return 0 if succeed, 1 otherwise
 */
static int
gw_read_do_authentication(DCB *dcb, GWBUF *read_buffer, int nbytes_read)
{
    ss_debug(check_packet(dcb, read_buffer, nbytes_read));

    /** Allocate the shared session structure */
    if (dcb->data == NULL && (dcb->data = mysql_session_alloc()) == NULL)
    {
        dcb_close(dcb);
        return 1;
    }

    /** Read the client's packet sequence and increment that by one */
    uint8_t next_sequence;
    gwbuf_copy_data(read_buffer, MYSQL_SEQ_OFFSET, 1, &next_sequence);

    if (next_sequence == 1 || (ssl_required_by_dcb(dcb) && next_sequence == 2))
    {
        /** This is the first response from the client, read the connection
         * information and store them in the shared structure. For SSL connections,
         * this will be packet number two since the first packet will be the
         * Protocol::SSLRequest packet.
         *
         * @see https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::SSLRequest
         */
        store_client_information(dcb, read_buffer);
    }

    next_sequence++;

    /**
     * The first step in the authentication process is to extract the
     * relevant information from the buffer supplied and place it
     * into a data structure pointed to by the DCB.  The "success"
     * result is not final, it implies only that the process is so
     * far successful, not that authentication has completed.  If the
     * data extraction succeeds, then a call is made to the actual
     * authenticate function to carry out the user checks.
     */
    int auth_val = dcb->authfunc.extract(dcb, read_buffer);

    if (MXS_AUTH_SUCCEEDED == auth_val)
    {
        auth_val = dcb->authfunc.authenticate(dcb);
    }

    MySQLProtocol *protocol = (MySQLProtocol *)dcb->protocol;

    /**
     * At this point, if the auth_val return code indicates success
     * the user authentication has been successfully completed.
     * But in order to have a working connection, a session has to
     * be created.  Provided that is also successful (indicated by a
     * non-null session) then the whole process has succeeded. In all
     * other cases an error return is made.
     */
    if (MXS_AUTH_SUCCEEDED == auth_val)
    {
        if (dcb->user == NULL)
        {
            /** User authentication complete, copy the username to the DCB */
            MYSQL_session *ses = dcb->data;
            if ((dcb->user = MXS_STRDUP(ses->user)) == NULL)
            {
                dcb_close(dcb);
                gwbuf_free(read_buffer);
                return 0;
            }
        }

        protocol->protocol_auth_state = MXS_AUTH_STATE_RESPONSE_SENT;
        /**
         * Create session, and a router session for it.
         * If successful, there will be backend connection(s)
         * after this point. The protocol authentication state
         * is changed so that future data will go through the
         * normal data handling function instead of this one.
         */
        MXS_SESSION *session = session_alloc(dcb->service, dcb);

        if (session != NULL)
        {
            CHK_SESSION(session);
            ss_dassert(session->state != SESSION_STATE_ALLOC &&
                       session->state != SESSION_STATE_DUMMY);
            protocol->protocol_auth_state = MXS_AUTH_STATE_COMPLETE;
            mxs_mysql_send_ok(dcb, next_sequence, 0, NULL);
        }
        else
        {
            auth_val = MXS_AUTH_NO_SESSION;
        }
    }
    /**
     * If we did not get success throughout or authentication is not yet complete,
     * then the protocol state is updated, the client is notified of the failure
     * and the DCB is closed.
     */
    if (MXS_AUTH_SUCCEEDED != auth_val &&
        MXS_AUTH_INCOMPLETE != auth_val &&
        MXS_AUTH_SSL_INCOMPLETE != auth_val)
    {
        protocol->protocol_auth_state = MXS_AUTH_STATE_FAILED;
        mysql_client_auth_error_handling(dcb, auth_val, next_sequence);
        /**
         * Close DCB and which will release MYSQL_session
         */
        dcb_close(dcb);
    }
    /* One way or another, the buffer is now fully processed */
    gwbuf_free(read_buffer);
    return 0;
}

/**
 * Helper function to split and store the buffer
 * @param client_dcb Client DCB
 * @param queue Buffer to split
 * @param offset Offset where the split is made
 * @return The first part of the buffer
 */
static GWBUF* split_and_store(DCB *client_dcb, GWBUF* queue, int offset)
{
    GWBUF* newbuf = gwbuf_split(&queue, offset);
    dcb_append_readqueue(client_dcb, queue);
    return newbuf;
}

/**
 * @brief Check if the DCB is idle from the protocol's point of view
 *
 * This checks if all expected data from the DCB has been read. The values
 * prefixed with @c protocol_ should be manipulated by the protocol modules.
 *
 * @param dcb DCB to check
 * @return True if the DCB protocol is not expecting any data
 */
static bool protocol_is_idle(DCB *dcb)
{
    return dcb->protocol_bytes_processed == dcb->protocol_packet_length;
}

/**
 * @brief Process the commands the client is executing
 *
 * The data read from the network is not guaranteed to contain a complete MySQL
 * packet. This means that it is possible that a command sent by the client is
 * split across multiple network packets and those packets need to be processed
 * individually.
 *
 * The forwarding of the data to the routers starts once the length and command
 * bytes have been read. The @c current_command field of the protocol
 * structure is guaranteed to always represent the current command being executed
 * by the client.
 *
 * Currently the gathered information is used by the readconnroute module to
 * detect COM_CHANGE_USER packets.
 *
 * @param dcb Client MySQL protocol struct
 * @param bytes_available Number of bytes available
 * @param queue Data written by the client
 * @return True if routing can proceed, false if processing should be attempted
 * later when more data is available
 */
static bool process_client_commands(DCB* dcb, int bytes_available, GWBUF** buffer)
{
    GWBUF* queue = *buffer;

    /** Make sure we have enough data if the client is sending a new command */
    if (protocol_is_idle(dcb) && bytes_available < MYSQL_HEADER_LEN)
    {
        dcb_append_readqueue(dcb, queue);
        return false;
    }

    int offset = 0;

    while (bytes_available)
    {
        if (protocol_is_idle(dcb))
        {
            int pktlen;
            uint8_t cmd = (uint8_t)MYSQL_COM_QUERY; // Treat empty packets as COM_QUERY

            uint8_t packet_header[MYSQL_HEADER_LEN];

            if (gwbuf_copy_data(queue, offset, MYSQL_HEADER_LEN, packet_header) != MYSQL_HEADER_LEN)
            {
                ss_dassert(offset > 0);
                queue = split_and_store(dcb, queue, offset);
                break;
            }

            pktlen = gw_mysql_get_byte3(packet_header);

            /**
             * Check if the packet is empty, and if not, if we have the command byte.
             * If we an empty packet or have at least 5 bytes of data, we can start
             * sending the data to the router.
             */
            if (pktlen && gwbuf_copy_data(queue, offset + MYSQL_HEADER_LEN, 1, &cmd) != 1)
            {
                if ((queue = split_and_store(dcb, queue, offset)) == NULL)
                {
                    ss_dassert(bytes_available - offset == MYSQL_HEADER_LEN);
                    return false;
                }
                ss_dassert(offset > 0);
                break;
            }

            MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
            if (dcb->protocol_packet_length - MYSQL_HEADER_LEN != GW_MYSQL_MAX_PACKET_LEN)
            {
                /** We're processing the first packet of a command */
                proto->current_command = cmd;
            }

            dcb->protocol_packet_length = pktlen + MYSQL_HEADER_LEN;
            dcb->protocol_bytes_processed = 0;
        }

        int bytes_needed = dcb->protocol_packet_length - dcb->protocol_bytes_processed;
        int packet_bytes = bytes_needed <= bytes_available ? bytes_needed : bytes_available;

        bytes_available -= packet_bytes;
        dcb->protocol_bytes_processed += packet_bytes;
        offset += packet_bytes;
        ss_dassert(dcb->protocol_bytes_processed <= dcb->protocol_packet_length);
    }

    ss_dassert(bytes_available >= 0);
    ss_dassert(queue);
    *buffer = queue;
    return true;
}

/**
 * @brief Client read event, process data, client already authenticated
 *
 * First do some checks and get the router capabilities.  If the router
 * wants to process each individual statement, then the data must be split
 * into individual SQL statements. Any data that is left over is held in the
 * DCB read queue.
 *
 * Finally, the general client data processing function is called.
 *
 * @param dcb           Descriptor control block
 * @param read_buffer   A buffer containing the data read from client
 * @param nbytes_read   The number of bytes of data read
 * @return 0 if succeed, 1 otherwise
 */
static int
gw_read_normal_data(DCB *dcb, GWBUF *read_buffer, int nbytes_read)
{
    MXS_SESSION *session;
    mxs_session_state_t session_state_value;
    uint64_t capabilities = 0;

    session = dcb->session;
    CHK_SESSION(session);
    session_state_value = session->state;
    if (session_state_value != SESSION_STATE_ROUTER_READY)
    {
        if (session_state_value != SESSION_STATE_STOPPING)
        {
            MXS_ERROR("Session received a query in incorrect state %s",
                      STRSESSIONSTATE(session_state_value));
        }
        gwbuf_free(read_buffer);
        dcb_close(dcb);
        return 1;
    }

    /** Ask what type of input the router/filter chain expects */
    capabilities = service_get_capabilities(session->service);

    /** If the router requires statement input we need to make sure that
     * a complete SQL packet is read before continuing. The current command
     * that is tracked by the protocol module is updated in route_by_statement() */
    if (rcap_type_required(capabilities, RCAP_TYPE_STMT_INPUT))
    {
        uint8_t pktlen[MYSQL_HEADER_LEN];
        size_t n_copied = gwbuf_copy_data(read_buffer, 0, MYSQL_HEADER_LEN, pktlen);

        if (n_copied != sizeof(pktlen) ||
            nbytes_read < MYSQL_GET_PAYLOAD_LEN(pktlen) + MYSQL_HEADER_LEN)
        {
            dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
            return 0;
        }
        gwbuf_set_type(read_buffer, GWBUF_TYPE_MYSQL);
    }
    /** Update the current protocol command being executed */
    else if (!process_client_commands(dcb, nbytes_read, &read_buffer))
    {
        return 0;
    }

    /**
     * Handle COM_SET_OPTION. This seems to be only used by some versions of PHP.
     *
     * The option is stored as a two byte integer with the values 0 for enabling
     * multi-statements and 1 for disabling it.
     */
    MySQLProtocol *proto = dcb->protocol;
    uint8_t opt;

    if (proto->current_command == MYSQL_COM_SET_OPTION &&
        gwbuf_copy_data(read_buffer, MYSQL_HEADER_LEN + 2, 1, &opt))
    {
        if (opt)
        {
            proto->client_capabilities &= ~GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
        }
        else
        {
            proto->client_capabilities |= GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS;
        }
    }

    return gw_read_finish_processing(dcb, read_buffer, capabilities);
}

/**
 * Check if a connection qualifies to be added into the persistent connection pool
 *
 * @param dcb The client DCB to check
 */
void check_pool_candidate(DCB* dcb)
{
    MXS_SESSION *session = dcb->session;
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;

    if (proto->current_command == MYSQL_COM_QUIT)
    {
        /** The client is closing the connection. We know that this will be the
         * last command the client sends so the backend connections are very likely
         * to be in an idle state.
         *
         * If the client is pipelining the queries (i.e. sending N request as
         * a batch and then expecting N responses) then it is possible that
         * the backend connections are not idle when the COM_QUIT is received.
         * In most cases we can assume that the connections are idle. */
        session_qualify_for_pool(session);
    }
}

/**
 * @brief Client read event, common processing after single statement handling
 *
 * @param dcb           Descriptor control block
 * @param read_buffer   A buffer containing the data read from client
 * @param capabilities  The router capabilities flags
 * @return 0 if succeed, 1 otherwise
 */
static int
gw_read_finish_processing(DCB *dcb, GWBUF *read_buffer, uint64_t capabilities)
{
    MXS_SESSION *session = dcb->session;
    uint8_t *payload = GWBUF_DATA(read_buffer);
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    CHK_PROTOCOL(proto);
    int return_code = 0;

    /** Reset error handler when routing of the new query begins */
    dcb->dcb_errhandle_called = false;

    if (rcap_type_required(capabilities, RCAP_TYPE_STMT_INPUT))
    {
        /**
         * Feed each statement completely and separately to router.
         */
        return_code = route_by_statement(session, capabilities, &read_buffer) ? 0 : 1;

        if (read_buffer != NULL)
        {
            /* Must have been data left over */
            /* Add incomplete mysql packet to read queue */

            dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);

        }
    }
    else if (NULL != session->router_session || (rcap_type_required(capabilities, RCAP_TYPE_NO_RSESSION)))
    {
        /** Check if this connection qualifies for the connection pool */
        check_pool_candidate(dcb);

        /** Feed the whole buffer to the router */
        return_code = MXS_SESSION_ROUTE_QUERY(session, read_buffer) ? 0 : 1;
    }
    /* else return_code is still 0 from when it was originally set */
    /* Note that read_buffer has been freed or transferred by this point */

    /** Routing failed */
    if (return_code != 0)
    {
        bool router_can_continue;
        GWBUF* errbuf;
        /**
         * Create error to be sent to client if session
         * can't be continued.
         */
        errbuf = mysql_create_custom_error(1, 0,
                                           "Routing failed. Session is closed.");
        /**
         * Ensure that there are enough backends
         * available for router to continue operation.
         */
        session->service->router->handleError(session->service->router_instance,
                                              session->router_session,
                                              errbuf,
                                              dcb,
                                              ERRACT_NEW_CONNECTION,
                                              &router_can_continue);
        gwbuf_free(errbuf);
        /**
         * If the router cannot continue, close session
         */
        if (!router_can_continue)
        {
            MXS_ERROR("Routing the query failed. "
                      "Session will be closed.");
        }
    }

    if (proto->current_command == MYSQL_COM_QUIT)
    {
        /** Close router session which causes closing of backends */
        dcb_close(dcb);
    }

    return return_code;
}

/**
 * @brief Analyse authentication errors and write appropriate log messages
 *
 * @param dcb Request handler DCB connected to the client
 * @param auth_val The type of authentication failure
 * @note Authentication status codes are defined in maxscale/protocol/mysql.h
 */
static void
mysql_client_auth_error_handling(DCB *dcb, int auth_val, int packet_number)
{
    int message_len;
    char *fail_str = NULL;
    MYSQL_session *session = (MYSQL_session*)dcb->data;

    switch (auth_val)
    {
    case MXS_AUTH_NO_SESSION:
        MXS_DEBUG("%lu [gw_read_client_event] session creation failed. fd %d, "
                  "state = MYSQL_AUTH_NO_SESSION.", pthread_self(), dcb->fd);

        /** Send ERR 1045 to client */
        mysql_send_auth_error(dcb, packet_number, 0, "failed to create new session");
        break;

    case MXS_AUTH_FAILED_DB:
        MXS_DEBUG("%lu [gw_read_client_event] database specified was not valid. fd %d, "
                  "state = MYSQL_FAILED_AUTH_DB.", pthread_self(), dcb->fd);
        /** Send error 1049 to client */
        message_len = 25 + MYSQL_DATABASE_MAXLEN;

        fail_str = MXS_CALLOC(1, message_len + 1);
        MXS_ABORT_IF_NULL(fail_str);
        snprintf(fail_str, message_len, "Unknown database '%s'", session->db);

        modutil_send_mysql_err_packet(dcb, packet_number, 0, 1049, "42000", fail_str);
        break;

    case MXS_AUTH_FAILED_SSL:
        MXS_DEBUG("%lu [gw_read_client_event] client is "
                  "not SSL capable for SSL listener. fd %d, "
                  "state = MYSQL_FAILED_AUTH_SSL.", pthread_self(), dcb->fd);

        /** Send ERR 1045 to client */
        mysql_send_auth_error(dcb, packet_number, 0, "Access without SSL denied");
        break;

    case MXS_AUTH_SSL_INCOMPLETE:
        MXS_DEBUG("%lu [gw_read_client_event] unable to "
                  "complete SSL authentication. fd %d, "
                  "state = MYSQL_AUTH_SSL_INCOMPLETE.", pthread_self(), dcb->fd);

        /** Send ERR 1045 to client */
        mysql_send_auth_error(dcb, packet_number, 0,
                              "failed to complete SSL authentication");
        break;

    case MXS_AUTH_FAILED:
        MXS_DEBUG("%lu [gw_read_client_event] authentication failed. fd %d, "
                  "state = MYSQL_FAILED_AUTH.", pthread_self(), dcb->fd);
        /** Send error 1045 to client */
        fail_str = create_auth_fail_str(session->user, dcb->remote,
                                        session->auth_token_len > 0,
                                        session->db, auth_val);
        modutil_send_mysql_err_packet(dcb, packet_number, 0, 1045, "28000", fail_str);
        break;

    default:
        MXS_DEBUG("%lu [gw_read_client_event] authentication failed. fd %d, "
                  "state unrecognized.", pthread_self(), dcb->fd);
        /** Send error 1045 to client */
        fail_str = create_auth_fail_str(session->user, dcb->remote,
                                        session->auth_token_len > 0,
                                        session->db, auth_val);
        modutil_send_mysql_err_packet(dcb, packet_number, 0, 1045, "28000", fail_str);
    }
    MXS_FREE(fail_str);
}

static int
gw_connection_limit(DCB *dcb, int limit)
{
    return mysql_send_standard_error(dcb, 0, 1040, "Too many connections");
}
///////////////////////////////////////////////
// client write event to Client triggered by EPOLLOUT
//////////////////////////////////////////////
/**
 * @node Client's fd became writable, and EPOLLOUT event
 * arrived. As a consequence, client input buffer (writeq) is flushed.
 *
 * Parameters:
 * @param dcb - in, use
 *          client dcb
 *
 * @return constantly 1
 *
 *
 * @details (write detailed description here)
 *
 */
int gw_write_client_event(DCB *dcb)
{
    MySQLProtocol *protocol = NULL;

    CHK_DCB(dcb);

    ss_dassert(dcb->state != DCB_STATE_DISCONNECTED);

    if (dcb == NULL)
    {
        goto return_1;
    }

    if (dcb->state == DCB_STATE_DISCONNECTED)
    {
        goto return_1;
    }

    if (dcb->protocol == NULL)
    {
        goto return_1;
    }
    protocol = (MySQLProtocol *)dcb->protocol;
    CHK_PROTOCOL(protocol);

    if (protocol->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
    {
        dcb_drain_writeq(dcb);
        goto return_1;
    }

return_1:
#if defined(SS_DEBUG)
    if (dcb->state == DCB_STATE_POLLING ||
        dcb->state == DCB_STATE_NOPOLLING ||
        dcb->state == DCB_STATE_ZOMBIE)
    {
        CHK_PROTOCOL(protocol);
    }
#endif
    return 1;
}

/**
 * Bind the DCB to a network port or a UNIX Domain Socket.
 * @param listen_dcb Listener DCB
 * @param config_bind Bind address in either IP:PORT format for network sockets or PATH
 *                    for UNIX Domain Sockets
 * @return 1 on success, 0 on error
 */
int gw_MySQLListener(DCB *listen_dcb, char *config_bind)
{
    if (dcb_listen(listen_dcb, config_bind, "MySQL") < 0)
    {
        return 0;
    }
    listen_dcb->func.accept = gw_MySQLAccept;

    return 1;
}


/**
 * @node Accept a new connection, using the DCB code for the basic work
 *
 * For as long as dcb_accept can return new client DCBs for new connections,
 * continue to loop. The code will always give a failure return, since it
 * continues to try to create new connections until a failure occurs.
 *
 * @param listener - The Listener DCB that picks up new connection requests
 * @return 0 in success, 1 in failure
 *
 */
int gw_MySQLAccept(DCB *listener)
{
    DCB *client_dcb;
    MySQLProtocol *protocol;

    CHK_DCB(listener);

    if (DCB_STATE_WAITING == listener->state)
    {
        gw_process_one_new_client(listener);
    }
    else
    {
        while ((client_dcb = dcb_accept(listener)) != NULL)
        {
            gw_process_one_new_client(client_dcb);
        } /**< while client_dcb != NULL */
    }

    /* Must have broken out of while loop or received NULL client_dcb */
    return 1;
}

static void gw_process_one_new_client(DCB *client_dcb)
{
    MySQLProtocol *protocol;

    CHK_DCB(client_dcb);
    protocol = mysql_protocol_init(client_dcb, client_dcb->fd);

    if (protocol == NULL)
    {
        /** delete client_dcb */
        dcb_close(client_dcb);
        MXS_ERROR("%lu [gw_MySQLAccept] Failed to create "
                  "protocol object for client connection.",
                  pthread_self());
        return;
    }
    CHK_PROTOCOL(protocol);
    client_dcb->protocol = protocol;
    if (DCB_STATE_WAITING == client_dcb->state)
    {
        client_dcb->state = DCB_STATE_ALLOC;
    }
    else
    {
        atomic_add(&client_dcb->service->client_count, 1);
    }
    //send handshake to the client_dcb
    MySQLSendHandshake(client_dcb);

    // client protocol state change
    protocol->protocol_auth_state = MXS_AUTH_STATE_MESSAGE_READ;

    /**
     * Set new descriptor to event set. At the same time,
     * change state to DCB_STATE_POLLING so that
     * thread which wakes up sees correct state.
     */
    if (poll_add_dcb(client_dcb) == -1)
    {
        /* Send a custom error as MySQL command reply */
        mysql_send_custom_error(client_dcb,
                                1,
                                0,
                                "MaxScale encountered system limit while "
                                "attempting to register on an epoll instance.");

        /** close client_dcb */
        dcb_close(client_dcb);

        /** Previous state is recovered in poll_add_dcb. */
        MXS_ERROR("%lu [gw_MySQLAccept] Failed to add dcb %p for "
                  "fd %d to epoll set.",
                  pthread_self(),
                  client_dcb,
                  client_dcb->fd);
        return;
    }
    else
    {
        MXS_DEBUG("%lu [gw_MySQLAccept] Added dcb %p for fd "
                  "%d to epoll set.",
                  pthread_self(),
                  client_dcb,
                  client_dcb->fd);
    }
    return;
}

static int gw_error_client_event(DCB* dcb)
{
    MXS_SESSION* session;

    CHK_DCB(dcb);

    session = dcb->session;

    MXS_DEBUG("%lu [gw_error_client_event] Error event handling for DCB %p "
              "in state %s, session %p.",
              pthread_self(),
              dcb,
              STRDCBSTATE(dcb->state),
              (session != NULL ? session : NULL));

    if (session != NULL && session->state == SESSION_STATE_STOPPING)
    {
        goto retblock;
    }

#if defined(SS_DEBUG)
    MXS_DEBUG("Client error event handling.");
#endif
    dcb_close(dcb);

retblock:
    return 1;
}

static int
gw_client_close(DCB *dcb)
{
    MXS_SESSION* session;
    MXS_ROUTER_OBJECT* router;
    void* router_instance;
#if defined(SS_DEBUG)
    MySQLProtocol* protocol = (MySQLProtocol *)dcb->protocol;

    if (dcb->state == DCB_STATE_POLLING ||
        dcb->state == DCB_STATE_NOPOLLING ||
        dcb->state == DCB_STATE_ZOMBIE)
    {
        if (!DCB_IS_CLONE(dcb))
        {
            CHK_PROTOCOL(protocol);
        }
    }
#endif
    MXS_DEBUG("%lu [gw_client_close]", pthread_self());
    mysql_protocol_done(dcb);
    session = dcb->session;
    /**
     * session may be NULL if session_alloc failed.
     * In that case, router session wasn't created.
     */
    if (session != NULL && SESSION_STATE_DUMMY != session->state)
    {
        CHK_SESSION(session);

        if (session->state != SESSION_STATE_STOPPING)
        {
            session->state = SESSION_STATE_STOPPING;
        }
        router_instance = session->service->router_instance;
        router = session->service->router;
        /**
         * If router session is being created concurrently router
         * session might be NULL and it shouldn't be closed.
         */
        if (session->router_session != NULL)
        {
            /** Close router session and all its connections */
            router->closeSession(router_instance, session->router_session);
        }
    }
    return 1;
}

/**
 * Handle a hangup event on the client side descriptor.
 *
 * We simply close the DCB, this will propagate the closure to any
 * backend descriptors and perform the session cleanup.
 *
 * @param dcb           The DCB of the connection
 */
static int gw_client_hangup_event(DCB *dcb)
{
    MXS_SESSION* session;

    CHK_DCB(dcb);
    session = dcb->session;

    if (session != NULL && session->state == SESSION_STATE_ROUTER_READY)
    {
        CHK_SESSION(session);
    }

    if (session != NULL && session->state == SESSION_STATE_STOPPING)
    {
        goto retblock;
    }

    if (!session_valid_for_pool(session))
    {
        // The client did not send a COM_QUIT packet
        modutil_send_mysql_err_packet(dcb, 0, 0, 1927, "08S01", "Connection killed by MaxScale");
    }
    dcb_close(dcb);

retblock:
    return 1;
}

/**
 * Update protocol tracking information for an individual statement
 *
 * @param dcb    Client DCB
 * @param buffer Buffer containing a single packet
 */
void update_current_command(DCB* dcb, GWBUF* buffer)
{
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    uint8_t cmd = (uint8_t)MYSQL_COM_QUERY;

    /**
     * As we are routing individual packets, we can extract the command byte here.
     * Empty packets are treated as COM_QUERY packets by default.
     */
    gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd);
    proto->current_command = cmd;

    /**
     * Now that we have the current command, we can check if this connection
     * can be a candidate for the connection pool.
     */
    check_pool_candidate(dcb);
}

/**
 * Detect if buffer includes partial mysql packet or multiple packets.
 * Store partial packet to dcb_readqueue. Send complete packets one by one
 * to router.
 *
 * It is assumed readbuf includes at least one complete packet.
 * Return 1 in success. If the last packet is incomplete return success but
 * leave incomplete packet to readbuf.
 *
 * @param session       Session pointer
 * @param capabilities  The capabilities of the service.
 * @param p_readbuf     Pointer to the address of GWBUF including the query
 *
 * @return 1 if succeed,
 */
static int route_by_statement(MXS_SESSION* session, uint64_t capabilities, GWBUF** p_readbuf)
{
    int rc;
    GWBUF* packetbuf;
#if defined(SS_DEBUG)
    GWBUF* tmpbuf;

    tmpbuf = *p_readbuf;
    while (tmpbuf != NULL)
    {
        ss_dassert(GWBUF_IS_TYPE_MYSQL(tmpbuf));
        tmpbuf = tmpbuf->next;
    }
#endif
    do
    {
        ss_dassert(GWBUF_IS_TYPE_MYSQL((*p_readbuf)));

        /**
         * Collect incoming bytes to a buffer until complete packet has
         * arrived and then return the buffer.
         */
        // TODO: This should be replaced with modutil_get_next_MySQL_packet.
        packetbuf = gw_MySQL_get_next_packet(p_readbuf);

        if (packetbuf != NULL)
        {
            CHK_GWBUF(packetbuf);
            ss_dassert(GWBUF_IS_TYPE_MYSQL(packetbuf));
            /**
             * This means that buffer includes exactly one MySQL
             * statement.
             * backend func.write uses the information. MySQL backend
             * protocol, for example, stores the command identifier
             * to protocol structure. When some other thread reads
             * the corresponding response the command tells how to
             * handle response.
             *
             * Set it here instead of gw_read_client_event to make
             * sure it is set to each (MySQL) packet.
             */
            gwbuf_set_type(packetbuf, GWBUF_TYPE_SINGLE_STMT);

            /**
             * Update the currently command being executed.
             */
            update_current_command(session->client_dcb, packetbuf);

            if (rcap_type_required(capabilities, RCAP_TYPE_CONTIGUOUS_INPUT))
            {
                if (!GWBUF_IS_CONTIGUOUS(packetbuf))
                {
                    // TODO: As long as gw_MySQL_get_next_packet is used above, the buffer
                    // TODO: will be contiguous. That function should be replaced with
                    // TODO: modutil_get_next_MySQL_packet.
                    GWBUF* tmp = gwbuf_make_contiguous(packetbuf);
                    if (tmp)
                    {
                        packetbuf = tmp;
                    }
                    else
                    {
                        // TODO: A memory allocation failure. We should close the dcb
                        // TODO: and terminate the session.
                        rc = 0;
                        goto return_rc;
                    }
                }

                if (rcap_type_required(capabilities, RCAP_TYPE_TRANSACTION_TRACKING))
                {
                    uint8_t *data = GWBUF_DATA(packetbuf);

                    if (session_trx_is_ending(session))
                    {
                        session_set_trx_state(session, SESSION_TRX_INACTIVE);
                    }

                    if (MYSQL_GET_COMMAND(data) == MYSQL_COM_QUERY)
                    {
                        uint32_t type = qc_get_trx_type_mask(packetbuf);

                        if (type & QUERY_TYPE_BEGIN_TRX)
                        {
                            if (type & QUERY_TYPE_DISABLE_AUTOCOMMIT)
                            {
                                session_set_autocommit(session, false);
                                session_set_trx_state(session, SESSION_TRX_INACTIVE);
                            }
                            else
                            {
                                mxs_session_trx_state_t trx_state;
                                if (type & QUERY_TYPE_WRITE)
                                {
                                    trx_state = SESSION_TRX_READ_WRITE;
                                }
                                else if (type & QUERY_TYPE_READ)
                                {
                                    trx_state = SESSION_TRX_READ_ONLY;
                                }
                                else
                                {
                                    trx_state = SESSION_TRX_ACTIVE;
                                }

                                session_set_trx_state(session, trx_state);
                            }
                        }
                        else if ((type & QUERY_TYPE_COMMIT) || (type & QUERY_TYPE_ROLLBACK))
                        {
                            mxs_session_trx_state_t trx_state = session_get_trx_state(session);
                            trx_state |= SESSION_TRX_ENDING_BIT;
                            session_set_trx_state(session, trx_state);

                            if (type & QUERY_TYPE_ENABLE_AUTOCOMMIT)
                            {
                                session_set_autocommit(session, true);
                            }
                        }
                    }
                }
            }

            /** Route query */
            rc = MXS_SESSION_ROUTE_QUERY(session, packetbuf);
        }
        else
        {
            rc = 1;
            goto return_rc;
        }
    }
    while (rc == 1 && *p_readbuf != NULL);

return_rc:
    return rc;
}
