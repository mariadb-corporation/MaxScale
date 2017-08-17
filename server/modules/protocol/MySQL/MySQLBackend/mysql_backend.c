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

#define MXS_MODULE_NAME "MySQLBackend"

#include <maxscale/protocol/mysql.h>
#include <maxscale/limits.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/utils.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/protocol.h>

/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 */

static int gw_create_backend_connection(DCB *backend, SERVER *server, MXS_SESSION *in_session);
static int gw_read_backend_event(DCB* dcb);
static int gw_write_backend_event(DCB *dcb);
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
static int gw_error_backend_event(DCB *dcb);
static int gw_backend_close(DCB *dcb);
static int gw_backend_hangup(DCB *dcb);
static int backend_write_delayqueue(DCB *dcb, GWBUF *buffer);
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue);
static int gw_change_user(DCB *backend_dcb, SERVER *server, MXS_SESSION *in_session, GWBUF *queue);
static char *gw_backend_default_auth();
static GWBUF* process_response_data(DCB* dcb, GWBUF** readbuf, int nbytes_to_process);
extern char* create_auth_failed_msg(GWBUF* readbuf, char* hostaddr, uint8_t* sha1);
static bool sescmd_response_complete(DCB* dcb);
static void gw_reply_on_error(DCB *dcb, mxs_auth_state_t state);
static int gw_read_and_write(DCB *dcb);
static int gw_do_connect_to_backend(char *host, int port, int *fd);
static void inline close_socket(int socket);
static GWBUF *gw_create_change_user_packet(MYSQL_session*  mses,
                                           MySQLProtocol*  protocol);
static int gw_send_change_user_to_backend(char          *dbname,
                                          char          *user,
                                          uint8_t       *passwd,
                                          MySQLProtocol *conn);
static void gw_send_proxy_protocol_header(DCB *backend_dcb);
static bool get_ip_string_and_port(struct sockaddr_storage *sa, char *ip, int iplen,
                                   in_port_t *port_out);
static bool gw_connection_established(DCB* dcb);

/*
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
        gw_read_backend_event,      /* Read - EPOLLIN handler        */
        gw_MySQLWrite_backend,      /* Write - data from gateway     */
        gw_write_backend_event,     /* WriteReady - EPOLLOUT handler */
        gw_error_backend_event,     /* Error - EPOLLERR handler      */
        gw_backend_hangup,          /* HangUp - EPOLLHUP handler     */
        NULL,                       /* Accept                        */
        gw_create_backend_connection, /* Connect                     */
        gw_backend_close,           /* Close                         */
        NULL,                       /* Listen                        */
        gw_change_user,             /* Authentication                */
        NULL,                       /* Session                       */
        gw_backend_default_auth,    /* Default authenticator         */
        NULL,                       /* Connection limit reached      */
        gw_connection_established
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_GA,
        MXS_PROTOCOL_VERSION,
        "The MySQL to backend server protocol",
        "V2.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
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
    return "MySQLBackendAuth";
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
                                        MXS_SESSION *session)
{
    MySQLProtocol *protocol = NULL;
    int rv = -1;
    int fd = -1;

    protocol = mysql_protocol_init(backend_dcb, -1);
    ss_dassert(protocol != NULL);

    if (protocol == NULL)
    {
        MXS_ERROR("Failed to create protocol object for backend connection.");
        goto return_fd;
    }

    /** Copy client flags to backend protocol */
    if (backend_dcb->session->client_dcb->protocol)
    {
        MySQLProtocol *client = (MySQLProtocol*)backend_dcb->session->client_dcb->protocol;
        protocol->client_capabilities = client->client_capabilities;
        protocol->charset = client->charset;
        protocol->extra_capabilities = client->extra_capabilities;
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
        protocol->protocol_auth_state = MXS_AUTH_STATE_CONNECTED;
        MXS_DEBUG("Established "
                  "connection to %s:%i, protocol fd %d client "
                  "fd %d.",
                  server->name,
                  server->port,
                  protocol->fd,
                  session->client_dcb->fd);

        if (server->proxy_protocol)
        {
            gw_send_proxy_protocol_header(backend_dcb);
        }
        break;

    case 1:
        /* The state MYSQL_PENDING_CONNECT is likely to be transitory,    */
        /* as it means the calls have been successful but the connection  */
        /* has not yet completed and the calls are non-blocking.          */
        ss_dassert(fd > 0);
        protocol->protocol_auth_state = MXS_AUTH_STATE_PENDING_CONNECT;
        protocol->fd = fd;
        MXS_DEBUG("Connection "
                  "pending to %s:%i, protocol fd %d client fd %d.",
                  server->name,
                  server->port,
                  protocol->fd,
                  session->client_dcb->fd);
        break;

    default:
        /* Failure - the state reverts to its initial value */
        ss_dassert(fd == -1);
        ss_dassert(protocol->protocol_auth_state == MXS_AUTH_STATE_INIT);
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
static int gw_do_connect_to_backend(char *host, int port, int *fd)
{
    struct sockaddr_storage serv_addr = {};
    int rv = -1;

    /* prepare for connect */
    int so = open_network_socket(MXS_SOCKET_NETWORK, &serv_addr, host, port);

    if (so == -1)
    {
        MXS_ERROR("Establishing connection to backend server [%s]:%d failed.", host, port);
        return rv;
    }

    rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    if (rv != 0)
    {
        if (errno == EINPROGRESS)
        {
            rv = 1;
        }
        else
        {
            MXS_ERROR("Failed to connect backend server [%s]:%d due to: %d, %s.",
                      host, port, errno, mxs_strerror(errno));
            close(so);
            return rv;
        }
    }

    *fd = so;
    MXS_DEBUG("Connected to backend server [%s]:%d, fd %d.", host, port, so);

    return rv;

}

/**
 * @brief Check if the response contain an error
 *
 * @param buffer Buffer with a complete response
 * @return True if the reponse contains an MySQL error packet
 */
bool is_error_response(GWBUF *buffer)
{
    uint8_t cmd;
    return gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &cmd) && cmd == MYSQL_REPLY_ERR;
}

/**
 * @brief Log handshake failure
 *
 * @param dcb Backend DCB where authentication failed
 * @param buffer Buffer containing the response from the backend
 */
void log_error_response(DCB *dcb, GWBUF *buffer)
{
    uint8_t *data = (uint8_t*)GWBUF_DATA(buffer);
    size_t len = MYSQL_GET_PAYLOAD_LEN(data);
    uint16_t errcode = MYSQL_GET_ERRCODE(data);
    char bufstr[len];
    memcpy(bufstr, data + 7, len - 3);
    bufstr[len - 3] = '\0';

    MXS_ERROR("Invalid authentication message from backend '%s'. Error code: %d, "
              "Msg : %s", dcb->server->unique_name, errcode, bufstr);

    /** If the error is ER_HOST_IS_BLOCKED put the server into maintenace mode.
     * This will prevent repeated authentication failures. */
    if (errcode == ER_HOST_IS_BLOCKED)
    {
        MXS_ERROR("Server %s has been put into maintenance mode due "
                  "to the server blocking connections from MaxScale. "
                  "Run 'mysqladmin -h %s -P %d flush-hosts' on this "
                  "server before taking this server out of maintenance "
                  "mode.", dcb->server->unique_name,
                  dcb->server->name, dcb->server->port);

        server_set_status(dcb->server, SERVER_MAINT);
    }
}

/**
 * @brief Handle the server's response packet
 *
 * This function reads the server's response packet and does the final step of
 * the authentication.
 *
 * @param dcb Backend DCB
 * @param buffer Buffer containing the server's complete handshake
 * @return MXS_AUTH_STATE_HANDSHAKE_FAILED on failure.
 */
mxs_auth_state_t handle_server_response(DCB *dcb, GWBUF *buffer)
{
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    mxs_auth_state_t rval = proto->protocol_auth_state == MXS_AUTH_STATE_CONNECTED ?
                            MXS_AUTH_STATE_HANDSHAKE_FAILED : MXS_AUTH_STATE_FAILED;

    if (dcb->authfunc.extract(dcb, buffer))
    {
        switch (dcb->authfunc.authenticate(dcb))
        {
        case MXS_AUTH_INCOMPLETE:
        case MXS_AUTH_SSL_INCOMPLETE:
            rval = MXS_AUTH_STATE_RESPONSE_SENT;
            break;

        case MXS_AUTH_SUCCEEDED:
            rval = MXS_AUTH_STATE_COMPLETE;

        default:
            break;
        }
    }

    gwbuf_free(buffer);
    return rval;
}

/**
 * @brief Prepare protocol for a write
 *
 * This prepares both the buffer and the protocol itself for writing a query
 * to the backend.
 *
 * @param dcb    The backend DCB to write to
 * @param buffer Buffer that will be written
 */
static inline void prepare_for_write(DCB *dcb, GWBUF *buffer)
{
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;

    /** Copy the current command being executed to this backend */
    if (dcb->session->client_dcb && dcb->session->client_dcb->protocol)
    {
        MySQLProtocol *client_proto = (MySQLProtocol*)dcb->session->client_dcb->protocol;
        proto->current_command = client_proto->current_command;
    }

    if (GWBUF_IS_TYPE_SESCMD(buffer))
    {
        mysql_server_cmd_t cmd = mxs_mysql_get_command(buffer);
        protocol_add_srv_command(proto, cmd);
    }
    if (GWBUF_SHOULD_COLLECT_RESULT(buffer))
    {
        proto->collect_result = true;
    }
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
    CHK_DCB(dcb);
    if (dcb->persistentstart)
    {
        /** If a DCB gets a read event when it's in the persistent pool, it is
         * treated as if it were an error. */
        dcb->dcb_errhandle_called = true;
        return 0;
    }

    if (dcb->dcb_is_zombie || dcb->session == NULL ||
        dcb->session->state == SESSION_STATE_DUMMY)
    {
        return 0;
    }

    CHK_SESSION(dcb->session);

    MySQLProtocol *proto = (MySQLProtocol *)dcb->protocol;
    CHK_PROTOCOL(proto);

    MXS_DEBUG("Read dcb %p fd %d protocol state %d, %s.", dcb, dcb->fd,
              proto->protocol_auth_state, STRPROTOCOLSTATE(proto->protocol_auth_state));

    int rc = 0;
    if (proto->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
    {
        rc = gw_read_and_write(dcb);
    }
    else
    {
        GWBUF *readbuf = NULL;

        if (!read_complete_packet(dcb, &readbuf))
        {
            proto->protocol_auth_state = MXS_AUTH_STATE_FAILED;
            gw_reply_on_error(dcb, proto->protocol_auth_state);
        }
        else if (readbuf)
        {
            /** We have a complete response from the server */
            /** TODO: add support for non-contiguous responses */
            readbuf = gwbuf_make_contiguous(readbuf);
            MXS_ABORT_IF_NULL(readbuf);

            if (is_error_response(readbuf))
            {
                /** The server responded with an error */
                proto->protocol_auth_state = MXS_AUTH_STATE_FAILED;
                log_error_response(dcb, readbuf);
            }

            if (proto->protocol_auth_state == MXS_AUTH_STATE_CONNECTED)
            {
                mxs_auth_state_t state = MXS_AUTH_STATE_FAILED;

                /** Read the server handshake and send the standard response */
                if (gw_read_backend_handshake(dcb, readbuf))
                {
                    state = gw_send_backend_auth(dcb);
                }

                proto->protocol_auth_state = state;
                gwbuf_free(readbuf);
            }
            else if (proto->protocol_auth_state == MXS_AUTH_STATE_RESPONSE_SENT)
            {
                /** Read the message from the server. This will be the first
                 * packet that can contain authenticator specific data from the
                 * backend server. For 'mysql_native_password' it'll be an OK
                 * packet */
                proto->protocol_auth_state = handle_server_response(dcb, readbuf);
            }

            if (proto->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
            {
                /** Authentication completed successfully */
                GWBUF *localq = dcb->delayq;
                dcb->delayq = NULL;

                if (localq)
                {
                    /** Send the queued commands to the backend */
                    prepare_for_write(dcb, localq);
                    rc = backend_write_delayqueue(dcb, localq);
                }
            }
            else if (proto->protocol_auth_state == MXS_AUTH_STATE_FAILED ||
                     proto->protocol_auth_state == MXS_AUTH_STATE_HANDSHAKE_FAILED)
            {
                /** Authentication failed */
                gw_reply_on_error(dcb, proto->protocol_auth_state);
            }
        }
        else if (proto->protocol_auth_state == MXS_AUTH_STATE_CONNECTED &&
                 dcb->ssl_state == SSL_ESTABLISHED)
        {
            proto->protocol_auth_state = gw_send_backend_auth(dcb);
        }
    }

    return rc;
}

static void do_handle_error(DCB *dcb, mxs_error_action_t action, const char *errmsg)
{
    bool succp = true;
    MXS_SESSION *session = dcb->session;

    if (!dcb->dcb_errhandle_called)
    {
        GWBUF *errbuf = mysql_create_custom_error(1, 0, errmsg);
        void *rsession = session->router_session;
        MXS_ROUTER_OBJECT *router = session->service->router;
        MXS_ROUTER *router_instance = session->service->router_instance;

        router->handleError(router_instance, rsession, errbuf,
                            dcb, action, &succp);

        gwbuf_free(errbuf);
        dcb->dcb_errhandle_called = true;
    }
    /**
     * If error handler fails it means that routing session can't continue
     * and it must be closed. In success, only this DCB is closed.
     */
    if (!succp)
    {
        session->state = SESSION_STATE_STOPPING;
    }
}

/**
 * @brief Authentication of backend - read the reply, or handle an error
 *
 * @param dcb               Descriptor control block for backend server
 * @param local_session     The current MySQL session data structure
 * @return
 */
static void gw_reply_on_error(DCB *dcb, mxs_auth_state_t state)
{
    MXS_SESSION *session = dcb->session;
    CHK_SESSION(session);

    if (!dcb->dcb_errhandle_called)
    {
        do_handle_error(dcb, ERRACT_REPLY_CLIENT,
                        "Authentication with backend failed. Session will be closed.");
        session->state = SESSION_STATE_STOPPING;
        dcb->dcb_errhandle_called = true;
    }
}

/**
 * @brief Check if a reply can be routed to the client
 *
 * @param Backend DCB
 * @return True if session is ready for reply routing
 */
static inline bool session_ok_to_route(DCB *dcb)
{
    bool rval = false;

    if (dcb->session->state == SESSION_STATE_ROUTER_READY &&
        dcb->session->client_dcb != NULL &&
        dcb->session->client_dcb->state == DCB_STATE_POLLING &&
        (dcb->session->router_session ||
         service_get_capabilities(dcb->session->service) & RCAP_TYPE_NO_RSESSION))
    {
        MySQLProtocol *client_protocol = (MySQLProtocol *)dcb->session->client_dcb->protocol;

        if (client_protocol)
        {
            CHK_PROTOCOL(client_protocol);

            if (client_protocol->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
            {
                rval = true;
            }
        }
        else if (dcb->session->client_dcb->dcb_role == DCB_ROLE_INTERNAL)
        {
            rval = true;
        }
    }

    return rval;
}

static inline bool expecting_resultset(MySQLProtocol *proto)
{
    return proto->current_command == MYSQL_COM_QUERY ||
           proto->current_command == MYSQL_COM_STMT_FETCH;
}

static inline bool expecting_ps_response(MySQLProtocol *proto)
{
    return proto->current_command == MYSQL_COM_STMT_PREPARE;
}

static inline bool complete_ps_response(GWBUF *buffer)
{
    ss_dassert(GWBUF_IS_CONTIGUOUS(buffer));
    MXS_PS_RESPONSE resp;
    bool rval = false;

    if (mxs_mysql_extract_ps_response(buffer, &resp))
    {
        int expected_eof = 0;

        if (resp.columns > 0)
        {
            expected_eof++;
        }

        if (resp.parameters > 0)
        {
            expected_eof++;
        }

        bool more;
        int n_eof = modutil_count_signal_packets(buffer, 0, &more);

        MXS_DEBUG("Expecting %u EOF, have %u", n_eof, expected_eof);

        rval = n_eof == expected_eof;
    }

    return rval;
}

static inline bool collecting_resultset(MySQLProtocol *proto, uint64_t capabilities)
{
    return rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT) ||
           proto->collect_result;
}

/**
 * @brief With authentication completed, read new data and write to backend
 *
 * @param dcb           Descriptor control block for backend server
 * @param local_session Current MySQL session data structure
 * @return 0 is fail, 1 is success
 */
static int
gw_read_and_write(DCB *dcb)
{
    GWBUF *read_buffer = NULL;
    MXS_SESSION *session = dcb->session;
    int nbytes_read;
    int return_code = 0;

    CHK_SESSION(session);

    /* read available backend data */
    return_code = dcb_read(dcb, &read_buffer, 0);

    if (return_code < 0)
    {
        do_handle_error(dcb, ERRACT_NEW_CONNECTION, "Read from backend failed");
        return 0;
    }

    nbytes_read = gwbuf_length(read_buffer);
    if (nbytes_read == 0)
    {
        ss_dassert(read_buffer == NULL);
        return return_code;
    }
    else
    {
        ss_dassert(read_buffer != NULL);
    }

    /** Ask what type of output the router/filter chain expects */
    uint64_t capabilities = service_get_capabilities(session->service);
    bool result_collected = false;

    if (rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT))
    {
        GWBUF *tmp = modutil_get_complete_packets(&read_buffer);
        /* Put any residue into the read queue */

        dcb->dcb_readqueue = read_buffer;

        if (tmp == NULL)
        {
            /** No complete packets */
            return 0;
        }

        read_buffer = tmp;

        MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;

        if (rcap_type_required(capabilities, RCAP_TYPE_CONTIGUOUS_OUTPUT) ||
            proto->collect_result)
        {
            if ((tmp = gwbuf_make_contiguous(read_buffer)))
            {
                read_buffer = tmp;
            }
            else
            {
                /** Failed to make the buffer contiguous */
                gwbuf_free(read_buffer);
                poll_fake_hangup_event(dcb);
                return 0;
            }

            if (collecting_resultset(proto, capabilities))
            {
                if (expecting_resultset(proto) &&
                    mxs_mysql_is_result_set(read_buffer))
                {
                    bool more = false;
                    if (modutil_count_signal_packets(read_buffer, 0, &more) != 2)
                    {
                        dcb->dcb_readqueue = gwbuf_append(read_buffer, dcb->dcb_readqueue);
                        return 0;
                    }

                    // Collected the complete result
                    proto->collect_result = false;
                    result_collected = true;
                }
                else if (expecting_ps_response(proto) &&
                         mxs_mysql_is_prep_stmt_ok(read_buffer))
                {
                    if (!complete_ps_response(read_buffer))
                    {
                        dcb->dcb_readqueue = gwbuf_append(read_buffer, dcb->dcb_readqueue);
                        return 0;
                    }

                    // Collected the complete result
                    proto->collect_result = false;
                    result_collected = true;
                }
            }
        }
    }

    MySQLProtocol *proto = (MySQLProtocol *)dcb->protocol;

    if (proto->ignore_replies > 0)
    {
        /**
         * The reply to an ignorable command is in the packet. Extract the
         * response type and discard the response.
         */
        uint8_t result = 0xff;
        gwbuf_copy_data(read_buffer, MYSQL_HEADER_LEN, 1, &result);
        proto->ignore_replies--;
        ss_dassert(proto->ignore_replies >= 0);
        gwbuf_free(read_buffer);

        int rval = 0;
        GWBUF *query = proto->stored_query;
        proto->stored_query = NULL;

        if (result == MYSQL_REPLY_OK)
        {
            rval = query ? dcb->func.write(dcb, query) : 1;
        }
        else if (query)
        {
            /**
             * The ignorable command failed when we had a queued query from the
             * client. Generate a fake hangup event to close the DCB and send
             * an error to the client.
             */
            gwbuf_free(query);
            poll_fake_hangup_event(dcb);
        }

        return rval;
    }

    do
    {
        GWBUF *stmt = NULL;
        /**
         * If protocol has session command set, concatenate whole
         * response into one buffer.
         */
        if (protocol_get_srv_command((MySQLProtocol *)dcb->protocol, true) != MYSQL_COM_UNDEFINED)
        {
            if (result_collected)
            {
                /** The result set or PS response was collected, we know it's complete */
                stmt = read_buffer;
                read_buffer = NULL;
                gwbuf_set_type(stmt, GWBUF_TYPE_RESPONSE_END | GWBUF_TYPE_SESCMD_RESPONSE);
            }
            else
            {
                stmt = process_response_data(dcb, &read_buffer, gwbuf_length(read_buffer));
                /**
                 * Received incomplete response to session command.
                 * Store it to readqueue and return.
                 */
                if (!sescmd_response_complete(dcb))
                {
                    stmt = gwbuf_append(stmt, read_buffer);
                    dcb->dcb_readqueue = gwbuf_append(stmt, dcb->dcb_readqueue);
                    return 0;
                }
            }
            if (!stmt)
            {
                MXS_ERROR("Read buffer unexpectedly null, even though response "
                          "not marked as complete. User: %s", dcb->session->client_dcb->user);
                return 0;
            }
        }
        else if (rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT) &&
                 !rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT) &&
                 !result_collected)
        {
            stmt = modutil_get_next_MySQL_packet(&read_buffer);
        }
        else
        {
            stmt = read_buffer;
            read_buffer = NULL;
        }

        if (session_ok_to_route(dcb))
        {
            session->service->router->clientReply(session->service->router_instance,
                                                  session->router_session,
                                                  stmt, dcb);
            return_code = 1;
        }
        else /*< session is closing; replying to client isn't possible */
        {
            gwbuf_free(stmt);
        }
    }
    while (read_buffer);

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
    int rc = 1;

    if (dcb->state != DCB_STATE_POLLING)
    {
        /** Don't write to backend if backend_dcb is not in poll set anymore */
        uint8_t* data = NULL;
        bool com_quit = false;

        if (dcb->writeq)
        {
            data = (uint8_t *) GWBUF_DATA(dcb->writeq);
            com_quit = MYSQL_IS_COM_QUIT(data);
        }

        if (data)
        {
            rc = 0;

            if (!com_quit)
            {
                mysql_send_custom_error(dcb->session->client_dcb, 1, 0,
                                        "Writing to backend failed due invalid Maxscale state.");
                MXS_ERROR("Attempt to write buffered data to backend "
                          "failed due internal inconsistent state: %s",
                          STRDCBSTATE(dcb->state));
            }
        }
        else
        {
            MXS_DEBUG("Dcb %p in state %s but there's nothing to write either.",
                      dcb, STRDCBSTATE(dcb->state));
        }
    }
    else
    {
        MySQLProtocol *backend_protocol = (MySQLProtocol*)dcb->protocol;

        if (backend_protocol->protocol_auth_state == MXS_AUTH_STATE_PENDING_CONNECT)
        {
            backend_protocol->protocol_auth_state = MXS_AUTH_STATE_CONNECTED;
            if (dcb->server->proxy_protocol)
            {
                gw_send_proxy_protocol_header(dcb);
            }
        }
        else
        {
            dcb_drain_writeq(dcb);
        }

        MXS_DEBUG("wrote to dcb %p fd %d, return %d", dcb, dcb->fd, rc);
    }

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

    if (dcb->was_persistent && dcb->state == DCB_STATE_POLLING &&
        backend_protocol->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
    {
        ss_dassert(dcb->persistentstart == 0);
        /**
         * This is a DCB that was just taken out of the persistent connection pool.
         * We need to sent a COM_CHANGE_USER query to the backend to reset the
         * session state.
         */
        if (backend_protocol->stored_query)
        {
            /** It is possible that the client DCB is closed before the COM_CHANGE_USER
             * response is received. */
            gwbuf_free(backend_protocol->stored_query);
        }
        dcb->was_persistent = false;
        backend_protocol->ignore_replies++;
        ss_dassert(backend_protocol->ignore_replies > 0);
        backend_protocol->stored_query = queue;

        GWBUF *buf = gw_create_change_user_packet(dcb->session->client_dcb->data, dcb->protocol);
        return dcb_write(dcb, buf) ? 1 : 0;
    }
    else if (backend_protocol->ignore_replies > 0)
    {
        if (MYSQL_IS_COM_QUIT((uint8_t*)GWBUF_DATA(queue)))
        {
            gwbuf_free(queue);
        }
        else
        {
            /**
             * We're still waiting on the reply to the COM_CHANGE_USER, append the
             * buffer to the stored query. This is possible if the client sends
             * BLOB data on the first command.
             */
            backend_protocol->stored_query = gwbuf_append(backend_protocol->stored_query, queue);
        }
        return 1;
    }

    /**
     * Pick action according to state of protocol.
     * If auth failed, return value is 0, write and buffered write
     * return 1.
     */
    switch (backend_protocol->protocol_auth_state)
    {
    case MXS_AUTH_STATE_HANDSHAKE_FAILED:
    case MXS_AUTH_STATE_FAILED:
        if (dcb->session->state != SESSION_STATE_STOPPING)
        {
            MXS_ERROR("Unable to write to backend '%s' due to "
                      "%s failure. Server in state %s.",
                      dcb->server->unique_name,
                      backend_protocol->protocol_auth_state == MXS_AUTH_STATE_HANDSHAKE_FAILED ?
                      "handshake" : "authentication",
                      STRSRVSTATUS(dcb->server));
        }

        gwbuf_free(queue);
        rc = 0;

        break;

    case MXS_AUTH_STATE_COMPLETE:
        {
            uint8_t* ptr = GWBUF_DATA(queue);
            mysql_server_cmd_t cmd = mxs_mysql_get_command(queue);

            MXS_DEBUG("write to dcb %p fd %d protocol state %s.",
                      dcb, dcb->fd, STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));

            prepare_for_write(dcb, queue);

            if (cmd == MYSQL_COM_QUIT && dcb->server->persistpoolmax)
            {
                /** We need to keep the pooled connections alive so we just ignore the COM_QUIT packet */
                gwbuf_free(queue);
                rc = 1;
            }
            else
            {
                if (GWBUF_IS_IGNORABLE(queue))
                {
                    /** The response to this command should be ignored */
                    backend_protocol->ignore_replies++;
                    ss_dassert(backend_protocol->ignore_replies > 0);
                }

                /** Write to backend */
                rc = dcb_write(dcb, queue);
            }
        }
        break;

    default:
        {
            MXS_DEBUG("delayed write to dcb %p fd %d protocol state %s.",
                      dcb, dcb->fd, STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));

            /** Store data until authentication is complete */
            prepare_for_write(dcb, queue);
            backend_set_delayqueue(dcb, queue);
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
    CHK_DCB(dcb);
    MXS_SESSION *session = dcb->session;
    CHK_SESSION(session);

    if (session->state == SESSION_STATE_DUMMY)
    {
        if (dcb->persistentstart == 0)
        {
            /** Not a persistent connection, something is wrong. */
            MXS_ERROR("EPOLLERR event on a non-persistent DCB with no session. "
                      "Closing connection.");
        }
        dcb_close(dcb);
    }
    else if (dcb->state != DCB_STATE_POLLING || session->state != SESSION_STATE_ROUTER_READY)
    {
        int error;
        int len = sizeof(error);

        if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len) == 0 && error != 0)
        {
            if (dcb->state != DCB_STATE_POLLING)
            {
                MXS_ERROR("DCB in state %s got error '%s'.", STRDCBSTATE(dcb->state),
                          mxs_strerror(errno));
            }
            else
            {
                MXS_ERROR("Error '%s' in session that is not ready for routing.",
                          mxs_strerror(errno));
            }
        }
    }
    else
    {
        do_handle_error(dcb, ERRACT_NEW_CONNECTION, "Lost connection to backend server.");
    }

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
    CHK_DCB(dcb);
    MXS_SESSION *session = dcb->session;
    CHK_SESSION(session);

    if (dcb->persistentstart)
    {
        dcb->dcb_errhandle_called = true;
    }
    else if (session->state != SESSION_STATE_ROUTER_READY)
    {
        int error;
        int len = sizeof(error);
        if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len) == 0)
        {
            if (error != 0 && session->state != SESSION_STATE_STOPPING)
            {
                MXS_ERROR("Hangup in session that is not ready for routing, "
                          "Error reported is '%s'.",
                          mxs_strerror(errno));
            }
        }
    }
    else
    {
        do_handle_error(dcb, ERRACT_NEW_CONNECTION, "Lost connection to backend server.");
    }

    return 1;
}

/**
 * Send COM_QUIT to backend so that it can be closed.
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int gw_backend_close(DCB *dcb)
{
    CHK_DCB(dcb);
    ss_dassert(dcb->session);

    /** Send COM_QUIT to the backend being closed */
    GWBUF* quitbuf = mysql_create_com_quit(NULL, 0);
    mysql_send_com_quit(dcb, 0, quitbuf);

    /** Free protocol data */
    mysql_protocol_done(dcb);

    MXS_SESSION* session = dcb->session;
    CHK_SESSION(session);

    /**
     * If session state is SESSION_STATE_STOPPING, start closing client session.
     * Otherwise only this backend connection is closed.
     */
    if (session->client_dcb &&
        session->state == SESSION_STATE_STOPPING &&
        session->client_dcb->state == DCB_STATE_POLLING)
    {
        dcb_close(session->client_dcb);
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
static int backend_write_delayqueue(DCB *dcb, GWBUF *buffer)
{
    ss_dassert(buffer);

    if (MYSQL_IS_CHANGE_USER(((uint8_t *)GWBUF_DATA(buffer))))
    {
        /** Recreate the COM_CHANGE_USER packet with the scramble the backend sent to us */
        MYSQL_session mses;
        gw_get_shared_session_auth_info(dcb, &mses);
        gwbuf_free(buffer);
        buffer = gw_create_change_user_packet(&mses, dcb->protocol);
    }

    int rc = 1;

    if (MYSQL_IS_COM_QUIT(((uint8_t*)GWBUF_DATA(buffer))) && dcb->server->persistpoolmax)
    {
        /** We need to keep the pooled connections alive so we just ignore the COM_QUIT packet */
        gwbuf_free(buffer);
        rc = 1;
    }
    else
    {
        rc = dcb_write(dcb, buffer);
    }

    if (rc == 0)
    {
        do_handle_error(dcb, ERRACT_NEW_CONNECTION, "Lost connection to backend server.");
    }

    return rc;
}

/**
 * This routine handles the COM_CHANGE_USER command
 *
 * TODO: Move this into the authenticators
 *
 * @param dcb           The current backend DCB
 * @param server        The backend server pointer
 * @param in_session    The current session data (MYSQL_session)
 * @param queue         The GWBUF containing the COM_CHANGE_USER receveid
 * @return 1 on success and 0 on failure
 */
static int gw_change_user(DCB *backend,
                          SERVER *server,
                          MXS_SESSION *in_session,
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
    size_t len = strlen((char *)client_auth_packet);
    if (len > MYSQL_USER_MAXLEN)
    {
        MXS_ERROR("Client sent user name \"%s\",which is %lu characters long, "
                  "while a maximum length of %d is allowed. Cutting trailing "
                  "characters.", (char*)client_auth_packet, len, MYSQL_USER_MAXLEN);
    }
    strncpy(username, (char *)client_auth_packet, MYSQL_USER_MAXLEN);
    username[MYSQL_USER_MAXLEN] = 0;

    client_auth_packet += (len + 1);

    /* get the auth token len */
    memcpy(&auth_token_len, client_auth_packet, 1);

    client_auth_packet++;

    /* allocate memory for token only if auth_token_len > 0 */
    if (auth_token_len > 0)
    {
        auth_token = (uint8_t *)MXS_MALLOC(auth_token_len);
        ss_dassert(auth_token != NULL);

        if (auth_token == NULL)
        {
            return rv;
        }
        memcpy(auth_token, client_auth_packet, auth_token_len);
        client_auth_packet += auth_token_len;
    }

    /* get new database name */
    len = strlen((char *)client_auth_packet);
    if (len > MYSQL_DATABASE_MAXLEN)
    {
        MXS_ERROR("Client sent database name \"%s\", which is %lu characters long, "
                  "while a maximum length of %d is allowed. Cutting trailing "
                  "characters.", (char*)client_auth_packet, len, MYSQL_DATABASE_MAXLEN);
    }
    strncpy(database, (char *)client_auth_packet, MYSQL_DATABASE_MAXLEN);
    database[MYSQL_DATABASE_MAXLEN] = 0;

    client_auth_packet += (len + 1);

    if (*client_auth_packet)
    {
        memcpy(&backend_protocol->charset, client_auth_packet, sizeof(int));
    }

    /* save current_database name */
    strcpy(current_database, current_session->db);

    /*
     * Now clear database name in dcb as we don't do local authentication on db name for change user.
     * Local authentication only for user@host and if successful the database name change is sent to backend.
     */
    *current_session->db = 0;

    /*
     * Decode the token and check the password.
     * Note: if auth_token_len == 0 && auth_token == NULL, user is without password
     */
    DCB *dcb = backend->session->client_dcb;

    if (dcb->authfunc.reauthenticate == NULL)
    {
        /** Authenticator does not support reauthentication */
        rv = 0;
        goto retblock;
    }

    auth_ret = dcb->authfunc.reauthenticate(dcb, username,
                                            auth_token, auth_token_len,
                                            client_protocol->scramble,
                                            sizeof(client_protocol->scramble),
                                            client_sha1, sizeof(client_sha1));

    strcpy(current_session->db, current_database);

    if (auth_ret != 0)
    {
        if (service_refresh_users(backend->session->client_dcb->service) == 0)
        {
            /* Try authentication again with new repository data */
            /* Note: if no auth client authentication will fail */
            *current_session->db = 0;

            auth_ret = dcb->authfunc.reauthenticate(dcb, username,
                                                    auth_token, auth_token_len,
                                                    client_protocol->scramble,
                                                    sizeof(client_protocol->scramble),
                                                    client_sha1, sizeof(client_sha1));

            strcpy(current_session->db, current_database);
        }
    }

    MXS_FREE(auth_token);

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
                                       false,
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
    size_t nbytes_left = 0; /*< nbytes to be read for the packet */
    MySQLProtocol* p;
    GWBUF* outbuf = NULL;
    int initial_packets = npackets_left;
    size_t initial_bytes = nbytes_left;

    /** Get command which was stored in gw_MySQLWrite_backend */
    p = DCB_PROTOCOL(dcb, MySQLProtocol);
    CHK_PROTOCOL(p);

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

        MXS_DEBUG("Read command %s for DCB %p fd %d.", STRPACKETTYPE(srvcmd), dcb, dcb->fd);
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

                /** Ignore the rest of the response */
                nbytes_to_process = 0;
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
                    MXS_DEBUG("[%s] Read %d packets. Waiting for %d more "
                              "packets for a total of %d packets.", __FUNCTION__,
                              initial_packets - npackets_left,
                              npackets_left, initial_packets);

                    /** Store the already read data into the readqueue of the DCB
                     * and restore the response status to the initial number of packets */

                    dcb->dcb_readqueue = gwbuf_append(outbuf, dcb->dcb_readqueue);

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
    size_t nbytes_left;
    MySQLProtocol* p;
    bool succp;

    p = DCB_PROTOCOL(dcb, MySQLProtocol);
    CHK_PROTOCOL(p);

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
    uint8_t* payload = NULL;
    uint8_t* payload_start = NULL;
    long bytes;
    char dbpass[MYSQL_USER_MAXLEN + 1] = "";
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
    buffer->gwbuf_type = GWBUF_TYPE_SESCMD;
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
        uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE] = "";
        uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE] = "";
        uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE] = "";
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
    gw_mysql_set_byte3(payload_start, (bytes - 4));

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

/* Send proxy protocol header. See
 * http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 * for more information. Currently only supports the text version (v1) of
 * the protocol. Binary version may be added when the feature has been confirmed
 * to work.
 *
 * @param backend_dcb The target dcb.
 */
static void gw_send_proxy_protocol_header(DCB *backend_dcb)
{
    // TODO: Add support for chained proxies. Requires reading the client header.

    const DCB *client_dcb = backend_dcb->session->client_dcb;
    const int client_fd = client_dcb->fd;
    const sa_family_t family = client_dcb->ip.ss_family;
    const char *family_str = NULL;

    struct sockaddr_storage sa_peer;
    struct sockaddr_storage sa_local;
    socklen_t sa_peer_len = sizeof(sa_peer);
    socklen_t sa_local_len = sizeof(sa_local);

    /* Fill in peer's socket address.  */
    if (getpeername(client_fd, (struct sockaddr *)&sa_peer, &sa_peer_len) == -1)
    {
        MXS_ERROR("'%s' failed on file descriptor '%d'.", "getpeername()", client_fd);
        return;
    }

    /* Fill in this socket's local address. */
    if (getsockname(client_fd, (struct sockaddr *)&sa_local, &sa_local_len) == -1)
    {
        MXS_ERROR("'%s' failed on file descriptor '%d'.", "getsockname()", client_fd);
        return;
    }
    ss_dassert(sa_peer.ss_family == sa_local.ss_family);

    char peer_ip[INET6_ADDRSTRLEN];
    char maxscale_ip[INET6_ADDRSTRLEN];
    in_port_t peer_port;
    in_port_t maxscale_port;

    if (!get_ip_string_and_port(&sa_peer, peer_ip, sizeof(peer_ip), &peer_port) ||
        !get_ip_string_and_port(&sa_local, maxscale_ip, sizeof(maxscale_ip), &maxscale_port))
    {
        MXS_ERROR("Could not convert network address to string form.");
        return;
    }

    switch (family)
    {
    case AF_INET:
        family_str = "TCP4";
        break;

    case AF_INET6:
        family_str = "TCP6";
        break;

    default:
        family_str = "UNKNOWN";
        break;
    }

    int rval;
    char proxy_header[108]; // 108 is the worst-case length
    if (family == AF_INET || family == AF_INET6)
    {
        rval = snprintf(proxy_header, sizeof(proxy_header), "PROXY %s %s %s %d %d\r\n",
                        family_str, peer_ip, maxscale_ip, peer_port, maxscale_port);
    }
    else
    {
        rval = snprintf(proxy_header, sizeof(proxy_header), "PROXY %s\r\n", family_str);
    }
    if (rval < 0 || rval >= sizeof(proxy_header))
    {
        MXS_ERROR("Proxy header printing error, produced '%s'.", proxy_header);
        return;
    }

    GWBUF *headerbuf = gwbuf_alloc_and_load(strlen(proxy_header), proxy_header);
    if (headerbuf)
    {
        MXS_INFO("Sending proxy-protocol header '%s' to backend %s.", proxy_header,
                 backend_dcb->server->unique_name);
        if (!dcb_write(backend_dcb, headerbuf))
        {
            gwbuf_free(headerbuf);
        }
    }
    return;
}

/* Read IP and port from socket address structure, return IP as string and port
 * as host byte order integer.
 *
 * @param sa A sockaddr_storage containing either an IPv4 or v6 address
 * @param ip Pointer to output array
 * @param iplen Output array length
 * @param port_out Port number output
 */
static bool get_ip_string_and_port(struct sockaddr_storage *sa,
                                   char *ip, int iplen, in_port_t *port_out)
{
    bool success = false;
    in_port_t port;

    switch (sa->ss_family)
    {
    case AF_INET:
        {
            struct sockaddr_in *sock_info = (struct sockaddr_in *)sa;
            struct in_addr *addr = &(sock_info->sin_addr);
            success = (inet_ntop(AF_INET, addr, ip, iplen) != NULL);
            port = ntohs(sock_info->sin_port);
        }
        break;

    case AF_INET6:
        {
            struct sockaddr_in6 *sock_info = (struct sockaddr_in6 *)sa;
            struct in6_addr *addr = &(sock_info->sin6_addr);
            success = (inet_ntop(AF_INET6, addr, ip, iplen) != NULL);
            port = ntohs(sock_info->sin6_port);
        }
        break;
    }
    if (success)
    {
        *port_out = port;
    }
    return success;
}

static bool gw_connection_established(DCB* dcb)
{
    MySQLProtocol *proto = (MySQLProtocol*)dcb->protocol;
    return proto->protocol_auth_state == MXS_AUTH_STATE_COMPLETE;
}
