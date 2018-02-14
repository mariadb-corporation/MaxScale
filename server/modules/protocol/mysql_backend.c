/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mysql_client_server_protocol.h"
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <modutil.h>
#include <utils.h>
#include <netinet/tcp.h>
#include <gw.h>
#include <mysqld_error.h>

/* The following can be compared using memcmp to detect a null password */
uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN]="";

/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 *
 * Revision History
 * Date         Who                     Description
 * 14/06/2013   Mark Riddoch            Initial version
 * 17/06/2013   Massimiliano Pinto      Added MaxScale To Backends routines
 * 01/07/2013   Massimiliano Pinto      Put Log Manager example code behind SS_DEBUG macros.
 * 03/07/2013   Massimiliano Pinto      Added delayq for incoming data before mysql connection
 * 04/07/2013   Massimiliano Pinto      Added asynchronous MySQL protocol connection to backend
 * 05/07/2013   Massimiliano Pinto      Added closeSession if backend auth fails
 * 12/07/2013   Massimiliano Pinto      Added Mysql Change User via dcb->func.auth()
 * 15/07/2013   Massimiliano Pinto      Added Mysql session change via dcb->func.session()
 * 17/07/2013   Massimiliano Pinto      Added dcb->command update from gwbuf->command for proper routing
 *                                      server replies to client via router->clientReply
 * 04/09/2013   Massimiliano Pinto      Added dcb->session and dcb->session->client checks for NULL
 * 12/09/2013   Massimiliano Pinto      Added checks in gw_read_backend_event() for gw_read_backend_handshake
 * 27/09/2013   Massimiliano Pinto      Changed in gw_read_backend_event the check for dcb_read(),
 *                                      now is if rc less than 0
 * 24/10/2014   Massimiliano Pinto      Added Mysql user@host @db authentication support
 * 10/11/2014   Massimiliano Pinto      Client charset is passed to backend
 * 19/06/2015   Martin Brampton         Persistent connection handling
 * 07/10/2015   Martin Brampton         Remove calls to dcb_close - should be done by routers
 * 27/10/2015   Martin Brampton         Test for RCAP_TYPE_NO_RSESSION before calling clientReply
 * 23/05/2016   Martin Brampton         Provide for backend SSL
 *
 */
#include <modinfo.h>
#include <gw_protocol.h>
#include <mysql_auth.h>

 /* @see function load_module in load_utils.c for explanation of the following
  * lint directives.
 */
/*lint -e14 */
MODULE_INFO info = {
                    MODULE_API_PROTOCOL,
                    MODULE_GA,
                    GWPROTOCOL_VERSION,
                    "The MySQL to backend server protocol"
};
/*lint +e14 */

static char *version_str = "V2.0.0";
static int gw_create_backend_connection(DCB *backend, SERVER *server, SESSION *in_session);
static int gw_read_backend_event(DCB* dcb);
static int gw_write_backend_event(DCB *dcb);
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
static int gw_error_backend_event(DCB *dcb);
static int gw_backend_close(DCB *dcb);
static int gw_backend_hangup(DCB *dcb);
static int backend_write_delayqueue(DCB *dcb);
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue);
static int gw_change_user(DCB *backend_dcb, SERVER *server, SESSION *in_session, GWBUF *queue);
static char *gw_backend_default_auth();
static GWBUF* process_response_data(DCB* dcb, GWBUF** readbuf, int nbytes_to_process);
extern char* create_auth_failed_msg(GWBUF* readbuf, char* hostaddr, uint8_t* sha1);
static bool sescmd_response_complete(DCB* dcb);
static int gw_read_reply_or_error(DCB *dcb, MYSQL_session local_session);
static int gw_read_and_write(DCB *dcb, MYSQL_session local_session);
static int gw_read_backend_handshake(MySQLProtocol *conn);
static int gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload);
static int gw_receive_backend_auth(MySQLProtocol *protocol, uint16_t *code);
static mysql_auth_state_t gw_send_authentication_to_backend(char *dbname,
                                      char *user,
                                      uint8_t *passwd,
                                      MySQLProtocol *conn);
static uint32_t create_capabilities(MySQLProtocol *conn, bool db_specified, bool compress);
static int response_length(MySQLProtocol *conn, char *user, uint8_t *passwd, char *dbname);
static uint8_t *load_hashed_password(MySQLProtocol *conn, uint8_t *payload, uint8_t *passwd);
static int gw_do_connect_to_backend(char *host, int port, int *fd);
static void inline close_socket(int socket);
static GWBUF *gw_create_change_user_packet(MYSQL_session*  mses,
                                    MySQLProtocol*  protocol);
static int gw_send_change_user_to_backend(char          *dbname,
                                   char          *user,
                                   uint8_t       *passwd,
                                   MySQLProtocol *conn);

#if defined(NOT_USED)
static int gw_session(DCB *backend_dcb, void *data);
#endif
static bool gw_get_shared_session_auth_info(DCB* dcb, MYSQL_session* session);

static GWPROTOCOL MyObject = {
                              gw_read_backend_event, /* Read - EPOLLIN handler        */
                              gw_MySQLWrite_backend, /* Write - data from gateway     */
                              gw_write_backend_event, /* WriteReady - EPOLLOUT handler */
                              gw_error_backend_event, /* Error - EPOLLERR handler      */
                              gw_backend_hangup, /* HangUp - EPOLLHUP handler     */
                              NULL, /* Accept                        */
                              gw_create_backend_connection, /* Connect                       */
                              gw_backend_close, /* Close                         */
                              NULL, /* Listen                        */
                              gw_change_user, /* Authentication                */
                              NULL, /* Session                       */
                              gw_backend_default_auth, /* Default authenticator */
                              NULL  /**< Connection limit reached      */
};

/*
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 *
 * @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
char* version()
{
    return version_str;
}

/*
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void ModuleInit()
{
}

/*
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWPROTOCOL* GetModuleObject()
{
    return &MyObject;
}

/**
 * The default authenticator name for this protocol
 *
 * This is not used for a backend protocol, it is for client authentication.
 *
 * @return name of authenticator
 */
static char *gw_backend_default_auth()
{
    return "NullBackendAuth";
}
/*lint +e14 */

/*******************************************************************************
 *******************************************************************************
 *
 * API Entry Point - Connect
 *
 * This is the first entry point that will be called in the life of a backend
 * (database) connection. It creates a protocol data structure and attempts
 * to open a non-blocking socket to the database. If it succeeds, the
 * protocol_auth_state will become MYSQL_CONNECTED.
 *
 *******************************************************************************
 ******************************************************************************/

/*
 * Create a new backend connection.
 *
 * This routine will connect to a backend server and it is called by dbc_connect
 * in router->newSession
 *
 * @param backend_dcb, in, out, use - backend DCB allocated from dcb_connect
 * @param server, in, use - server to connect to
 * @param session, in use - current session from client DCB
 * @return 0/1 on Success and -1 on Failure.
 * If succesful, returns positive fd to socket which is connected to
 *  backend server. Positive fd is copied to protocol and to dcb.
 * If fails, fd == -1 and socket is closed.
 */
static int gw_create_backend_connection(DCB *backend_dcb,
                                        SERVER *server,
                                        SESSION *session)
{
    MySQLProtocol *protocol = NULL;
    int rv = -1;
    int fd = -1;

    protocol = mysql_protocol_init(backend_dcb, -1);
    ss_dassert(protocol != NULL);

    if (protocol == NULL)
    {
        MXS_DEBUG("%lu [gw_create_backend_connection] Failed to create "
                  "protocol object for backend connection.",
                  pthread_self());
        MXS_ERROR("Failed to create protocol object for backend connection.");
        goto return_fd;
    }

    /** Copy client flags to backend protocol */
    if (backend_dcb->session->client_dcb->protocol)
    {
        /** Copy client flags to backend protocol */
        protocol->client_capabilities =
            ((MySQLProtocol *)(backend_dcb->session->client_dcb->protocol))->client_capabilities;
        /** Copy client charset to backend protocol */
        protocol->charset =
            ((MySQLProtocol *)(backend_dcb->session->client_dcb->protocol))->charset;
    }
    else
    {
        protocol->client_capabilities = (int)GW_MYSQL_CAPABILITIES_CLIENT;
        protocol->charset = 0x08;
    }

    /*< if succeed, fd > 0, -1 otherwise */
    /* TODO: Better if function returned a protocol auth state */
    rv = gw_do_connect_to_backend(server->name, server->port, &fd);
    /*< Assign protocol with backend_dcb */
    backend_dcb->protocol = protocol;

    /*< Set protocol state */
    switch (rv)
    {
        case 0:
            ss_dassert(fd > 0);
            protocol->fd = fd;
            protocol->protocol_auth_state = MYSQL_CONNECTED;
            MXS_DEBUG("%lu [gw_create_backend_connection] Established "
                      "connection to %s:%i, protocol fd %d client "
                      "fd %d.",
                      pthread_self(),
                      server->name,
                      server->port,
                      protocol->fd,
                      session->client_dcb->fd);
            break;

        case 1:
            /* The state MYSQL_PENDING_CONNECT is likely to be transitory,    */
            /* as it means the calls have been successful but the connection  */
            /* has not yet completed and the calls are non-blocking.          */
            ss_dassert(fd > 0);
            protocol->protocol_auth_state = MYSQL_PENDING_CONNECT;
            protocol->fd = fd;
            MXS_DEBUG("%lu [gw_create_backend_connection] Connection "
                      "pending to %s:%i, protocol fd %d client fd %d.",
                      pthread_self(),
                      server->name,
                      server->port,
                      protocol->fd,
                      session->client_dcb->fd);
            break;

        default:
            /* Failure - the state reverts to its initial value */
            ss_dassert(fd == -1);
            ss_dassert(protocol->protocol_auth_state == MYSQL_ALLOC);
            MXS_DEBUG("%lu [gw_create_backend_connection] Connection "
                      "failed to %s:%i, protocol fd %d client fd %d.",
                      pthread_self(),
                      server->name,
                      server->port,
                      protocol->fd,
                      session->client_dcb->fd);
            break;
    } /*< switch */

return_fd:
    return fd;
}

/**
 * gw_do_connect_to_backend
 *
 * This routine creates socket and connects to a backend server.
 * Connect it non-blocking operation. If connect fails, socket is closed.
 *
 * @param host The host to connect to
 * @param port The host TCP/IP port
 * @param *fd where connected fd is copied
 * @return 0/1 on success and -1 on failure
 * If successful, fd has file descriptor to socket which is connected to
 * backend server. In failure, fd == -1 and socket is closed.
 *
 */
static int
gw_do_connect_to_backend(char *host, int port, int *fd)
{
    struct sockaddr_in serv_addr;
    int rv;
    int so = 0;
    int bufsize;

    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.sin_family = (int)AF_INET;
    so = socket((int)AF_INET, (int)SOCK_STREAM, 0);

    if (so < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Establishing connection to backend server "
                  "%s:%d failed.\n\t\t             Socket creation failed "
                  "due %d, %s.",
                  host,
                  port,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        rv = -1;
        goto return_rv;
    }
    /* prepare for connect */
    setipaddress(&serv_addr.sin_addr, host);
    serv_addr.sin_port = htons(port);
    bufsize = GW_BACKEND_SO_SNDBUF;

    if (setsockopt(so, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to set socket options "
                  "%s:%d failed.\n\t\t             Socket configuration failed "
                  "due %d, %s.",
                  host,
                  port,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        rv = -1;
        /** Close socket */
        close_socket(so);
        goto return_rv;
    }
    bufsize = GW_BACKEND_SO_RCVBUF;

    if (setsockopt(so, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to set socket options "
                  "%s:%d failed.\n\t\t             Socket configuration failed "
                  "due %d, %s.",
                  host,
                  port,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        rv = -1;
        /** Close socket */
        close_socket(so);
        goto return_rv;
    }

    int one = 1;
    if (setsockopt(so, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to set socket options "
                  "%s:%d failed.\n\t\t             Socket configuration failed "
                  "due %d, %s.",
                  host,
                  port,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        rv = -1;
        /** Close socket */
        close_socket(so);
        goto return_rv;
    }

    /* set socket to as non-blocking here */
    setnonblocking(so);
    rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    if (rv != 0)
    {
        if (errno == EINPROGRESS)
        {
            rv = 1;
        }
        else
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Failed to connect backend server %s:%d, "
                      "due %d, %s.",
                      host,
                      port,
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
            /** Close socket */
            close_socket(so);
            goto return_rv;
        }
    }
    *fd = so;
    MXS_DEBUG("%lu [gw_do_connect_to_backend] Connected to backend server "
              "%s:%d, fd %d.",
              pthread_self(), host, port, so);
#if defined(FAKE_CODE)
    conn_open[so] = true;
#endif /* FAKE_CODE */

return_rv:
    return rv;

}

/*******************************************************************************
 *******************************************************************************
 *
 * API Entry Point - Read
 *
 * When the polling mechanism finds that new incoming data is available for
 * a backend connection, it will call this entry point, passing the relevant
 * DCB.
 *
 * The first time through, it is expected that protocol_auth_state will be
 * MYSQL_CONNECTED and an attempt will be made to send authentication data
 * to the backend server. The state may progress to MYSQL_AUTH_REC although
 * for an SSL connection this will not happen straight away, and the state
 * will remain MYSQL_CONNECTED.
 *
 * When the connection is fully established, it is expected that the state
 * will be MYSQL_IDLE and the information read from the backend will be
 * transferred to the client (front end).
 *
 *******************************************************************************
 ******************************************************************************/

/**
 * Backend Read Event for EPOLLIN on the MySQL backend protocol module
 * @param dcb   The backend Descriptor Control Block
 * @return 1 on operation, 0 for no action
 */
static int
gw_read_backend_event(DCB *dcb)
{
    MySQLProtocol *backend_protocol;
    MYSQL_session local_session;

    CHK_DCB(dcb);
    if (dcb->persistentstart)
    {
        dcb->dcb_errhandle_called = true;
        return 0;
    }

    if (dcb->dcb_is_zombie || dcb->session == NULL)
    {
        return 0;
    }

    CHK_SESSION(dcb->session);

    /*< return only with complete session */
    if (!gw_get_shared_session_auth_info(dcb, &local_session))
    {
        return 0;
    }

    backend_protocol = (MySQLProtocol *) dcb->protocol;
    CHK_PROTOCOL(backend_protocol);

    MXS_DEBUG("%lu [gw_read_backend_event] Read dcb %p fd %d protocol "
        "state %d, %s.",
        pthread_self(),
        dcb,
        dcb->fd,
        backend_protocol->protocol_auth_state,
        STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));

    /* backend is connected:
     *
     * 1. read server handshake
     * 2. if (success) write auth request
     * 3.  and return
     */

    /*<
     * If starting to auhenticate with backend server, lock dcb
     * to prevent overlapping processing of auth messages.
     */
    if (backend_protocol->protocol_auth_state == MYSQL_CONNECTED)
    {
        spinlock_acquire(&dcb->authlock);
        if (backend_protocol->protocol_auth_state == MYSQL_CONNECTED)
        {
            /** Read cached backend handshake */
            if (gw_read_backend_handshake(backend_protocol) != 0)
            {
                backend_protocol->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;

                MXS_DEBUG("%lu [gw_read_backend_event] after "
                          "gw_read_backend_handshake, fd %d, "
                          "state = MYSQL_HANDSHAKE_FAILED.",
                          pthread_self(),
                          backend_protocol->owner_dcb->fd);
            }
            else
            {
                /**
                 * Decode password and send the auth credentials
                 * to backend.
                 */
                backend_protocol->protocol_auth_state =
                    gw_send_authentication_to_backend(
                    local_session.db,
                    local_session.user,
                    local_session.client_sha1,
                    backend_protocol);
            }
        }
        spinlock_release(&dcb->authlock);
    } /*< backend_protocol->protocol_auth_state == MYSQL_CONNECTED */
    /*
     * Now:
     *  -- check the authentication reply from backend
     * OR
     * -- handle a previous handshake error
     */
    if (backend_protocol->protocol_auth_state != MYSQL_IDLE)
    {
        spinlock_acquire(&dcb->authlock);

        if (backend_protocol->protocol_auth_state != MYSQL_IDLE)
        {
            if (backend_protocol->protocol_auth_state == MYSQL_CONNECTED)
            {
                spinlock_release(&dcb->authlock);
                return 0;
            }
            /* Function gw_read_reply_or_error will release dcb->authlock */
            int return_code = gw_read_reply_or_error(dcb, local_session);
            /* Make decision whether to exit */
            if (return_code < 2)
            {
                return return_code;
            }
        }
        else
        {
            spinlock_release(&dcb->authlock);
        }
    } /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED || MYSQL_HANDSHAKE_FAILED */

    /* Reading MySQL command output from backend and writing to the client */
    return gw_read_and_write(dcb, local_session);
}

/**
 * Read the backend server MySQL handshake
 *
 * @param conn  MySQL protocol structure
 * @return 0 on success, 1 on failure
 */
static int
gw_read_backend_handshake(MySQLProtocol *conn)
{
    GWBUF *head = NULL;
    DCB *dcb = conn->owner_dcb;
    uint8_t *payload = NULL;
    int h_len = 0;
    int  success = 0;
    int packet_len = 0;

    if (dcb_read(dcb, &head, 0) != -1)
    {
        dcb->last_read = hkheartbeat;

        if (head)
        {
            payload = GWBUF_DATA(head);
            h_len = gwbuf_length(head);

            /**
             * The mysql packets content starts at byte fifth
             * just return with less bytes
             */

            if (h_len <= 4)
            {
                /* log error this exit point */
                conn->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;
                MXS_DEBUG("%lu [gw_read_backend_handshake] after "
                          "dcb_read, fd %d, "
                          "state = MYSQL_HANDSHAKE_FAILED.",
                          pthread_self(),
                          dcb->fd);

                return 1;
            }

            if (payload[4] == 0xff)
            {
                size_t len = MYSQL_GET_PACKET_LEN(payload);
                uint16_t errcode = MYSQL_GET_ERRCODE(payload);
                char* bufstr = strndup(&((char *)payload)[7], len - 3);

                conn->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;

                MXS_DEBUG("%lu [gw_receive_backend_auth] Invalid "
                          "authentication message from backend dcb %p "
                          "fd %d, ptr[4] = %d, error code %d, msg %s.",
                          pthread_self(),
                          dcb,
                          dcb->fd,
                          payload[4],
                          errcode,
                          bufstr);

                MXS_ERROR("Invalid authentication message "
                          "from backend '%s'. Error code: %d, Msg : %s",
                          dcb->server->unique_name,
                          errcode,
                          bufstr);

                /**
                 * If ER_HOST_IS_BLOCKED is found
                 * the related server is put in maintenace mode
                 * This will avoid filling the error log.
                 */

                if (errcode == 1129)
                {
                    MXS_ERROR("Server %s has been put into maintenance mode due "
                              "to the server blocking connections from MaxScale. "
                              "Run 'mysqladmin -h %s -P %d flush-hosts' on this "
                              "server before taking this server out of maintenance "
                              "mode.",
                              dcb->server->unique_name,
                              dcb->server->name,
                              dcb->server->port);

                    server_set_status(dcb->server, SERVER_MAINT);
                }

                free(bufstr);
            }
            //get mysql packet size, 3 bytes
            packet_len = gw_mysql_get_byte3(payload);

            if (h_len < (packet_len + 4))
            {
                /*
                 * data in buffer less than expected in the
                 * packet. Log error this exit point
                 */

                conn->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;

                MXS_DEBUG("%lu [gw_read_backend_handshake] after "
                          "gw_mysql_get_byte3, fd %d, "
                          "state = MYSQL_HANDSHAKE_FAILED.",
                          pthread_self(),
                          dcb->fd);

                return 1;
            }

            // skip the 4 bytes header
            payload += 4;

            //Now decode mysql handshake
            success = gw_decode_mysql_server_handshake(conn, payload);

            if (success < 0)
            {
                /* MySQL handshake has not been properly decoded
                 * we cannot continue
                 * log error this exit point
                 */
                conn->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;

                MXS_DEBUG("%lu [gw_read_backend_handshake] after "
                          "gw_decode_mysql_server_handshake, fd %d, "
                          "state = MYSQL_HANDSHAKE_FAILED.",
                          pthread_self(),
                          conn->owner_dcb->fd);
                gwbuf_free(head);
                return 1;
            }

            conn->protocol_auth_state = MYSQL_AUTH_SENT;

            // consume all the data here
            gwbuf_free(head);

            return 0;
        }
        else if (SSL_ESTABLISHED == dcb->ssl_state)
        {
            return 0;
        }
    }

    // Nothing done here, log error this
    return 1;
}

/**
 * Write MySQL authentication packet to backend server
 *
 * @param conn  MySQL protocol structure
 * @param dbname The selected database
 * @param user The selected user
 * @param passwd The SHA1(real_password): Note real_password is unknown
 * @return MySQL authorisation state after operation
 */
static mysql_auth_state_t
gw_send_authentication_to_backend(char *dbname,
                                      char *user,
                                      uint8_t *passwd,
                                      MySQLProtocol *conn)
{
    uint8_t *payload;
    long bytes;
    uint32_t capabilities;
    uint8_t client_capabilities[4] = {0,0,0,0};
    GWBUF *buffer;
    uint8_t *curr_passwd = memcmp(passwd, null_client_sha1, MYSQL_SCRAMBLE_LEN) ? passwd : NULL;

    /**
     * If session is stopping return with error.
     */
    if (conn->owner_dcb->session == NULL ||
        (conn->owner_dcb->session->state != SESSION_STATE_READY &&
         conn->owner_dcb->session->state != SESSION_STATE_ROUTER_READY))
    {
        return MYSQL_AUTH_FAILED;
    }

    capabilities = create_capabilities(conn, (dbname && strlen(dbname)), false);
    gw_mysql_set_byte4(client_capabilities, capabilities);

    bytes = response_length(conn, user, passwd, dbname);

    // allocating the GWBUF
    buffer = gwbuf_alloc(bytes);
    payload = GWBUF_DATA(buffer);

    // clearing data
    memset(payload, '\0', bytes);

    // put here the paylod size: bytes to write - 4 bytes packet header
    gw_mysql_set_byte3(payload, (bytes - 4));

    // set packet # = 1
    payload[3] = (SSL_ESTABLISHED == conn->owner_dcb->ssl_state) ? '\x02' : '\x01';
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

    // 23 bytes of 0
    payload += 23;

    // 4 + 4 + 4 + 1 + 23 = 36, this includes the 4 bytes packet header
    if (conn->owner_dcb->server->server_ssl && conn->owner_dcb->ssl_state != SSL_ESTABLISHED)
    {
        if (dcb_write(conn->owner_dcb, buffer))
        {
            switch (dcb_connect_SSL(conn->owner_dcb))
            {
                case 1:
                    return MYSQL_CONNECTED;
                case 0:
                    return MYSQL_CONNECTED;
                default:
                    break;
            }
        }
        return MYSQL_AUTH_FAILED;
    }

    memcpy(payload, user, strlen(user));
    payload += strlen(user);
    payload++;

    if (curr_passwd != NULL)
    {
        payload = load_hashed_password(conn, payload, curr_passwd);
    }
    else
    {
        payload++;
    }

    // if the db is not NULL append it
    if (dbname && strlen(dbname))
    {
        memcpy(payload, dbname, strlen(dbname));
        payload += strlen(dbname);
        payload++;
    }

    memcpy(payload,
           "mysql_native_password",
           strlen("mysql_native_password"));
    /* Following needed if payload is used again */
    /* payload += strlen("mysql_native_password"); */

    return dcb_write(conn->owner_dcb, buffer) ? MYSQL_AUTH_RECV : MYSQL_AUTH_FAILED;
}

/**
 * Copy shared session authentication info
 *
 * @param dcb A backend DCB
 * @param session Destination where authentication data is copied
 * @return bool true = success, false = fail
 */
static bool gw_get_shared_session_auth_info(DCB* dcb, MYSQL_session* session)
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
        MXS_ERROR("%lu [gw_get_shared_session_auth_info] Couldn't get "
                  "session authentication info. Session in a wrong state %d.",
                  pthread_self(), dcb->session->state);
        rval = false;
    }
    spinlock_release(&dcb->session->ses_lock);
    return rval;
}

/**
 * @brief Authentication of backend - read the reply, or handle an error
 *
 * @param dcb               Descriptor control block for backend server
 * @param local_session     The current MySQL session data structure
 * @return 0 = fail, 1 = success, 2 = success and data to be transferred
 */
static int
gw_read_reply_or_error(DCB *dcb, MYSQL_session local_session)
{
    int return_code = 0;
    SESSION *session = dcb->session;
    MySQLProtocol *backend_protocol = (MySQLProtocol *)dcb->protocol;
    CHK_PROTOCOL(backend_protocol);

        if (SESSION_STATE_DUMMY == session->state)
        {
            spinlock_release(&dcb->authlock);
            return 0;
        }
        CHK_SESSION(session);

        uint16_t code = 0;
        if (backend_protocol->protocol_auth_state == MYSQL_AUTH_RECV)
        {
            /**
             * Read backend's reply to authentication message
             */
            int receive_rc = gw_receive_backend_auth(backend_protocol, &code);

            switch (receive_rc)
            {
                case -1:
                    backend_protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
                    MXS_ERROR("Backend server didn't "
                          "accept authentication for user "
                          "%s.",
                          local_session.user);
                    break;
                case 1:
                    backend_protocol->protocol_auth_state = MYSQL_IDLE;
                    MXS_DEBUG("%lu [gw_read_backend_event] "
                          "gw_receive_backend_auth succeed. "
                          "dcb %p fd %d, user %s.",
                          pthread_self(),
                          dcb,
                          dcb->fd,
                          local_session.user);
                    break;
                default:
                    ss_dassert(receive_rc == 0);
                    MXS_DEBUG("%lu [gw_read_backend_event] "
                          "gw_receive_backend_auth read "
                          "successfully "
                          "nothing. dcb %p fd %d, user %s.",
                          pthread_self(),
                          dcb,
                          dcb->fd,
                          local_session.user);
                    spinlock_release(&dcb->authlock);
                    return 0;
            } /* switch */
        }

        if (backend_protocol->protocol_auth_state == MYSQL_AUTH_FAILED ||
            backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED)
        {
            GWBUF* errbuf;
            bool succp;
            /**
             * protocol state won't change anymore, lock can be freed.
             * First free delay queue - which is only ever processed while
             * authlock is held.
             */
            gwbuf_free(dcb->delayq);
            dcb->delayq = NULL;
            spinlock_release(&dcb->authlock);

            /* Only reload the users table if authentication failed and the
             * client session is not stopping. It is possible that authentication
             * fails because the client has closed the connection before all
             * backends have done authentication. */
            if (backend_protocol->protocol_auth_state == MYSQL_AUTH_FAILED &&
                dcb->session->state != SESSION_STATE_STOPPING)
            {
                // If the authentication failed due to too many connections,
                // we do not refresh the users as it would not change anything.
                if (code != ER_TOO_MANY_USER_CONNECTIONS)
                {
                    service_refresh_users(dcb->session->service);
                }
            }
#if defined(SS_DEBUG)
            MXS_DEBUG("%lu [gw_read_backend_event] "
                  "calling handleError. Backend "
                  "DCB %p, session %p",
                  pthread_self(),
                  dcb,
                  dcb->session);
#endif
            errbuf = mysql_create_custom_error(1,
                                   0,
                                   "Authentication with backend failed. "
                                   "Session will be closed.");

            if (session->router_session)
            {
                session->service->router->handleError(
                    session->service->router_instance,
                    session->router_session,
                    errbuf,
                    dcb,
                    ERRACT_REPLY_CLIENT,
                    &succp);
            spinlock_acquire(&session->ses_lock);
            session->state = SESSION_STATE_STOPPING;
            spinlock_release(&session->ses_lock);
            ss_dassert(dcb->dcb_errhandle_called);
            }
            else
            {
                dcb->dcb_errhandle_called = true;
                /*
                 * I'm pretty certain this is best removed and
                 * causes trouble if present, but have left it
                 * here just for now as a comment. Martin
                 */
                /* dcb_close(dcb); */
            }
            gwbuf_free(errbuf);
            return 1;
        }
        else
        {
            MXS_DEBUG("%lu [gw_read_backend_event] "
                  "gw_receive_backend_auth succeed. Fd %d, "
                  "user %s.",
                  pthread_self(),
                  dcb->fd,
                  local_session.user);

            /* check the delay queue and flush the data */
            if (dcb->delayq)
            {
                return_code = backend_write_delayqueue(dcb);
                spinlock_release(&dcb->authlock);
                return return_code;
            }
        }
    spinlock_release(&dcb->authlock);
    return 2;
} /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED */

/**
 * @brief With authentication completed, read new data and write to backend
 *
 * @param dcb           Descriptor control block for backend server
 * @param local_session Current MySQL session data structure
 * @return 0 is fail, 1 is success
 */
static int
gw_read_and_write(DCB *dcb, MYSQL_session local_session)
{
        GWBUF *read_buffer = NULL;
        SESSION *session = dcb->session;
        int nbytes_read;
        int return_code;

        CHK_SESSION(session);

        /* read available backend data */
        return_code = dcb_read(dcb, &read_buffer, 0);

        if (return_code < 0)
        {
            GWBUF* errbuf;
            bool succp;
            errbuf = mysql_create_custom_error(1,
                                               0,
                                               "Read from backend failed");

            session->service->router->handleError(
                session->service->router_instance,
                                session->router_session,
                                errbuf,
                                dcb,
                                ERRACT_NEW_CONNECTION,
                                &succp);
            gwbuf_free(errbuf);

            if (!succp)
            {
                spinlock_acquire(&session->ses_lock);
                session->state = SESSION_STATE_STOPPING;
                spinlock_release(&session->ses_lock);
            }
            return_code = 0;
            goto return_rc;
        }

        nbytes_read = gwbuf_length(read_buffer);
        if (nbytes_read == 0)
        {
            ss_dassert(read_buffer == NULL);
            goto return_rc;
        }
        else
        {
            ss_dassert(read_buffer != NULL);
        }

        if (nbytes_read < 3)
        {
            dcb->dcb_readqueue = read_buffer;
            return_code = 0;
            goto return_rc;
        }

        {
            GWBUF *tmp = modutil_get_complete_packets(&read_buffer);
            /* Put any residue into the read queue */
            spinlock_acquire(&dcb->authlock);
            dcb->dcb_readqueue = read_buffer;
            spinlock_release(&dcb->authlock);
            if (tmp == NULL)
            {
                /** No complete packets */
                return_code = 0;
                goto return_rc;
            }
            else
            {
                read_buffer = tmp;
            }
        }

    do
    {
        GWBUF *stmt = NULL;
        /**
         * If protocol has session command set, concatenate whole
         * response into one buffer.
         */
        if (protocol_get_srv_command((MySQLProtocol *)dcb->protocol, false) != MYSQL_COM_UNDEFINED)
        {
            stmt = process_response_data(dcb, &read_buffer, gwbuf_length(read_buffer));
            /**
             * Received incomplete response to session command.
             * Store it to readqueue and return.
             */
            if (!sescmd_response_complete(dcb))
            {
                stmt = gwbuf_append(stmt, read_buffer);
                spinlock_acquire(&dcb->authlock);
                dcb->dcb_readqueue = gwbuf_append(stmt, dcb->dcb_readqueue);
                spinlock_release(&dcb->authlock);
                return_code = 0;
                goto return_rc;
            }

            if (!stmt)
            {
                MXS_NOTICE("%lu [gw_read_backend_event] "
                           "Read buffer unexpectedly null, even though response "
                           "not marked as complete. User: %s",
                           pthread_self(),
                           local_session.user);
                return_code = 0;
                goto return_rc;
            }
        }
        else
        {
            stmt = read_buffer;
            read_buffer = NULL;
        }

        /**
         * Check that session is operable, and that client DCB is
         * still listening the socket for replies.
         */
        if (dcb->session->state == SESSION_STATE_ROUTER_READY &&
            dcb->session->client_dcb != NULL &&
            dcb->session->client_dcb->state == DCB_STATE_POLLING &&
            (session->router_session ||
             session->service->router->getCapabilities() & (int)RCAP_TYPE_NO_RSESSION))
        {
            MySQLProtocol *client_protocol = (MySQLProtocol *)dcb->session->client_dcb->protocol;
            if (client_protocol != NULL)
            {
                CHK_PROTOCOL(client_protocol);

                if (client_protocol->protocol_auth_state == MYSQL_IDLE)
                {
                    gwbuf_set_type(stmt, GWBUF_TYPE_MYSQL);

                    session->service->router->clientReply(session->service->router_instance,
                                                          session->router_session,
                                                          stmt, dcb);
                    return_code = 1;
                }
            }
            else if (dcb->session->client_dcb->dcb_role == DCB_ROLE_INTERNAL)
            {
                gwbuf_set_type(stmt, GWBUF_TYPE_MYSQL);
                session->service->router->clientReply(session->service->router_instance,
                                                      session->router_session,
                                                      stmt, dcb);
                return_code = 1;
            }
        }
        else /*< session is closing; replying to client isn't possible */
        {
            gwbuf_free(stmt);
        }
    }
    while (read_buffer);

return_rc:
    return return_code;
}

/*
 * EPOLLOUT handler for the MySQL Backend protocol module.
 *
 * @param dcb   The descriptor control block
 * @return      1 in success, 0 in case of failure,
 */
static int gw_write_backend_event(DCB *dcb)
{
    int rc = 0;
    MySQLProtocol *backend_protocol = dcb->protocol;

    /*<
     * Don't write to backend if backend_dcb is not in poll set anymore.
     */
    if (dcb->state != DCB_STATE_POLLING)
    {
        uint8_t* data = NULL;
        bool com_quit = false;

        spinlock_acquire(&dcb->writeqlock);
        if (dcb->writeq)
        {
            data = (uint8_t *) GWBUF_DATA(dcb->writeq);
            com_quit = MYSQL_IS_COM_QUIT(data);
            rc = 0;
        }
        spinlock_release(&dcb->writeqlock);


        if (data && !com_quit)
        {
            mysql_send_custom_error(dcb->session->client_dcb, 1, 0,
                                    "Writing to backend failed due invalid Maxscale state.");
            MXS_DEBUG("%lu [gw_write_backend_event] Write to backend "
                      "dcb %p fd %d failed due invalid state %s.",
                      pthread_self(), dcb, dcb->fd, STRDCBSTATE(dcb->state));

            MXS_ERROR("Attempt to write buffered data to backend "
                      "failed due internal inconsistent state.");
        }
        else
        {
            MXS_DEBUG("%lu [gw_write_backend_event] Dcb %p in state %s "
                      "but there's nothing to write either.",
                      pthread_self(), dcb, STRDCBSTATE(dcb->state));
            rc = 1;
        }

        goto return_rc;
    }

    if (backend_protocol->protocol_auth_state == MYSQL_PENDING_CONNECT)
    {
        backend_protocol->protocol_auth_state = MYSQL_CONNECTED;
        rc = 1;
        goto return_rc;
    }
    dcb_drain_writeq(dcb);
    rc = 1;
return_rc:
    MXS_DEBUG("%lu [gw_write_backend_event] "
              "wrote to dcb %p fd %d, return %d",
              pthread_self(),
              dcb,
              dcb->fd,
              rc);

    return rc;
}

/*
 * Write function for backend DCB. Store command to protocol.
 *
 * @param dcb   The DCB of the backend
 * @param queue Queue of buffers to write
 * @return      0 on failure, 1 on success
 */
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue)
{
    MySQLProtocol *backend_protocol = dcb->protocol;
    int rc = 0;

    CHK_DCB(dcb);
    spinlock_acquire(&dcb->authlock);
    /**
     * Pick action according to state of protocol.
     * If auth failed, return value is 0, write and buffered write
     * return 1.
     */
    switch (backend_protocol->protocol_auth_state)
    {
        case MYSQL_HANDSHAKE_FAILED:
        case MYSQL_AUTH_FAILED:
            if (dcb->session->state != SESSION_STATE_STOPPING)
            {
                MXS_ERROR("Unable to write to backend '%s' due to "
                          "%s failure. Server in state %s.",
                          dcb->server->unique_name,
                          backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED ?
                          "handshake" : "authentication",
                          STRSRVSTATUS(dcb->server));
            }
            /** Consume query buffer */
            while ((queue = gwbuf_consume(
                                          queue,
                                          GWBUF_LENGTH(queue))) != NULL)
            {
                ;
            }
            rc = 0;
            spinlock_release(&dcb->authlock);
            break;

        case MYSQL_IDLE:
        {
            uint8_t* ptr = GWBUF_DATA(queue);
            mysql_server_cmd_t cmd = MYSQL_GET_COMMAND(ptr);

            MXS_DEBUG("%lu [gw_MySQLWrite_backend] write to dcb %p "
                      "fd %d protocol state %s.",
                      pthread_self(),
                      dcb,
                      dcb->fd,
                      STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));

            spinlock_release(&dcb->authlock);
            /**
             * Statement type is used in readwrite split router.
             * Command is *not* set for readconn router.
             *
             * Server commands are stored to MySQLProtocol structure
             * if buffer always includes a single statement.
             */
            if (GWBUF_IS_TYPE_SINGLE_STMT(queue) &&
                GWBUF_IS_TYPE_SESCMD(queue))
            {
                /** Record the command to backend's protocol */
                protocol_add_srv_command(backend_protocol, cmd);
            }
            /** Write to backend */
            rc = dcb_write(dcb, queue);
        }
        break;

        default:
        {
            MXS_DEBUG("%lu [gw_MySQLWrite_backend] delayed write to "
                      "dcb %p fd %d protocol state %s.",
                      pthread_self(),
                      dcb,
                      dcb->fd,
                      STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));
            /**
             * In case of session commands, store command to DCB's
             * protocol struct.
             */
            if (GWBUF_IS_TYPE_SINGLE_STMT(queue) &&
                GWBUF_IS_TYPE_SESCMD(queue))
            {
                uint8_t* ptr = GWBUF_DATA(queue);
                mysql_server_cmd_t cmd = MYSQL_GET_COMMAND(ptr);

                /** Record the command to backend's protocol */
                protocol_add_srv_command(backend_protocol, cmd);
            }
            /*<
             * Now put the incoming data to the delay queue unless backend is
             * connected with auth ok
             */
            backend_set_delayqueue(dcb, queue);
            spinlock_release(&dcb->authlock);
            rc = 1;
        }
        break;
    }
    return rc;
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error
 * handler fails in providing enough backend servers, mark session being
 * closed and call DCB close function which triggers closing router session
 * and related backends (if any exists.
 */
static int gw_error_backend_event(DCB *dcb)
{
    SESSION* session;
    void* rsession;
    ROUTER_OBJECT* router;
    ROUTER* router_instance;
    GWBUF* errbuf;
    bool succp;
    session_state_t ses_state;

    CHK_DCB(dcb);
    session = dcb->session;
    CHK_SESSION(session);
    if (SESSION_STATE_DUMMY == session->state)
    {
        if (dcb->persistentstart == 0)
        {
            /** Not a persistent connection, something is wrong. */
            MXS_ERROR("EPOLLERR event on a non-persistent DCB with no session. "
                      "Closing connection.");
        }
        dcb_close(dcb);
        return 1;
    }
    rsession = session->router_session;
    router = session->service->router;
    router_instance = session->service->router_instance;

    /**
     * Avoid running redundant error handling procedure.
     * dcb_close is already called for the DCB. Thus, either connection is
     * closed by router and COM_QUIT sent or there was an error which
     * have already been handled.
     */
    if (dcb->state != DCB_STATE_POLLING)
    {
        int error, len;

        len = sizeof(error);

        if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len) == 0)
        {
            if (error != 0)
            {
                char errstring[STRERROR_BUFLEN];
                MXS_ERROR("DCB in state %s got error '%s'.",
                          STRDCBSTATE(dcb->state),
                          strerror_r(error, errstring, sizeof(errstring)));
            }
        }
        return 1;
    }
    errbuf = mysql_create_custom_error(1,
                                       0,
                                       "Lost connection to backend server.");

    spinlock_acquire(&session->ses_lock);
    ses_state = session->state;
    spinlock_release(&session->ses_lock);

    /**
     * Session might be initialized when DCB already is in the poll set.
     * Thus hangup can occur in the middle of session initialization.
     * Only complete and successfully initialized sessions allow for
     * calling error handler.
     */
    while (ses_state == SESSION_STATE_READY)
    {
        spinlock_acquire(&session->ses_lock);
        ses_state = session->state;
        spinlock_release(&session->ses_lock);
    }

    if (ses_state != SESSION_STATE_ROUTER_READY)
    {
        int error, len;

        len = sizeof(error);
        if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len) == 0)
        {
            if (error != 0)
            {
                char errstring[STRERROR_BUFLEN];
                MXS_ERROR("Error '%s' in session that is not ready for routing.",
                          strerror_r(error, errstring, sizeof(errstring)));
            }
        }
        gwbuf_free(errbuf);
        goto retblock;
    }

#if defined(SS_DEBUG)
    MXS_INFO("Backend error event handling.");
#endif
    router->handleError(router_instance,
                        rsession,
                        errbuf,
                        dcb,
                        ERRACT_NEW_CONNECTION,
                        &succp);
    gwbuf_free(errbuf);

    /**
     * If error handler fails it means that routing session can't continue
     * and it must be closed. In success, only this DCB is closed.
     */
    if (!succp)
    {
        spinlock_acquire(&session->ses_lock);
        session->state = SESSION_STATE_STOPPING;
        spinlock_release(&session->ses_lock);
    }

retblock:
    return 1;
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error
 * handler fails in providing enough backend servers, mark session being
 * closed and call DCB close function which triggers closing router session
 * and related backends (if any exists.
 *
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int gw_backend_hangup(DCB *dcb)
{
    SESSION* session;
    void* rsession;
    ROUTER_OBJECT* router;
    ROUTER* router_instance;
    bool succp;
    GWBUF* errbuf;
    session_state_t ses_state;

    CHK_DCB(dcb);
    if (dcb->persistentstart)
    {
        dcb->dcb_errhandle_called = true;
        goto retblock;
    }
    session = dcb->session;

    if (session == NULL)
    {
        goto retblock;
    }

    CHK_SESSION(session);

    rsession = session->router_session;
    router = session->service->router;
    router_instance = session->service->router_instance;

    errbuf = mysql_create_custom_error(1,
                                       0,
                                       "Lost connection to backend server.");

    spinlock_acquire(&session->ses_lock);
    ses_state = session->state;
    spinlock_release(&session->ses_lock);

    /**
     * Session might be initialized when DCB already is in the poll set.
     * Thus hangup can occur in the middle of session initialization.
     * Only complete and successfully initialized sessions allow for
     * calling error handler.
     */
    while (ses_state == SESSION_STATE_READY)
    {
        spinlock_acquire(&session->ses_lock);
        ses_state = session->state;
        spinlock_release(&session->ses_lock);
    }

    if (ses_state != SESSION_STATE_ROUTER_READY)
    {
        int error, len;

        len = sizeof(error);
        if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len) == 0)
        {
            if (error != 0 && ses_state != SESSION_STATE_STOPPING)
            {
                char errstring[STRERROR_BUFLEN];
                MXS_ERROR("Hangup in session that is not ready for routing, "
                          "Error reported is '%s'.",
                          strerror_r(error, errstring, sizeof(errstring)));
            }
        }
        gwbuf_free(errbuf);
        /*
         * I'm pretty certain this is best removed and
         * causes trouble if present, but have left it
         * here just for now as a comment. Martin
         */
        /* dcb_close(dcb); */
        goto retblock;
    }

    router->handleError(router_instance,
                        rsession,
                        errbuf,
                        dcb,
                        ERRACT_NEW_CONNECTION,
                        &succp);

    gwbuf_free(errbuf);
    /** There are no required backends available, close session. */
    if (!succp)
    {
        spinlock_acquire(&session->ses_lock);
        session->state = SESSION_STATE_STOPPING;
        spinlock_release(&session->ses_lock);
    }

retblock:
    return 1;
}

/**
 * Send COM_QUIT to backend so that it can be closed.
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int gw_backend_close(DCB *dcb)
{
    SESSION* session;
    GWBUF* quitbuf;

    CHK_DCB(dcb);
    session = dcb->session;

    MXS_DEBUG("%lu [gw_backend_close]", pthread_self());

    quitbuf = mysql_create_com_quit(NULL, 0);
    gwbuf_set_type(quitbuf, GWBUF_TYPE_MYSQL);

    /** Send COM_QUIT to the backend being closed */
    mysql_send_com_quit(dcb, 0, quitbuf);

    mysql_protocol_done(dcb);

    if (session)
    {
        CHK_SESSION(session);
        /**
         * The lock is needed only to protect the read of session->state and
         * session->client_dcb values. Client's state may change by other thread
         * but client's close and adding client's DCB to zombies list is executed
         * only if client's DCB's state does _not_ change in parallel.
         */
        spinlock_acquire(&session->ses_lock);
        /**
         * If session->state is STOPPING, start closing client session.
         * Otherwise only this backend connection is closed.
         */
        if (session->state == SESSION_STATE_STOPPING &&
            session->client_dcb != NULL)
        {
            if (session->client_dcb->state == DCB_STATE_POLLING)
            {
                spinlock_release(&session->ses_lock);

                /** Close client DCB */
                dcb_close(session->client_dcb);
            }
            else
            {
                spinlock_release(&session->ses_lock);
            }
        }
        else
        {
            spinlock_release(&session->ses_lock);
        }
    }
    return 1;
}

/**
 * This routine put into the delay queue the input queue
 * The input is what backend DCB is receiving
 * The routine is called from func.write() when mysql backend connection
 * is not yet complete buu there are inout data from client
 *
 * @param dcb   The current backend DCB
 * @param queue Input data in the GWBUF struct
 */
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue)
{
    /* Append data */
    dcb->delayq = gwbuf_append(dcb->delayq, queue);
}

/**
 * This routine writes the delayq via dcb_write
 * The dcb->delayq contains data received from the client before
 * mysql backend authentication succeded
 *
 * @param dcb The current backend DCB
 * @return The dcb_write status
 */
static int backend_write_delayqueue(DCB *dcb)
{
    GWBUF *localq = NULL;
    int rc;

    if (dcb->delayq == NULL)
    {
        rc = 1;
    }
    else
    {
        localq = dcb->delayq;
        dcb->delayq = NULL;

        if (MYSQL_IS_CHANGE_USER(((uint8_t *)GWBUF_DATA(localq))))
        {
            MYSQL_session mses;
            GWBUF* new_packet;

            gw_get_shared_session_auth_info(dcb, &mses);
            new_packet = gw_create_change_user_packet(&mses, dcb->protocol);
            /**
             * Remove previous packet which lacks scramble
             * and append the new.
             */
            localq = gwbuf_consume(localq, GWBUF_LENGTH(localq));
            localq = gwbuf_append(localq, new_packet);
        }
        rc = dcb_write(dcb, localq);
    }

    if (rc == 0)
    {
        GWBUF* errbuf;
        bool succp;
        ROUTER_OBJECT *router = NULL;
        ROUTER *router_instance = NULL;
        void *rsession = NULL;
        SESSION *session = dcb->session;

        CHK_SESSION(session);

        if (session != NULL)
        {
            router = session->service->router;
            router_instance = session->service->router_instance;
            rsession = session->router_session;
#if defined(SS_DEBUG)
            MXS_INFO("Backend write delayqueue error handling.");
#endif
            errbuf = mysql_create_custom_error(1,
                                               0,
                                               "Failed to write buffered data to back-end server. "
                                               "Buffer was empty or back-end was disconnected during "
                                               "operation. Attempting to find a new backend.");

            router->handleError(router_instance,
                                rsession,
                                errbuf,
                                dcb,
                                ERRACT_NEW_CONNECTION,
                                &succp);
            gwbuf_free(errbuf);

            if (!succp)
            {
                spinlock_acquire(&session->ses_lock);
                session->state = SESSION_STATE_STOPPING;
                spinlock_release(&session->ses_lock);
            }
        }
    }
    return rc;
}

/**
 * This routine handles the COM_CHANGE_USER command
 *
 * @param dcb           The current backend DCB
 * @param server        The backend server pointer
 * @param in_session    The current session data (MYSQL_session)
 * @param queue         The GWBUF containing the COM_CHANGE_USER receveid
 * @return 1 on success and 0 on failure
 */
static int gw_change_user(DCB *backend,
                          SERVER *server,
                          SESSION *in_session,
                          GWBUF *queue)
{
    MYSQL_session *current_session = NULL;
    MySQLProtocol *backend_protocol = NULL;
    MySQLProtocol *client_protocol = NULL;
    char username[MYSQL_USER_MAXLEN + 1] = "";
    char database[MYSQL_DATABASE_MAXLEN + 1] = "";
    char current_database[MYSQL_DATABASE_MAXLEN + 1] = "";
    uint8_t client_sha1[MYSQL_SCRAMBLE_LEN] = "";
    uint8_t *client_auth_packet = GWBUF_DATA(queue);
    unsigned int auth_token_len = 0;
    uint8_t *auth_token = NULL;
    int rv = -1;
    int auth_ret = 1;

    current_session = (MYSQL_session *)in_session->client_dcb->data;
    backend_protocol = backend->protocol;
    client_protocol = in_session->client_dcb->protocol;

    /* now get the user, after 4 bytes header and 1 byte command */
    client_auth_packet += 5;
    strncpy(username, (char *)client_auth_packet, MYSQL_USER_MAXLEN);
    client_auth_packet += strlen(username) + 1;

    /* get the auth token len */
    memcpy(&auth_token_len, client_auth_packet, 1);

    client_auth_packet++;

    /* allocate memory for token only if auth_token_len > 0 */
    if (auth_token_len > 0)
    {
        auth_token = (uint8_t *)malloc(auth_token_len);
        ss_dassert(auth_token != NULL);

        if (auth_token == NULL)
        {
            return rv;
        }
        memcpy(auth_token, client_auth_packet, auth_token_len);
        client_auth_packet += auth_token_len;
    }

    /* get new database name */
    strncpy(database, (char *)client_auth_packet, MYSQL_DATABASE_MAXLEN);

    /* get character set */
    if (strlen(database))
    {
        client_auth_packet += strlen(database) + 1;
    }
    else
    {
        client_auth_packet++;
    }

    if (client_auth_packet && *client_auth_packet)
    {
        memcpy(&backend_protocol->charset, client_auth_packet, sizeof(int));
    }

    spinlock_acquire(&in_session->ses_lock);

    /* save current_database name */
    strncpy(current_database, current_session->db, MYSQL_DATABASE_MAXLEN);

    /*
     * Now clear database name in dcb as we don't do local authentication on db name for change user.
     * Local authentication only for user@host and if successful the database name change is sent to backend.
     */
    strncpy(current_session->db, "", MYSQL_DATABASE_MAXLEN);

    /*
     * Decode the token and check the password.
     * Note: if auth_token_len == 0 && auth_token == NULL, user is without password
     */
    auth_ret = gw_check_mysql_scramble_data(backend->session->client_dcb,
                                            auth_token, auth_token_len,
                                            client_protocol->scramble,
                                            sizeof(client_protocol->scramble),
                                            username, client_sha1);
    strncpy(current_session->db, current_database, MYSQL_DATABASE_MAXLEN);
    spinlock_release(&in_session->ses_lock);

    if (auth_ret != 0)
    {
        if (service_refresh_users(backend->session->client_dcb->service) == 0)
        {
            /* Try authentication again with new repository data */
            /* Note: if no auth client authentication will fail */
            spinlock_acquire(&in_session->ses_lock);
            strncpy(current_session->db, "", MYSQL_DATABASE_MAXLEN);
            auth_ret = gw_check_mysql_scramble_data(
                                                    backend->session->client_dcb,
                                                    auth_token, auth_token_len,
                                                    client_protocol->scramble,
                                                    sizeof(client_protocol->scramble),
                                                    username, client_sha1);
            strncpy(current_session->db, current_database, MYSQL_DATABASE_MAXLEN);
            spinlock_release(&in_session->ses_lock);
        }
    }

    /* let's free the auth_token now */
    if (auth_token)
    {
        free(auth_token);
    }

    if (auth_ret != 0)
    {
        char *password_set = NULL;
        char *message = NULL;

        if (auth_token_len > 0)
        {
            password_set = (char *)client_sha1;
        }
        else
        {
            password_set = "";
        }

        /**
         * Create an error message and make it look like legit reply
         * from backend server. Then make it look like an incoming event
         * so that thread gets new task of it, calls clientReply
         * which filters out duplicate errors from same cause and forward
         * reply to the client.
         */
        message = create_auth_fail_str(username,
                                       backend->session->client_dcb->remote,
                                       password_set,
                                       "",
                                       auth_ret);
        if (message == NULL)
        {
            MXS_ERROR("Creating error message failed.");
            rv = 0;
            goto retblock;
        }
        /**
         * Add command to backend's protocol, create artificial reply
         * packet and add it to client's read buffer.
         */
        protocol_add_srv_command((MySQLProtocol*)backend->protocol,
                                 MYSQL_COM_CHANGE_USER);
        modutil_reply_auth_error(backend, message, 0);
        rv = 1;
    }
    else
    {
        /** This assumes that authentication will succeed. If authentication fails,
         * the internal session will represent the wrong user. This is wrong and
         * a check whether the COM_CHANGE_USER succeeded should be done in the
         * backend protocol reply handling.
         *
         * For the time being, it is simpler to assume a COM_CHANGE_USER will always
         * succeed if the authentication in MaxScale is successful. In practice this
         * might not be true but these cases are handled by the router modules
         * and the servers that fail to execute the COM_CHANGE_USER are discarded. */
        strcpy(current_session->user, username);
        strcpy(current_session->db, database);
        memcpy(current_session->client_sha1, client_sha1, sizeof(current_session->client_sha1));
        rv = gw_send_change_user_to_backend(database, username, client_sha1, backend_protocol);
    }

retblock:
    gwbuf_free(queue);

    return rv;
}

/**
 * Move packets or parts of packets from readbuf to outbuf as the packet headers
 * and lengths have been noticed and counted.
 * Session commands need to be marked so that they can be handled properly in
 * the router's clientReply.
 *
 * @param dcb                   Backend's DCB where data was read from
 * @param readbuf               GWBUF where data was read to
 * @param nbytes_to_process     Number of bytes that has been read and need to be processed
 *
 * @return GWBUF which includes complete MySQL packet
 */
static GWBUF* process_response_data(DCB* dcb,
                                    GWBUF** readbuf,
                                    int nbytes_to_process)
{
    int npackets_left = 0; /*< response's packet count */
    ssize_t nbytes_left = 0; /*< nbytes to be read for the packet */
    MySQLProtocol* p;
    GWBUF* outbuf = NULL;
    int initial_packets = npackets_left;
    ssize_t initial_bytes = nbytes_left;

    /** Get command which was stored in gw_MySQLWrite_backend */
    p = DCB_PROTOCOL(dcb, MySQLProtocol);
    if (!DCB_IS_CLONE(dcb))
    {
        CHK_PROTOCOL(p);
    }

    /** All buffers processed here are sescmd responses */
    gwbuf_set_type(*readbuf, GWBUF_TYPE_SESCMD_RESPONSE);

    /**
     * Now it is known how many packets there should be and how much
     * is read earlier.
     */
    while (nbytes_to_process != 0)
    {
        mysql_server_cmd_t srvcmd;
        bool succp;

        srvcmd = protocol_get_srv_command(p, false);

        MXS_DEBUG("%lu [process_response_data] Read command %s for DCB %p fd %d.",
                  pthread_self(),
                  STRPACKETTYPE(srvcmd),
                  dcb,
                  dcb->fd);
        /**
         * Read values from protocol structure, fails if values are
         * uninitialized.
         */
        if (npackets_left == 0)
        {
            succp = protocol_get_response_status(p, &npackets_left, &nbytes_left);

            if (!succp || npackets_left == 0)
            {
                /**
                 * Examine command type and the readbuf. Conclude response
                 * packet count from the command type or from the first
                 * packet content. Fails if read buffer doesn't include
                 * enough data to read the packet length.
                 */
                init_response_status(*readbuf, srvcmd, &npackets_left, &nbytes_left);
            }

            initial_packets = npackets_left;
            initial_bytes = nbytes_left;
        }
        /** Only session commands with responses should be processed */
        ss_dassert(npackets_left > 0);

        /** Read incomplete packet. */
        if (nbytes_left > nbytes_to_process)
        {
            /** Includes length info so it can be processed */
            if (nbytes_to_process >= 5)
            {
                /** discard source buffer */
                *readbuf = gwbuf_consume(*readbuf, GWBUF_LENGTH(*readbuf));
                nbytes_left -= nbytes_to_process;
            }
            nbytes_to_process = 0;
        }
            /** Packet was read. All bytes belonged to the last packet. */
        else if (nbytes_left == nbytes_to_process)
        {
            nbytes_left = 0;
            nbytes_to_process = 0;
            ss_dassert(npackets_left > 0);
            npackets_left -= 1;
            outbuf = gwbuf_append(outbuf, *readbuf);
            *readbuf = NULL;
        }
        /**
         * Buffer contains more data than we need. Split the complete packet and
         * the extra data into two separate buffers.
         */
        else
        {
            ss_dassert(nbytes_left < nbytes_to_process);
            ss_dassert(nbytes_left > 0);
            ss_dassert(npackets_left > 0);
            outbuf = gwbuf_append(outbuf, gwbuf_split(readbuf, nbytes_left));
            nbytes_to_process -= nbytes_left;
            npackets_left -= 1;
            nbytes_left = 0;
        }

        /** Store new status to protocol structure */
        protocol_set_response_status(p, npackets_left, nbytes_left);

        /** A complete packet was read */
        if (nbytes_left == 0)
        {
            /** No more packets in this response */
            if (npackets_left == 0 && outbuf != NULL)
            {
                GWBUF* b = outbuf;

                while (b->next != NULL)
                {
                    b = b->next;
                }
                /** Mark last as end of response */
                gwbuf_set_type(b, GWBUF_TYPE_RESPONSE_END);

                /** Archive the command */
                protocol_archive_srv_command(p);
            }
                /** Read next packet */
            else
            {
                uint8_t* data;

                /** Read next packet length if there is at least
                 * three bytes left. If there is less than three
                 * bytes in the buffer or it is NULL, we need to
                 wait for more data from the backend server.*/
                if (*readbuf == NULL || gwbuf_length(*readbuf) < 3)
                {
                    MXS_DEBUG("%lu [%s] Read %d packets. Waiting for %d more "
                              "packets for a total of %d packets.",
                              pthread_self(), __FUNCTION__,
                              initial_packets - npackets_left,
                              npackets_left, initial_packets);

                    /** Store the already read data into the readqueue of the DCB
                     * and restore the response status to the initial number of packets */
                    spinlock_acquire(&dcb->authlock);
                    dcb->dcb_readqueue = gwbuf_append(outbuf, dcb->dcb_readqueue);
                    spinlock_release(&dcb->authlock);
                    protocol_set_response_status(p, initial_packets, initial_bytes);
                    return NULL;
                }
                uint8_t packet_len[3];
                gwbuf_copy_data(*readbuf, 0, 3, packet_len);
                nbytes_left = gw_mysql_get_byte3(packet_len) + MYSQL_HEADER_LEN;
                /** Store new status to protocol structure */
                protocol_set_response_status(p, npackets_left, nbytes_left);
            }
        }
    }
    return outbuf;
}

static bool sescmd_response_complete(DCB* dcb)
{
    int npackets_left;
    ssize_t nbytes_left;
    MySQLProtocol* p;
    bool succp;

    p = DCB_PROTOCOL(dcb, MySQLProtocol);
    if (!DCB_IS_CLONE(dcb))
    {
        CHK_PROTOCOL(p);
    }

    protocol_get_response_status(p, &npackets_left, &nbytes_left);

    if (npackets_left == 0)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }
    return succp;
}

/**
 * gw_decode_mysql_server_handshake
 *
 * Decode mysql server handshake
 *
 * @param conn The MySQLProtocol structure
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 *
 */
static int
gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload)
{
    uint8_t *server_version_end = NULL;
    uint16_t mysql_server_capabilities_one = 0;
    uint16_t mysql_server_capabilities_two = 0;
    unsigned long tid = 0;
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
    tid = gw_mysql_get_byte4(payload);
    memcpy(&conn->tid, &tid, 4);

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
        scramble_len = payload[0] -1;
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
 * Receive the MySQL authentication packet from backend, packet # is 2
 *
 * @param protocol The MySQL protocol structure
 * @param code     [out] The protocol error code, if -1 is returned.

 * @return -1 in case of failure,
 *          0 if there was nothing to read,
 *          1 if read was successful.
 */
static int
gw_receive_backend_auth(MySQLProtocol *protocol, uint16_t *code)
{
    int n = -1;
    GWBUF *head = NULL;
    DCB *dcb = protocol->owner_dcb;
    uint8_t *ptr = NULL;
    int rc = 0;

    n = dcb_read(dcb, &head, 0);

    dcb->last_read = hkheartbeat;

    /*<
     * Read didn't fail and there is enough data for mysql packet.
     */
    if (n != -1 &&
        head != NULL &&
        GWBUF_LENGTH(head) >= 5)
    {
        ptr = GWBUF_DATA(head);
        /*<
         * 5th byte is 0x0 if successful.
         */
        if (ptr[4] == 0x00)
        {
            rc = 1;
        }
        else if (ptr[4] == 0xff)
        {
            size_t len = MYSQL_GET_PACKET_LEN(ptr);
            *code = MYSQL_GET_ERRCODE(ptr);
            char* err = strndup(&((char *)ptr)[8], 5);
            char* bufstr = strndup(&((char *)ptr)[13], len - 4 - 5);

            MXS_DEBUG("%lu [gw_receive_backend_auth] Invalid "
                      "authentication message from backend dcb %p "
                      "fd %d, ptr[4] = %d, error %s, msg %s.",
                      pthread_self(),
                      dcb,
                      dcb->fd,
                      ptr[4],
                      err,
                      bufstr);

            MXS_ERROR("Invalid authentication message "
                      "from backend. Error : %s, Msg : %s",
                      err,
                      bufstr);

            free(bufstr);
            free(err);
            rc = -1;
        }
        else
        {
            MXS_DEBUG("%lu [gw_receive_backend_auth] Invalid "
                      "authentication message from backend dcb %p "
                      "fd %d, ptr[4] = %d",
                      pthread_self(),
                      dcb,
                      dcb->fd,
                      ptr[4]);

            MXS_ERROR("Invalid authentication message "
                      "from backend. Packet type : %d",
                      ptr[4]);
        }
        /*<
         * Remove data from buffer.
         */
        while ((head = gwbuf_consume(head, GWBUF_LENGTH(head))) != NULL)
        {
            ;
        }
    }
    else if (n == 0)
    {
        /*<
         * This is considered as success because call didn't fail,
         * although no bytes was read.
         */
        rc = 0;
        MXS_DEBUG("%lu [gw_receive_backend_auth] Read zero bytes from "
                  "backend dcb %p fd %d in state %s. n %d, head %p, len %ld",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  STRDCBSTATE(dcb->state),
                  n,
                  head,
                  (head == NULL) ? 0 : GWBUF_LENGTH(head));
    }
    else
    {
        ss_dassert(n < 0 && head == NULL);
        rc = -1;
        MXS_DEBUG("%lu [gw_receive_backend_auth] Reading from backend dcb %p "
                  "fd %d in state %s failed. n %d, head %p, len %ld",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  STRDCBSTATE(dcb->state),
                  n,
                  head,
                  (head == NULL) ? 0 : GWBUF_LENGTH(head));
    }

    return rc;
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
 * @note Capability bits are defined in mysql_client_server_protocol.h
 */
static uint32_t
create_capabilities(MySQLProtocol *conn, bool db_specified, bool compress)
{
    uint32_t final_capabilities;

    /** Copy client's flags to backend but with the known capabilities mask */
    final_capabilities = (conn->client_capabilities & (uint32_t)GW_MYSQL_CAPABILITIES_CLIENT);

    if (conn->owner_dcb->server->server_ssl)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL;
        /* Unclear whether we should include this */
        /* Maybe it should depend on whether CA certificate is provided */
        /* final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT; */
    }

    /* Compression is not currently supported */
    if (compress)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_COMPRESS;
#ifdef DEBUG_MYSQL_CONN
        fprintf(stderr, ">>>> Backend Connection with compression\n");
#endif
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
 * @param conn  The MySQLProtocol structure for the connection
 * @param user  Name of the user seeking to connect
 * @param passwd Password for the user seeking to connect
 * @param dbname Name of the database to be made default, if any
 * @return The length of the response packet
 */
static int
response_length(MySQLProtocol *conn, char *user, uint8_t *passwd, char *dbname)
{
    long bytes;

    if (conn->owner_dcb->server->server_ssl && conn->owner_dcb->ssl_state != SSL_ESTABLISHED)
    {
        return 36;
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

    bytes += strlen("mysql_native_password");
    bytes++;

    // the packet header
    bytes += 4;

    return bytes;
}

static uint8_t *
load_hashed_password(MySQLProtocol *conn, uint8_t *payload, uint8_t *passwd)
{
    uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE]="";
    uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE]="";
    uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE]="";
    uint8_t client_scramble[GW_MYSQL_SCRAMBLE_SIZE];

    // hash1 is the function input, SHA1(real_password)
    memcpy(hash1, passwd, GW_MYSQL_SCRAMBLE_SIZE);

    // hash2 is the SHA1(input data), where input_data = SHA1(real_password)
    gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);

    // new_sha is the SHA1(CONCAT(scramble, hash2)
    gw_sha1_2_str(conn->scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, GW_MYSQL_SCRAMBLE_SIZE, new_sha);

    // compute the xor in client_scramble
    gw_str_xor(client_scramble, new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE);

    // set the auth-length
    *payload = GW_MYSQL_SCRAMBLE_SIZE;
    payload++;

    //copy the 20 bytes scramble data after packet_buffer + 36 + user + NULL + 1 (byte of auth-length)
    memcpy(payload, client_scramble, GW_MYSQL_SCRAMBLE_SIZE);

    payload += GW_MYSQL_SCRAMBLE_SIZE;
    return payload;
}

static void inline
close_socket(int sock)
{
    /*< Close newly created socket. */
    if (close(sock) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to close socket %d due %d, %s.",
            sock,
            errno,
            strerror_r(errno, errbuf, sizeof(errbuf)));
    }

}

/**
 * Create COM_CHANGE_USER packet and store it to GWBUF
 *
 * @param mses          MySQL session
 * @param protocol      protocol structure of the backend
 *
 * @return GWBUF buffer consisting of COM_CHANGE_USER packet
 *
 * @note the function doesn't fail
 */
static GWBUF *
gw_create_change_user_packet(MYSQL_session*  mses,
                                    MySQLProtocol*  protocol)
{
    char* db;
    char* user;
    uint8_t* pwd;
    GWBUF* buffer;
    int compress = 0;
    uint8_t* payload = NULL;
    uint8_t* payload_start = NULL;
    long bytes;
    char dbpass[MYSQL_USER_MAXLEN + 1]="";
    char* curr_db = NULL;
    uint8_t* curr_passwd = NULL;
    unsigned int charset;

    db = mses->db;
    user = mses->user;
    pwd = mses->client_sha1;

    if (strlen(db) > 0)
    {
        curr_db = db;
    }

    if (memcmp(pwd, null_client_sha1, MYSQL_SCRAMBLE_LEN))
    {
        curr_passwd = pwd;
    }

    /* get charset the client sent and use it for connection auth */
    charset = protocol->charset;

    if (compress)
    {
#ifdef DEBUG_MYSQL_CONN
        fprintf(stderr, ">>>> Backend Connection with compression\n");
#endif
    }

    /**
     * Protocol MySQL COM_CHANGE_USER for CLIENT_PROTOCOL_41
     * 1 byte COMMAND
     */
    bytes = 1;

    /** add the user and a terminating char */
    bytes += strlen(user);
    bytes++;
    /**
     * next will be + 1 (scramble_len) + 20 (fixed_scramble) +
     * (db + NULL term) + 2 bytes charset
     */
    if (curr_passwd != NULL)
    {
        bytes += GW_MYSQL_SCRAMBLE_SIZE;
    }
    /** 1 byte for scramble_len */
    bytes++;
    /** db name and terminating char */
    if (curr_db != NULL)
    {
        bytes += strlen(curr_db);
    }
    bytes++;

    /** the charset */
    bytes += 2;
    bytes += strlen("mysql_native_password");
    bytes++;

    /** the packet header */
    bytes += 4;

    buffer = gwbuf_alloc(bytes);
    /**
     * Set correct type to GWBUF so that it will be handled like session
     * commands
     */
    buffer->gwbuf_type = GWBUF_TYPE_MYSQL|GWBUF_TYPE_SINGLE_STMT|GWBUF_TYPE_SESCMD;
    payload = GWBUF_DATA(buffer);
    memset(payload, '\0', bytes);
    payload_start = payload;

    /** set packet number to 0 */
    payload[3] = 0x00;
    payload += 4;

    /** set the command COM_CHANGE_USER 0x11 */
    payload[0] = 0x11;
    payload++;
    memcpy(payload, user, strlen(user));
    payload += strlen(user);
    payload++;

    if (curr_passwd != NULL)
    {
        uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE]="";
        uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE]="";
        uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE]="";
        uint8_t client_scramble[GW_MYSQL_SCRAMBLE_SIZE];

        /** hash1 is the function input, SHA1(real_password) */
        memcpy(hash1, pwd, GW_MYSQL_SCRAMBLE_SIZE);

        /**
         * hash2 is the SHA1(input data), where
         * input_data = SHA1(real_password)
         */
        gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);

        /** dbpass is the HEX form of SHA1(SHA1(real_password)) */
        gw_bin2hex(dbpass, hash2, GW_MYSQL_SCRAMBLE_SIZE);

        /** new_sha is the SHA1(CONCAT(scramble, hash2) */
        gw_sha1_2_str(protocol->scramble,
                      GW_MYSQL_SCRAMBLE_SIZE,
                      hash2,
                      GW_MYSQL_SCRAMBLE_SIZE,
                      new_sha);

        /** compute the xor in client_scramble */
        gw_str_xor(client_scramble,
                   new_sha, hash1,
                   GW_MYSQL_SCRAMBLE_SIZE);

        /** set the auth-length */
        *payload = GW_MYSQL_SCRAMBLE_SIZE;
        payload++;
        /**
         * copy the 20 bytes scramble data after
         * packet_buffer + 36 + user + NULL + 1 (byte of auth-length)
         */
        memcpy(payload, client_scramble, GW_MYSQL_SCRAMBLE_SIZE);
        payload += GW_MYSQL_SCRAMBLE_SIZE;
    }
    else
    {
        /** skip the auth-length and leave the byte as NULL */
        payload++;
    }
    /** if the db is not NULL append it */
    if (curr_db != NULL)
    {
        memcpy(payload, curr_db, strlen(curr_db));
        payload += strlen(curr_db);
    }
    payload++;
    /** set the charset, 2 bytes */
    *payload = charset;
    payload++;
    *payload = '\x00';
    payload++;
    memcpy(payload, "mysql_native_password", strlen("mysql_native_password"));
    /* Following needed if more to be added */
    /* payload += strlen("mysql_native_password"); */
    /** put here the paylod size: bytes to write - 4 bytes packet header */
    gw_mysql_set_byte3(payload_start, (bytes-4));

    return buffer;
}

/**
 * Write a MySQL CHANGE_USER packet to backend server
 *
 * @param conn  MySQL protocol structure
 * @param dbname The selected database
 * @param user The selected user
 * @param passwd The SHA1(real_password)
 * @return 1 on success, 0 on failure
 */
static int
gw_send_change_user_to_backend(char          *dbname,
                                   char          *user,
                                   uint8_t       *passwd,
                                   MySQLProtocol *conn)
{
    GWBUF *buffer;
    int rc;
    MYSQL_session*  mses;

    mses = (MYSQL_session*)conn->owner_dcb->session->client_dcb->data;
    buffer = gw_create_change_user_packet(mses, conn);
    rc = conn->owner_dcb->func.write(conn->owner_dcb, buffer);

    if (rc != 0)
    {
        rc = 1;
    }
    return rc;
}
