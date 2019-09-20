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

#define MXS_MODULE_NAME "mariadbbackend"

#include <maxscale/protocol/mariadb/protocol_classes.hh>

#include <mysql.h>
#include <mysqld_error.h>
#include <maxbase/alloc.h>
#include <maxsql/mariadb.hh>
#include <maxscale/authenticator2.hh>
#include <maxscale/clock.h>
#include <maxscale/limits.h>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/utils.h>
#include <maxscale/protocol/mariadb/mysql.hh>
// For setting server status through monitor
#include "../../../core/internal/monitormanager.hh"

using mxs::ReplyState;

static bool get_ip_string_and_port(struct sockaddr_storage* sa,
                                   char* ip,
                                   int iplen,
                                   in_port_t* port_out);

MySQLBackendProtocol::MySQLBackendProtocol(MXS_SESSION* session, SERVER* server,
                                           mxs::Component* component,
                                           std::unique_ptr<mxs::BackendAuthenticator> authenticator)
    : MySQLProtocol(session, server)
    , m_component(component)
    , m_authenticator(std::move(authenticator))
{
}

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

std::unique_ptr<MySQLBackendProtocol>
MySQLBackendProtocol::create(MXS_SESSION* session, SERVER* server,
                             MySQLClientProtocol& client_protocol, mxs::Component* component,
                             std::unique_ptr<mxs::BackendAuthenticator> authenticator)
{
    std::unique_ptr<MySQLBackendProtocol> protocol_session(
            new(std::nothrow) MySQLBackendProtocol(session, server, component, std::move(authenticator)));
    if (protocol_session)
    {
        protocol_session->set_client_data(client_protocol);
        protocol_session->protocol_auth_state = MXS_AUTH_STATE_CONNECTED;
    }
    return protocol_session;
}

std::unique_ptr<MySQLBackendProtocol>
MySQLBackendProtocol::create_test_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component,
                                           std::unique_ptr<mxs::BackendAuthenticator> authenticator)
{
    std::unique_ptr<MySQLBackendProtocol> protocol_session(
            new(std::nothrow) MySQLBackendProtocol(session, server, component, std::move(authenticator)));
    return protocol_session;
}

bool MySQLBackendProtocol::init_connection(DCB* dcb)
{
    mxb_assert(dcb->role() == DCB::Role::BACKEND);
    BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);
    if (backend_dcb->server()->proxy_protocol)
    {
        // TODO: The following function needs a return value.
        gw_send_proxy_protocol_header(backend_dcb);
    }

    return true;
}

void MySQLBackendProtocol::finish_connection(DCB* dcb)
{
    mxb_assert(dcb->ready());
    /** Send COM_QUIT to the backend being closed */
    dcb->writeq_append(mysql_create_com_quit(nullptr, 0));
}

bool MySQLBackendProtocol::reuse_connection(BackendDCB* dcb, mxs::Component* upstream,
                                            mxs::ClientProtocol* client_protocol)
{
    bool rv = false;

    mxb_assert(!dcb->readq() && !dcb->delayq() && !dcb->writeq());
    mxb_assert(!dcb->is_in_persistent_pool());
    mxb_assert(m_ignore_replies >= 0);

    if (dcb->state() != DCB::State::POLLING || this->protocol_auth_state != MXS_AUTH_STATE_COMPLETE)
    {
        MXS_INFO("DCB and protocol state do not qualify for pooling: %s, %s",
                 mxs::to_string(dcb->state()), mxs::to_string(this->protocol_auth_state));
    }
    else
    {
        auto mysql_client = static_cast<MySQLClientProtocol*>(client_protocol);
        set_client_data(*mysql_client);
        MXS_SESSION* session = m_session;
        mxs::Component* component = m_component;

        m_session = dcb->session();
        m_component = upstream;

        m_ignore_replies = 0;

        /**
         * This is a DCB that was just taken out of the persistent connection pool.
         * We need to sent a COM_CHANGE_USER query to the backend to reset the
         * session state.
         */
        if (this->stored_query)
        {
            /** It is possible that the client DCB is closed before the COM_CHANGE_USER
             * response is received. */
            gwbuf_free(this->stored_query);
            this->stored_query = nullptr;
        }

        GWBUF* buf = gw_create_change_user_packet(m_client_data);
        if (dcb->writeq_append(buf))
        {
            MXS_INFO("Sent COM_CHANGE_USER");
            m_ignore_replies++;
            rv = true;
        }

        if (!rv)
        {
            // Restore situation
            m_session = session;
            m_component = component;
        }
    }

    return rv;
}

/**
 * @brief Check if the response contain an error
 *
 * @param buffer Buffer with a complete response
 * @return True if the reponse contains an MySQL error packet
 */
bool is_error_response(GWBUF* buffer)
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
void MySQLBackendProtocol::handle_error_response(DCB* plain_dcb, GWBUF* buffer)
{
    mxb_assert(plain_dcb->role() == DCB::Role::BACKEND);
    BackendDCB* dcb = static_cast<BackendDCB*>(plain_dcb);
    uint8_t* data = (uint8_t*)GWBUF_DATA(buffer);
    size_t len = MYSQL_GET_PAYLOAD_LEN(data);
    uint16_t errcode = MYSQL_GET_ERRCODE(data);
    char bufstr[len];
    memcpy(bufstr, data + 7, len - 3);
    bufstr[len - 3] = '\0';

    MXS_ERROR("Invalid authentication message from backend '%s'. Error code: %d, "
              "Msg : %s",
              dcb->server()->name(),
              errcode,
              bufstr);

    /** If the error is ER_HOST_IS_BLOCKED put the server into maintenance mode.
     * This will prevent repeated authentication failures. */
    if (errcode == ER_HOST_IS_BLOCKED)
    {
        auto main_worker = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
        auto target_server = dcb->server();
        main_worker->execute([target_server]() {
                                 MonitorManager::set_server_status(target_server, SERVER_MAINT);
                             }, mxb::Worker::EXECUTE_AUTO);

        MXS_ERROR("Server %s has been put into maintenance mode due to the server blocking connections "
                  "from MaxScale. Run 'mysqladmin -h %s -P %d flush-hosts' on this server before taking "
                  "this server out of maintenance mode. To avoid this problem in the future, set "
                  "'max_connect_errors' to a larger value in the backend server.",
                  dcb->server()->name(),
                  dcb->server()->address,
                  dcb->server()->port);
    }
    else if (errcode == ER_ACCESS_DENIED_ERROR
             || errcode == ER_DBACCESS_DENIED_ERROR
             || errcode == ER_ACCESS_DENIED_NO_PASSWORD_ERROR)
    {
        // Authentication failed, reload users
        service_refresh_users(dcb->service());
    }
}

/**
 * @brief Handle the server's response packet
 *
 * This function reads the server's response packet and does the final step of
 * the authentication.
 *
 * @param generic_dcb Backend DCB
 * @param buffer Buffer containing the server's complete handshake
 * @return MXS_AUTH_STATE_HANDSHAKE_FAILED on failure.
 */
mxs_auth_state_t MySQLBackendProtocol::handle_server_response(DCB* generic_dcb, GWBUF* buffer)
{
    auto dcb = static_cast<BackendDCB*>(generic_dcb);
    mxs_auth_state_t rval = protocol_auth_state == MXS_AUTH_STATE_CONNECTED ?
        MXS_AUTH_STATE_HANDSHAKE_FAILED : MXS_AUTH_STATE_FAILED;

    if (m_authenticator->extract(dcb, buffer))
    {
        switch (m_authenticator->authenticate(dcb))
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
void MySQLBackendProtocol::prepare_for_write(DCB* dcb, GWBUF* buffer)
{
    mxb_assert(dcb->session());

    track_query(buffer);
    if (GWBUF_SHOULD_COLLECT_RESULT(buffer))
    {
        m_collect_result = true;
    }
    m_track_state = GWBUF_SHOULD_TRACK_STATE(buffer);
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
void MySQLBackendProtocol::ready_for_reading(DCB* plain_dcb)
{
    mxb_assert(plain_dcb->role() == DCB::Role::BACKEND);
    BackendDCB* dcb = static_cast<BackendDCB*>(plain_dcb);

    if (dcb->is_in_persistent_pool())
    {
        /** If a DCB gets a read event when it's in the persistent pool, it is
         * treated as if it were an error. */
        dcb->trigger_hangup_event();
        return;
    }

    mxb_assert(dcb->session());

    auto proto = this;

    MXS_DEBUG("Read dcb %p fd %d protocol state %d, %s.",
              dcb,
              dcb->fd(),
              proto->protocol_auth_state,
              mxs::to_string(proto->protocol_auth_state));

    if (proto->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
    {
        gw_read_and_write(dcb);
    }
    else
    {
        GWBUF* readbuf = NULL;

        if (!read_complete_packet(dcb, &readbuf))
        {
            proto->protocol_auth_state = MXS_AUTH_STATE_FAILED;
            gw_reply_on_error(dcb);
        }
        else if (readbuf)
        {
            /*
            ** We have a complete response from the server
            ** TODO: add support for non-contiguous responses
            */
            readbuf = gwbuf_make_contiguous(readbuf);
            MXS_ABORT_IF_NULL(readbuf);

            if (is_error_response(readbuf))
            {
                /** The server responded with an error */
                proto->protocol_auth_state = MXS_AUTH_STATE_FAILED;
                handle_error_response(dcb, readbuf);
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
                GWBUF* localq = dcb->delayq_release();

                if (localq)
                {
                    localq = gwbuf_make_contiguous(localq);
                    /** Send the queued commands to the backend */
                    prepare_for_write(dcb, localq);
                    backend_write_delayqueue(dcb, localq);
                }
            }
            else if (proto->protocol_auth_state == MXS_AUTH_STATE_FAILED
                     || proto->protocol_auth_state == MXS_AUTH_STATE_HANDSHAKE_FAILED)
            {
                /** Authentication failed */
                gw_reply_on_error(dcb);
            }

            gwbuf_free(readbuf);
        }
        else if (proto->protocol_auth_state == MXS_AUTH_STATE_CONNECTED
                 && dcb->ssl_state() == DCB::SSLState::ESTABLISHED)
        {
            proto->protocol_auth_state = gw_send_backend_auth(dcb);
        }
    }

    return;
}

void MySQLBackendProtocol::do_handle_error(DCB* dcb, const char* errmsg)
{
    mxb_assert(!dcb->hanged_up());
    GWBUF* errbuf = mysql_create_custom_error(1, 0, errmsg);

    if (!m_component->handleError(errbuf, nullptr, m_reply))
    {
        // A failure to handle an error means that the session must be closed
        MXS_SESSION* session = dcb->session();
        session->close_reason = SESSION_CLOSE_HANDLEERROR_FAILED;
        session->terminate();
    }

    gwbuf_free(errbuf);
}

/**
 * Authentication of backend - read the reply, or handle an error
 *
 * @param dcb               Descriptor control block for backend server
 */
void MySQLBackendProtocol::gw_reply_on_error(DCB* dcb)
{
    auto err = mysql_create_custom_error(1, 0, "Authentication with backend failed. Session will be closed.");
    dcb->session()->terminate(err);
}

/**
 * @brief Check if a reply can be routed to the client
 *
 * @param Backend DCB
 * @return True if session is ready for reply routing
 */
bool MySQLBackendProtocol::session_ok_to_route(DCB* dcb)
{
    bool rval = false;
    auto session = dcb->session();
    if (session->state() == MXS_SESSION::State::STARTED)
    {
        ClientDCB* client_dcb = session->client_dcb;
        if (client_dcb && client_dcb->state() == DCB::State::POLLING)
        {
            auto client_protocol = static_cast<MySQLClientProtocol*>(client_dcb->protocol());
            if (client_protocol)
            {
                if (client_protocol->protocol_auth_state == MXS_AUTH_STATE_COMPLETE)
                {
                    rval = true;
                }
            }
            else if (dcb->session()->client_dcb->role() == DCB::Role::INTERNAL)
            {
                rval = true;
            }
        }
    }


    return rval;
}

bool MySQLBackendProtocol::expecting_text_result()
{
    /**
     * The addition of COM_STMT_FETCH to the list of commands that produce
     * result sets is slightly wrong. The command can generate complete
     * result sets but it can also generate incomplete ones if cursors
     * are used. The use of cursors most likely needs to be detected on
     * an upper level and the use of this function avoided in those cases.
     *
     * TODO: Revisit this to make sure it's needed.
     */

    uint8_t cmd = reply().command();
    return cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_FETCH;
}

bool MySQLBackendProtocol::expecting_ps_response()
{
    return reply().command() == MXS_COM_STMT_PREPARE;
}

bool MySQLBackendProtocol::complete_ps_response(GWBUF* buffer)
{
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

/**
 * Helpers for checking OK and ERR packets specific to COM_CHANGE_USER
 */
static inline bool not_ok_packet(const GWBUF* buffer)
{
    const uint8_t* data = GWBUF_DATA(buffer);

    return data[4] != MYSQL_REPLY_OK
           ||   // Should be more than 7 bytes of payload
           gw_mysql_get_byte3(data) < MYSQL_OK_PACKET_MIN_LEN - MYSQL_HEADER_LEN
           ||   // Should have no affected rows
           data[5] != 0
           ||   // Should not generate an insert ID
           data[6] != 0;
}

static inline bool not_err_packet(const GWBUF* buffer)
{
    return GWBUF_DATA(buffer)[4] != MYSQL_REPLY_ERR;
}

static inline bool auth_change_requested(GWBUF* buf)
{
    return mxs_mysql_get_command(buf) == MYSQL_REPLY_AUTHSWITCHREQUEST
           && gwbuf_length(buf) > MYSQL_EOF_PACKET_LEN;
}

bool MySQLBackendProtocol::handle_auth_change_response(GWBUF* reply, DCB* dcb)
{
    bool rval = false;

    if (strcmp((char*)GWBUF_DATA(reply) + 5, DEFAULT_MYSQL_AUTH_PLUGIN) == 0)
    {
        /**
         * The server requested a change of authentication methods.
         * If we're changing the authentication method to the same one we
         * are using now, it means that the server is simply generating
         * a new scramble for the re-authentication process.
         */

        // Load the new scramble into the protocol...
        gwbuf_copy_data(reply,
                        5 + strlen(DEFAULT_MYSQL_AUTH_PLUGIN) + 1,
                        GW_MYSQL_SCRAMBLE_SIZE,
                        scramble);

        /// ... and use it to send the encrypted password to the server
        rval = send_mysql_native_password_response(dcb);
    }

    return rval;
}

/**
 * With authentication completed, read new data and write to backend
 *
 * @param dcb           Descriptor control block for backend server
 * @return 0 is fail, 1 is success
 */
int MySQLBackendProtocol::gw_read_and_write(DCB* dcb)
{
    GWBUF* read_buffer = NULL;
    MXS_SESSION* session = dcb->session();
    int nbytes_read = 0;
    int return_code = 0;

    /* read available backend data */
    return_code = dcb->read(&read_buffer, 0);

    if (return_code < 0)
    {
        do_handle_error(dcb, "Read from backend failed");
        return 0;
    }

    if (read_buffer)
    {
        nbytes_read = gwbuf_length(read_buffer);
    }

    if (nbytes_read == 0)
    {
        mxb_assert(read_buffer == NULL);
        return return_code;
    }
    else
    {
        mxb_assert(read_buffer != NULL);
    }

    /** Ask what type of output the router/filter chain expects */
    uint64_t capabilities = service_get_capabilities(session->service);
    bool result_collected = false;
    auto proto = this;

    if (rcap_type_required(capabilities, RCAP_TYPE_PACKET_OUTPUT)
        || rcap_type_required(capabilities, RCAP_TYPE_CONTIGUOUS_OUTPUT)
        || proto->m_collect_result
        || proto->m_ignore_replies != 0)
    {
        GWBUF* tmp;

        if (rcap_type_required(capabilities, RCAP_TYPE_REQUEST_TRACKING)
            && !rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT)
            && !proto->m_ignore_replies)
        {
            tmp = proto->track_response(&read_buffer);
        }
        else
        {
            tmp = modutil_get_complete_packets(&read_buffer);
        }

        // Store any partial packets in the DCB's read buffer
        if (read_buffer)
        {
            dcb->readq_set(read_buffer);

            if (proto->reply().is_complete())
            {
                // There must be more than one response in the buffer which we need to process once we've
                // routed this response.
                dcb->trigger_read_event();
            }
        }

        if (tmp == NULL)
        {
            /** No complete packets */
            return 0;
        }

        /** Get sesion track info from ok packet and save it to gwbuf properties.
         *
         * The OK packets sent in response to COM_STMT_PREPARE are of a different
         * format so we need to detect and skip them. */
        if (rcap_type_required(capabilities, RCAP_TYPE_SESSION_STATE_TRACKING)
            && !expecting_ps_response()
            && proto->m_track_state)
        {
            mxs_mysql_get_session_track_info(tmp);
        }

        read_buffer = tmp;

        if (rcap_type_required(capabilities, RCAP_TYPE_CONTIGUOUS_OUTPUT)
            || proto->m_collect_result
            || proto->m_ignore_replies != 0)
        {
            if ((tmp = gwbuf_make_contiguous(read_buffer)))
            {
                read_buffer = tmp;
            }
            else
            {
                /** Failed to make the buffer contiguous */
                gwbuf_free(read_buffer);
                dcb->trigger_hangup_event();
                return 0;
            }

            if (rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT) || m_collect_result)
            {
                if (rcap_type_required(capabilities, RCAP_TYPE_REQUEST_TRACKING)
                    && !rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT))
                {
                    m_collectq.append(read_buffer);

                    if (!m_reply.is_complete())
                    {
                        return 0;
                    }

                    read_buffer = m_collectq.release();
                    proto->m_collect_result = false;
                    result_collected = true;
                }
                else if (expecting_text_result())
                {
                    if (mxs_mysql_is_result_set(read_buffer))
                    {
                        bool more = false;
                        int eof_cnt = modutil_count_signal_packets(read_buffer, 0, &more, NULL);
                        if (more || eof_cnt % 2 != 0)
                        {
                            dcb->readq_prepend(read_buffer);
                            return 0;
                        }
                    }

                    // Collected the complete result
                    proto->m_collect_result = false;
                    result_collected = true;
                }
                else if (expecting_ps_response()
                         && mxs_mysql_is_prep_stmt_ok(read_buffer)
                         && !complete_ps_response(read_buffer))
                {
                    dcb->readq_prepend(read_buffer);
                    return 0;
                }
                else
                {
                    // Collected the complete result
                    proto->m_collect_result = false;
                    result_collected = true;
                }
            }
        }
    }

    if (proto->changing_user)
    {
        if (auth_change_requested(read_buffer)
            && handle_auth_change_response(read_buffer, dcb))
        {
            gwbuf_free(read_buffer);
            return 0;
        }
        else
        {
            /**
             * The client protocol always requests an authentication method
             * switch to the same plugin to be compatible with most connectors.
             *
             * To prevent packet sequence number mismatch, always return a sequence
             * of 3 for the final response to a COM_CHANGE_USER.
             */
            GWBUF_DATA(read_buffer)[3] = 0x3;
            proto->changing_user = false;
            m_client_data->changing_user = false;
        }
    }

    if (proto->m_ignore_replies > 0)
    {
        /** The reply to a COM_CHANGE_USER is in packet */
        GWBUF* query = modutil_get_next_MySQL_packet(&proto->stored_query);
        proto->stored_query = NULL;
        proto->m_ignore_replies--;
        mxb_assert(proto->m_ignore_replies >= 0);
        GWBUF* reply = modutil_get_next_MySQL_packet(&read_buffer);

        while (read_buffer)
        {
            /** Skip to the last packet if we get more than one */
            gwbuf_free(reply);
            reply = modutil_get_next_MySQL_packet(&read_buffer);
        }

        mxb_assert(reply);
        mxb_assert(!read_buffer);
        uint8_t result = MYSQL_GET_COMMAND(GWBUF_DATA(reply));
        int rval = 0;

        if (result == MYSQL_REPLY_OK)
        {
            MXS_INFO("Response to COM_CHANGE_USER is OK, writing stored query");
            rval = query ? dcb->protocol_write(query) : 1;
        }
        else if (auth_change_requested(reply))
        {
            if (handle_auth_change_response(reply, dcb))
            {
                /** Store the query until we know the result of the authentication
                 * method switch. */
                proto->stored_query = query;
                proto->m_ignore_replies++;

                gwbuf_free(reply);
                return rval;
            }
            else
            {
                /** The server requested a change to something other than
                 * the default auth plugin */
                gwbuf_free(query);
                dcb->trigger_hangup_event();

                // TODO: Use the authenticators to handle COM_CHANGE_USER responses
                MXS_ERROR("Received AuthSwitchRequest to '%s' when '%s' was expected",
                          (char*)GWBUF_DATA(reply) + 5,
                          DEFAULT_MYSQL_AUTH_PLUGIN);
            }
        }
        else
        {
            /**
             * The ignorable command failed when we had a queued query from the
             * client. Generate a fake hangup event to close the DCB and send
             * an error to the client.
             */
            if (result == MYSQL_REPLY_ERR)
            {
                /** The COM_CHANGE USER failed, generate a fake hangup event to
                 * close the DCB and send an error to the client. */
                handle_error_response(dcb, reply);
            }
            else
            {
                /** This should never happen */
                MXS_ERROR("Unknown response to COM_CHANGE_USER (0x%02hhx), "
                          "closing connection",
                          result);
            }

            gwbuf_free(query);
            dcb->trigger_hangup_event();
        }

        gwbuf_free(reply);
        return rval;
    }

    do
    {
        GWBUF* stmt = NULL;

        if (result_collected)
        {
            /** The result set or PS response was collected, we know it's complete */
            stmt = read_buffer;
            read_buffer = NULL;
            gwbuf_set_type(stmt, GWBUF_TYPE_RESULT);

            // TODO: Remove this and use RCAP_TYPE_REQUEST_TRACKING in maxrows
            if (rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT)
                && rcap_type_required(capabilities, RCAP_TYPE_REQUEST_TRACKING))
            {
                GWBUF* tmp = proto->track_response(&stmt);
                mxb_assert(stmt == nullptr);
                stmt = tmp;
            }
        }
        else if (rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT)
                 && !rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT))
        {
            // TODO: Get rid of RCAP_TYPE_STMT_OUTPUT and rely on RCAP_TYPE_REQUEST_TRACKING to provide all
            // the required information.
            stmt = modutil_get_next_MySQL_packet(&read_buffer);

            if (!GWBUF_IS_CONTIGUOUS(stmt))
            {
                // Make sure the buffer is contiguous
                stmt = gwbuf_make_contiguous(stmt);
            }

            // TODO: Remove this and use RCAP_TYPE_REQUEST_TRACKING in maxrows
            if (rcap_type_required(capabilities, RCAP_TYPE_REQUEST_TRACKING))
            {
                GWBUF* tmp = proto->track_response(&stmt);
                mxb_assert(stmt == nullptr);
                stmt = tmp;
            }
        }
        else
        {
            stmt = read_buffer;
            read_buffer = NULL;
        }

        if (session_ok_to_route(dcb))
        {
            if (result_collected)
            {
                // Mark that this is a buffer containing a collected result
                gwbuf_set_type(stmt, GWBUF_TYPE_RESULT);
            }

            thread_local mxs::ReplyRoute route;
            route.clear();
            return_code = proto->m_component->clientReply(stmt, route, m_reply);
        }
        else    /*< session is closing; replying to client isn't possible */
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
void MySQLBackendProtocol::write_ready(DCB* dcb)
{
    if (dcb->state() != DCB::State::POLLING)
    {
        /** Don't write to backend if backend_dcb is not in poll set anymore */
        uint8_t* data = NULL;
        bool com_quit = false;

        if (dcb->writeq())
        {
            data = (uint8_t*) GWBUF_DATA(dcb->writeq());
            com_quit = MYSQL_IS_COM_QUIT(data);
        }

        if (data)
        {
            if (!com_quit)
            {
                mysql_send_custom_error(dcb->session()->client_dcb, 1, 0,
                                        "Writing to backend failed due invalid Maxscale state.");
                MXS_ERROR("Attempt to write buffered data to backend "
                          "failed due internal inconsistent state: %s",
                          mxs::to_string(dcb->state()));
            }
        }
        else
        {
            MXS_DEBUG("Dcb %p in state %s but there's nothing to write either.",
                      dcb,
                      mxs::to_string(dcb->state()));
        }
    }
    else
    {
        mxb_assert(protocol_auth_state != MXS_AUTH_STATE_PENDING_CONNECT);
        dcb->writeq_drain();
    }

    return;
}

int MySQLBackendProtocol::handle_persistent_connection(BackendDCB* dcb, GWBUF* queue)
{
    auto protocol = this;
    int rc = 0;

    mxb_assert(protocol->m_ignore_replies > 0);

    if (MYSQL_IS_COM_QUIT((uint8_t*)GWBUF_DATA(queue)))
    {
        /** The COM_CHANGE_USER was already sent but the session is already
         * closing. */
        MXS_INFO("COM_QUIT received while COM_CHANGE_USER is in progress, closing pooled connection");
        gwbuf_free(queue);
        dcb->trigger_hangup_event();
    }
    else
    {
        /**
         * We're still waiting on the reply to the COM_CHANGE_USER, append the
         * buffer to the stored query. This is possible if the client sends
         * BLOB data on the first command or is sending multiple COM_QUERY
         * packets at one time.
         */
        MXS_INFO("COM_CHANGE_USER in progress, appending query to queue");
        protocol->stored_query = gwbuf_append(protocol->stored_query, queue);
        rc = 1;
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
int32_t MySQLBackendProtocol::write(DCB* plain_dcb, GWBUF* queue)
{
    mxb_assert(plain_dcb->role() == DCB::Role::BACKEND);
    BackendDCB* dcb = static_cast<BackendDCB*>(plain_dcb);
    auto backend_protocol = this;

    if (backend_protocol->m_ignore_replies > 0)
    {
        return handle_persistent_connection(dcb, queue);
    }

    int rc = 0;

    switch (backend_protocol->protocol_auth_state)
    {
    case MXS_AUTH_STATE_HANDSHAKE_FAILED:
    case MXS_AUTH_STATE_FAILED:
        if (dcb->session()->state() != MXS_SESSION::State::STOPPING)
        {
            MXS_ERROR("Unable to write to backend '%s' due to "
                      "%s failure. Server in state %s.",
                      dcb->server()->name(),
                      backend_protocol->protocol_auth_state == MXS_AUTH_STATE_HANDSHAKE_FAILED ?
                      "handshake" : "authentication",
                      dcb->server()->status_string().c_str());
        }

        gwbuf_free(queue);
        rc = 0;
        break;

    case MXS_AUTH_STATE_COMPLETE:
        {
            uint8_t* ptr = GWBUF_DATA(queue);
            mxs_mysql_cmd_t cmd = static_cast<mxs_mysql_cmd_t>(mxs_mysql_get_command(queue));

            MXS_DEBUG("write to dcb %p fd %d protocol state %s.",
                      dcb,
                      dcb->fd(),
                      mxs::to_string(backend_protocol->protocol_auth_state));

            queue = gwbuf_make_contiguous(queue);
            prepare_for_write(dcb, queue);

            if (backend_protocol->reply().command() == MXS_COM_CHANGE_USER)
            {
                return gw_change_user(dcb, dcb->session(), queue);
            }
            else if (cmd == MXS_COM_QUIT && dcb->server()->persistent_conns_enabled())
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
                    backend_protocol->m_ignore_replies++;
                    mxb_assert(backend_protocol->m_ignore_replies > 0);
                }

                /** Write to backend */
                rc = dcb->writeq_append(queue);
            }
        }
        break;

    default:
        {
            MXS_DEBUG("delayed write to dcb %p fd %d protocol state %s.",
                      dcb,
                      dcb->fd(),
                      mxs::to_string(backend_protocol->protocol_auth_state));

            /** Store data until authentication is complete */
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
void MySQLBackendProtocol::error(DCB* dcb)
{
    MXS_SESSION* session = dcb->session();
    if (!session)
    {
        BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);

        if (!backend_dcb->is_in_persistent_pool())
        {
            /** Not a persistent connection, something is wrong. */
            MXS_ERROR("EPOLLERR event on a non-persistent DCB with no session. "
                      "Closing connection.");
        }
        DCB::close(dcb);
    }
    else if (dcb->state() != DCB::State::POLLING || session->state() != MXS_SESSION::State::STARTED)
    {
        int error;
        int len = sizeof(error);

        if (getsockopt(dcb->fd(), SOL_SOCKET, SO_ERROR, &error, (socklen_t*) &len) == 0 && error != 0)
        {
            if (dcb->state() != DCB::State::POLLING)
            {
                MXS_ERROR("DCB in state %s got error '%s'.",
                          mxs::to_string(dcb->state()),
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
        do_handle_error(dcb, "Lost connection to backend server.");
    }
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
void MySQLBackendProtocol::hangup(DCB* dcb)
{
    mxb_assert(!dcb->is_closed());
    MXS_SESSION* session = dcb->session();

    BackendDCB* backend_dcb = static_cast<BackendDCB*>(dcb);

    if (!backend_dcb->is_in_persistent_pool())
    {
        if (session->state() != MXS_SESSION::State::STARTED)
        {
            int error;
            int len = sizeof(error);
            if (getsockopt(dcb->fd(), SOL_SOCKET, SO_ERROR, &error, (socklen_t*) &len) == 0)
            {
                if (error != 0 && session->state() != MXS_SESSION::State::STOPPING)
                {
                    MXS_ERROR("Hangup in session that is not ready for routing, "
                              "Error reported is '%s'.",
                              mxs_strerror(errno));
                }
            }
        }
        else
        {
            do_handle_error(dcb, "Lost connection to backend server.");
        }
    }
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
void MySQLBackendProtocol::backend_set_delayqueue(DCB* dcb, GWBUF* queue)
{
    /* Append data */
    dcb->delayq_append(queue);
}

/**
 * This routine writes the delayq via dcb_write
 * The dcb->m_delayq contains data received from the client before
 * mysql backend authentication succeded
 *
 * @param dcb The current backend DCB
 * @return The dcb_write status
 */
int MySQLBackendProtocol::backend_write_delayqueue(DCB* plain_dcb, GWBUF* buffer)
{
    mxb_assert(plain_dcb->role() == DCB::Role::BACKEND);
    BackendDCB* dcb = static_cast<BackendDCB*>(plain_dcb);
    mxb_assert(buffer);
    mxb_assert(!dcb->is_in_persistent_pool());

    if (MYSQL_IS_CHANGE_USER(((uint8_t*)GWBUF_DATA(buffer))))
    {
        /** Recreate the COM_CHANGE_USER packet with the scramble the backend sent to us */
        gwbuf_free(buffer);
        buffer = gw_create_change_user_packet(m_client_data);
    }

    int rc = 1;

    if (MYSQL_IS_COM_QUIT(((uint8_t*)GWBUF_DATA(buffer))) && dcb->server()->persistent_conns_enabled())
    {
        /** We need to keep the pooled connections alive so we just ignore the COM_QUIT packet */
        gwbuf_free(buffer);
        rc = 1;
    }
    else
    {
        rc = dcb->writeq_append(buffer);
    }

    if (rc == 0)
    {
        do_handle_error(dcb, "Lost connection to backend server while writing delay queue.");
    }

    return rc;
}

/**
 * This routine handles the COM_CHANGE_USER command
 *
 * TODO: Move this into the authenticators
 *
 * @param dcb           The current backend DCB
 * @param in_session    The current session data (MYSQL_session)
 * @param queue         The GWBUF containing the COM_CHANGE_USER receveid
 * @return 1 on success and 0 on failure
 */
int MySQLBackendProtocol::gw_change_user(DCB* backend, MXS_SESSION* in_session, GWBUF* queue)
{
    gwbuf_free(queue);
    return gw_send_change_user_to_backend(backend);
}

/**
 * Create COM_CHANGE_USER packet and store it to GWBUF.
 *
 * @param mses          MySQL session
 * @return GWBUF buffer consisting of COM_CHANGE_USER packet
 * @note the function doesn't fail
 */
GWBUF* MySQLBackendProtocol::gw_create_change_user_packet(const MYSQL_session* mses)
{
    MySQLProtocol* protocol = this;
    GWBUF* buffer;
    uint8_t* payload = NULL;
    uint8_t* payload_start = NULL;
    long bytes;
    char dbpass[MYSQL_USER_MAXLEN + 1] = "";
    const char* curr_db = NULL;
    const uint8_t* curr_passwd = NULL;
    unsigned int charset;

    const char* db = mses->db;
    const char* user = m_client_data->user;
    const uint8_t* pwd = mses->client_sha1;

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

    // The COM_CHANGE_USER is a session command so the result must be collected
    gwbuf_set_type(buffer, GWBUF_TYPE_COLLECT_RESULT);

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
                   new_sha,
                   hash1,
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
    /*
     * Following needed if more to be added
     * payload += strlen("mysql_native_password");
     ** put here the paylod size: bytes to write - 4 bytes packet header
     */
    gw_mysql_set_byte3(payload_start, (bytes - 4));

    return buffer;
}

/**
 * Write a MySQL CHANGE_USER packet to backend server
 *
 * @return 1 on success, 0 on failure
 */
int
MySQLBackendProtocol::gw_send_change_user_to_backend(DCB* backend)
{
    GWBUF* buffer = gw_create_change_user_packet(m_client_data);
    int rc = 0;
    if (backend->writeq_append(buffer))
    {
        changing_user = true;
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
void MySQLBackendProtocol::gw_send_proxy_protocol_header(BackendDCB* backend_dcb)
{
    // TODO: Add support for chained proxies. Requires reading the client header.

    const ClientDCB* client_dcb = backend_dcb->session()->client_dcb;
    const int client_fd = client_dcb->fd();
    const sa_family_t family = client_dcb->ip().ss_family;
    const char* family_str = NULL;

    struct sockaddr_storage sa_peer;
    struct sockaddr_storage sa_local;
    socklen_t sa_peer_len = sizeof(sa_peer);
    socklen_t sa_local_len = sizeof(sa_local);

    /* Fill in peer's socket address.  */
    if (getpeername(client_fd, (struct sockaddr*)&sa_peer, &sa_peer_len) == -1)
    {
        MXS_ERROR("'%s' failed on file descriptor '%d'.", "getpeername()", client_fd);
        return;
    }

    /* Fill in this socket's local address. */
    if (getsockname(client_fd, (struct sockaddr*)&sa_local, &sa_local_len) == -1)
    {
        MXS_ERROR("'%s' failed on file descriptor '%d'.", "getsockname()", client_fd);
        return;
    }
    mxb_assert(sa_peer.ss_family == sa_local.ss_family);

    char peer_ip[INET6_ADDRSTRLEN];
    char maxscale_ip[INET6_ADDRSTRLEN];
    in_port_t peer_port;
    in_port_t maxscale_port;

    if (!get_ip_string_and_port(&sa_peer, peer_ip, sizeof(peer_ip), &peer_port)
        || !get_ip_string_and_port(&sa_local, maxscale_ip, sizeof(maxscale_ip), &maxscale_port))
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
    char proxy_header[108];     // 108 is the worst-case length
    if (family == AF_INET || family == AF_INET6)
    {
        rval = snprintf(proxy_header,
                        sizeof(proxy_header),
                        "PROXY %s %s %s %d %d\r\n",
                        family_str,
                        peer_ip,
                        maxscale_ip,
                        peer_port,
                        maxscale_port);
    }
    else
    {
        rval = snprintf(proxy_header, sizeof(proxy_header), "PROXY %s\r\n", family_str);
    }
    if (rval < 0 || rval >= (int)sizeof(proxy_header))
    {
        MXS_ERROR("Proxy header printing error, produced '%s'.", proxy_header);
        return;
    }

    GWBUF* headerbuf = gwbuf_alloc_and_load(strlen(proxy_header), proxy_header);
    if (headerbuf)
    {
        MXS_INFO("Sending proxy-protocol header '%s' to backend %s.",
                 proxy_header,
                 backend_dcb->server()->name());
        if (!backend_dcb->writeq_append(headerbuf))
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
static bool get_ip_string_and_port(struct sockaddr_storage* sa,
                                   char* ip,
                                   int iplen,
                                   in_port_t* port_out)
{
    bool success = false;
    in_port_t port;

    switch (sa->ss_family)
    {
    case AF_INET:
        {
            struct sockaddr_in* sock_info = (struct sockaddr_in*)sa;
            struct in_addr* addr = &(sock_info->sin_addr);
            success = (inet_ntop(AF_INET, addr, ip, iplen) != NULL);
            port = ntohs(sock_info->sin_port);
        }
        break;

    case AF_INET6:
        {
            struct sockaddr_in6* sock_info = (struct sockaddr_in6*)sa;
            struct in6_addr* addr = &(sock_info->sin6_addr);
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

bool MySQLBackendProtocol::established(DCB* dcb)
{
    auto proto = this;
    return proto->protocol_auth_state == MXS_AUTH_STATE_COMPLETE
           && (proto->m_ignore_replies == 0)
           && !proto->stored_query;
}

json_t* MySQLBackendProtocol::diagnostics_json(DCB* dcb)
{
    auto proto = this;
    json_t* obj = json_object();
    json_object_set_new(obj, "connection_id", json_integer(proto->m_thread_id));
    return obj;
}

int MySQLBackendProtocol::mysql_send_com_quit(DCB* dcb, int packet_number, GWBUF* bufparam)
{
    mxb_assert(packet_number <= 255);

    int nbytes = 0;
    GWBUF* buf = bufparam ? bufparam : mysql_create_com_quit(NULL, packet_number);
    if (buf)
    {
        nbytes = dcb->protocol_write(buf);
    }
    return nbytes;
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
bool MySQLBackendProtocol::read_complete_packet(DCB* dcb, GWBUF** readbuf)
{
    bool rval = false;
    GWBUF* localbuf = NULL;

    if (dcb->read(&localbuf, 0) >= 0)
    {
        rval = true;
        GWBUF* packets = modutil_get_complete_packets(&localbuf);

        if (packets)
        {
            /** A complete packet was read */
            *readbuf = packets;
        }

        if (localbuf)
        {
            /** Store any extra data in the DCB's readqueue */

            dcb->readq_append(localbuf);
        }
    }

    return rval;
}

/**
 * @brief Check if a buffer contains a result set
 *
 * @param buffer Buffer to check
 * @return True if the @c buffer contains the start of a result set
 */
bool MySQLBackendProtocol::mxs_mysql_is_result_set(GWBUF* buffer)
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

/**
 * Process a reply from a backend server. This method collects all complete packets and
 * updates the internal response state.
 *
 * @param buffer Pointer to buffer containing the raw response. Any partial packets will be left in this
 *               buffer.
 * @return All complete packets that were in `buffer`
 */
GWBUF* MySQLBackendProtocol::track_response(GWBUF** buffer)
{
    using mxs::ReplyState;
    GWBUF* rval = nullptr;

    if (m_reply.command() == MXS_COM_STMT_FETCH)
    {
        // TODO: m_reply.m_error is not updated here.
        // If the server responded with an error, n_eof > 0

        // COM_STMT_FETCH is used when a COM_STMT_EXECUTE opens a cursor and the result is read in chunks:
        // https://mariadb.com/kb/en/library/com_stmt_fetch/
        if (consume_fetched_rows(*buffer))
        {
            set_reply_state(ReplyState::DONE);
        }
        rval = modutil_get_complete_packets(buffer);
    }
    else if (m_reply.command() == MXS_COM_STATISTICS)
    {
        // COM_STATISTICS returns a single string and thus requires special handling:
        // https://mariadb.com/kb/en/library/com_statistics/#response
        set_reply_state(ReplyState::DONE);
        rval = modutil_get_complete_packets(buffer);
    }
    else if (m_reply.command() == MXS_COM_STMT_PREPARE && mxs_mysql_is_prep_stmt_ok(*buffer))
    {
        // Successful COM_STMT_PREPARE responses return a special OK packet:
        // https://mariadb.com/kb/en/library/com_stmt_prepare/#com_stmt_prepare_ok

        // TODO: Stream this result and don't collect it
        if (complete_ps_response(*buffer))
        {
            rval = modutil_get_complete_packets(buffer);
            set_reply_state(ReplyState::DONE);
        }
    }
    else
    {
        // Normal result, process it one packet at a time
        rval = process_packets(buffer);
    }

    if (rval)
    {
        m_reply.add_bytes(gwbuf_length(rval));
    }

    return rval;
}

/**
 * Write MySQL authentication packet to backend server.
 *
 * @param dcb  Backend DCB
 * @return Authentication state after sending handshake response
 */
mxs_auth_state_t MySQLBackendProtocol::gw_send_backend_auth(BackendDCB* dcb)
{
    mxs_auth_state_t rval = MXS_AUTH_STATE_FAILED;

    if (dcb->session() == NULL
        || (dcb->session()->state() != MXS_SESSION::State::CREATED
            && dcb->session()->state() != MXS_SESSION::State::STARTED)
        || (dcb->server()->ssl().context() && dcb->ssl_state() == DCB::SSLState::HANDSHAKE_FAILED))
    {
        return rval;
    }

    bool with_ssl = dcb->server()->ssl().context();
    bool ssl_established = dcb->ssl_state() == DCB::SSLState::ESTABLISHED;

    GWBUF* buffer = gw_generate_auth_response(with_ssl, ssl_established, dcb->service()->capabilities());
    mxb_assert(buffer);

    if (with_ssl && !ssl_established)
    {
        if (dcb->writeq_append(buffer) && dcb->ssl_handshake() >= 0)
        {
            rval = MXS_AUTH_STATE_CONNECTED;
        }
    }
    else if (dcb->writeq_append(buffer))
    {
        rval = MXS_AUTH_STATE_RESPONSE_SENT;
    }

    return rval;
}

/**
 * Read the backend server MySQL handshake
 *
 * @param dcb  Backend DCB
 * @return true on success, false on failure
 */
bool MySQLBackendProtocol::gw_read_backend_handshake(DCB* dcb, GWBUF* buffer)
{
    bool rval = false;
    uint8_t* payload = GWBUF_DATA(buffer) + 4;

    if (gw_decode_mysql_server_handshake(payload) >= 0)
    {
        rval = true;
    }

    return rval;
}

/**
 * Sends a response for an AuthSwitchRequest to the default auth plugin
 */
int MySQLBackendProtocol::send_mysql_native_password_response(DCB* dcb)
{
    uint8_t* curr_passwd = memcmp(m_client_data->client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) ?
                           m_client_data->client_sha1 : null_client_sha1;

    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + GW_MYSQL_SCRAMBLE_SIZE);
    uint8_t* data = GWBUF_DATA(buffer);
    gw_mysql_set_byte3(data, GW_MYSQL_SCRAMBLE_SIZE);
    data[3] = 2;    // This is the third packet after the COM_CHANGE_USER
    mxs_mysql_calculate_hash(scramble, curr_passwd, data + MYSQL_HEADER_LEN);

    return dcb->writeq_append(buffer);
}

void MySQLBackendProtocol::mxs_mysql_get_session_track_info(GWBUF* buff)
{
    auto proto = this;
    size_t offset = 0;
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    if (proto->server_capabilities & GW_MYSQL_CAPABILITIES_SESSION_TRACK)
    {
        while (gwbuf_copy_data(buff, offset, MYSQL_HEADER_LEN + 1, header_and_command)
               == (MYSQL_HEADER_LEN + 1))
        {
            size_t packet_len = gw_mysql_get_byte3(header_and_command) + MYSQL_HEADER_LEN;
            uint8_t cmd = header_and_command[MYSQL_COM_OFFSET];

            if (packet_len > MYSQL_OK_PACKET_MIN_LEN && cmd == MYSQL_REPLY_OK
                && (proto->m_num_eof_packets % 2) == 0)
            {
                buff->gwbuf_type |= GWBUF_TYPE_REPLY_OK;
                mxs_mysql_parse_ok_packet(buff, offset, packet_len);
            }

            uint8_t current_command = proto->reply().command();

            if ((current_command == MXS_COM_QUERY || current_command == MXS_COM_STMT_FETCH
                 || current_command == MXS_COM_STMT_EXECUTE) && cmd == MYSQL_REPLY_EOF)
            {
                proto->m_num_eof_packets++;
            }
            offset += packet_len;
        }
    }
}

/**
 *  Parse ok packet to get session track info, save to buff properties
 *  @param buff           Buffer contain multi compelte packets
 *  @param packet_offset  Ok packet offset in this buff
 *  @param packet_len     Ok packet lengh
 */
void MySQLBackendProtocol::mxs_mysql_parse_ok_packet(GWBUF* buff, size_t packet_offset, size_t packet_len)
{
    uint8_t local_buf[packet_len];
    uint8_t* ptr = local_buf;
    char* trx_info, * var_name, * var_value;

    gwbuf_copy_data(buff, packet_offset, packet_len, local_buf);
    ptr += (MYSQL_HEADER_LEN + 1);  // Header and Command type
    mxq::leint_consume(&ptr);       // Affected rows
    mxq::leint_consume(&ptr);       // Last insert-id
    uint16_t server_status = gw_mysql_get_byte2(ptr);
    ptr += 2;   // status
    ptr += 2;   // number of warnings

    if (ptr < (local_buf + packet_len))
    {
        size_t size;
        mxq::lestr_consume(&ptr, &size);    // info

        if (server_status & SERVER_SESSION_STATE_CHANGED)
        {
            MXB_AT_DEBUG(uint64_t data_size = ) mxq::leint_consume(&ptr);   // total
            // SERVER_SESSION_STATE_CHANGED
            // length
            mxb_assert(data_size == packet_len - (ptr - local_buf));

            while (ptr < (local_buf + packet_len))
            {
                enum_session_state_type type = (enum enum_session_state_type)mxq::leint_consume(&ptr);
#if defined (SS_DEBUG)
                mxb_assert(type <= SESSION_TRACK_TRANSACTION_TYPE);
#endif
                switch (type)
                {
                    case SESSION_TRACK_STATE_CHANGE:
                    case SESSION_TRACK_SCHEMA:
                        size = mxq::leint_consume(&ptr);    // Length of the overall entity.
                        ptr += size;
                        break;

                    case SESSION_TRACK_GTIDS:
                        mxq::leint_consume(&ptr);   // Length of the overall entity.
                        mxq::leint_consume(&ptr);   // encoding specification
                        var_value = mxq::lestr_consume_dup(&ptr);
                        gwbuf_add_property(buff, MXS_LAST_GTID, var_value);
                        MXS_FREE(var_value);
                        break;

                    case SESSION_TRACK_TRANSACTION_CHARACTERISTICS:
                        mxq::leint_consume(&ptr);   // length
                        var_value = mxq::lestr_consume_dup(&ptr);
                        gwbuf_add_property(buff, "trx_characteristics", var_value);
                        MXS_FREE(var_value);
                        break;

                    case SESSION_TRACK_SYSTEM_VARIABLES:
                        mxq::leint_consume(&ptr);   // lenth
                        // system variables like autocommit, schema, charset ...
                        var_name = mxq::lestr_consume_dup(&ptr);
                        var_value = mxq::lestr_consume_dup(&ptr);
                        gwbuf_add_property(buff, var_name, var_value);
                                MXS_DEBUG("SESSION_TRACK_SYSTEM_VARIABLES, name:%s, value:%s", var_name, var_value);
                        MXS_FREE(var_name);
                        MXS_FREE(var_value);
                        break;

                    case SESSION_TRACK_TRANSACTION_TYPE:
                        mxq::leint_consume(&ptr);   // length
                        trx_info = mxq::lestr_consume_dup(&ptr);
                                MXS_DEBUG("get trx_info:%s", trx_info);
                        gwbuf_add_property(buff, (char*)"trx_state", trx_info);
                        MXS_FREE(trx_info);
                        break;

                    default:
                        mxq::lestr_consume(&ptr, &size);
                                MXS_WARNING("recieved unexpecting session track type:%d", type);
                        break;
                }
            }
        }
    }
}

/**
 * Decode mysql server handshake
 *
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 *
 */
int MySQLBackendProtocol::gw_decode_mysql_server_handshake(uint8_t* payload)
{
    auto conn = this;
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

            MXS_INFO("Connected to '%s' with thread id %u", conn->reply().target()->name(), tid);

    /* TODO: Correct value of thread id could be queried later from backend if
     * there is any worry it might be larger than 32bit allows. */
    conn->m_thread_id = tid;

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
 * Create a response to the server handshake
 *
 * @param with_ssl             Whether to create an SSL response or a normal response packet
 * @param ssl_established      Set to true if the SSL response has been sent
 * @param service_capabilities Capabilities of the connecting service
 *
 * @return Generated response packet
 */
GWBUF* MySQLBackendProtocol::gw_generate_auth_response(bool with_ssl, bool ssl_established,
                                                       uint64_t service_capabilities)
{
    auto conn = this;
    auto client = m_client_data;
    uint8_t client_capabilities[4] = {0, 0, 0, 0};
    uint8_t* curr_passwd = NULL;

    if (memcmp(client->client_sha1, null_client_sha1, MYSQL_SCRAMBLE_LEN) != 0)
    {
        curr_passwd = client->client_sha1;
    }

    uint32_t capabilities = create_capabilities(with_ssl, client->db[0], service_capabilities);
    gw_mysql_set_byte4(client_capabilities, capabilities);

    /**
     * Use the default authentication plugin name. If the server is using a
     * different authentication mechanism, it will send an AuthSwitchRequest
     * packet.
     */
    const char* auth_plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;

    const std::string& username = m_client_data->user;
    long bytes = response_length(with_ssl,
                                 ssl_established,
                                 username.c_str(),
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
        memcpy(payload, username.c_str(), username.length());
        payload += username.length();
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
 * @brief Computes the capabilities bit mask for connecting to backend DB
 *
 * We start by taking the default bitmask and removing any bits not set in
 * the bitmask contained in the connection structure. Then add SSL flag if
 * the connection requires SSL (set from the MaxScale configuration). The
 * compression flag may be set, although compression is NOT SUPPORTED. If a
 * database name has been specified in the function call, the relevant flag
 * is set.
 *
 * @param db_specified Whether the connection request specified a database
 * @param compress Whether compression is requested - NOT SUPPORTED
 * @return Bit mask (32 bits)
 * @note Capability bits are defined in maxscale/protocol/mysql.h
 */
uint32_t MySQLBackendProtocol::create_capabilities(bool with_ssl, bool db_specified, uint64_t capabilities)
{
    uint32_t final_capabilities;

    /** Copy client's flags to backend but with the known capabilities mask */
    final_capabilities = (client_capabilities & (uint32_t)GW_MYSQL_CAPABILITIES_CLIENT);

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

GWBUF* MySQLBackendProtocol::process_packets(GWBUF** result)
{
    mxs::Buffer buffer(*result);
    auto it = buffer.begin();
    size_t total_bytes = buffer.length();
    size_t bytes_used = 0;

    while (it != buffer.end())
    {
        size_t bytes_left = total_bytes - bytes_used;

        if (bytes_left < MYSQL_HEADER_LEN)
        {
            // Partial header
            break;
        }

        // Extract packet length and command byte
        uint32_t len = *it++;
        len |= (*it++) << 8;
        len |= (*it++) << 16;
        ++it;   // Skip the sequence

        if (bytes_left < len + MYSQL_HEADER_LEN)
        {
            // Partial packet payload
            break;
        }

        bytes_used += len + MYSQL_HEADER_LEN;

        mxb_assert(it != buffer.end());
        auto end = it;
        end.advance(len);

        // Ignore the tail end of a large packet large packet. Only resultsets can generate packets this large
        // and we don't care what the contents are and thus it is safe to ignore it.
        bool skip_next = m_skip_next;
        m_skip_next = len == GW_MYSQL_MAX_PACKET_LEN;

        if (!skip_next)
        {
            process_one_packet(it, end, len);
        }

        it = end;
    }

    buffer.release();
    return gwbuf_split(result, bytes_used);
}

void MySQLBackendProtocol::process_one_packet(Iter it, Iter end, uint32_t len)
{
    uint8_t cmd = *it;
    switch (m_reply.state())
    {
        case ReplyState::START:
            process_reply_start(it, end);
            break;

        case ReplyState::DONE:
            if (cmd == MYSQL_REPLY_ERR)
            {
                update_error(++it, end);
            }
            else
            {
                // This should never happen
                MXS_ERROR("Unexpected result state. cmd: 0x%02hhx, len: %u server: %s",
                          cmd, len, m_reply.target()->name());
                session_dump_statements(session());
                session_dump_log(session());
                mxb_assert(!true);
            }
            break;

        case ReplyState::RSET_COLDEF:
            mxb_assert(m_num_coldefs > 0);
            --m_num_coldefs;

            if (m_num_coldefs == 0)
            {
                set_reply_state(ReplyState::RSET_COLDEF_EOF);
                // Skip this state when DEPRECATE_EOF capability is supported
            }
            break;

        case ReplyState::RSET_COLDEF_EOF:
            mxb_assert(cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN);
            set_reply_state(ReplyState::RSET_ROWS);

            if (m_opening_cursor)
            {
                m_opening_cursor = false;
                MXS_INFO("Cursor successfully opened");
                set_reply_state(ReplyState::DONE);
            }
            break;

        case ReplyState::RSET_ROWS:
            if (cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN)
            {
                set_reply_state(is_last_eof(it) ? ReplyState::DONE : ReplyState::START);
            }
            else if (cmd == MYSQL_REPLY_ERR)
            {
                ++it;
                update_error(it, end);
                set_reply_state(ReplyState::DONE);
            }
            else
            {
                m_reply.add_rows(1);
            }
            break;
    }
}

void MySQLBackendProtocol::process_reply_start(Iter it, Iter end)
{
    uint8_t cmd = *it;

    switch (cmd)
    {
        case MYSQL_REPLY_OK:
            if (is_last_ok(it))
            {
                // No more results
                set_reply_state(ReplyState::DONE);
            }
            break;

        case MYSQL_REPLY_LOCAL_INFILE:
            // The client will send a request after this with the contents of the file which the server will
            // respond to with either an OK or an ERR packet
            session_set_load_active(m_session, true);
            set_reply_state(ReplyState::DONE);
            break;

        case MYSQL_REPLY_ERR:
            // Nothing ever follows an error packet
            ++it;
            update_error(it, end);
            set_reply_state(ReplyState::DONE);
            break;

        case MYSQL_REPLY_EOF:
            // EOF packets are never expected as the first response
            mxb_assert(!true);
            break;

        default:
            if (m_reply.command() == MXS_COM_FIELD_LIST)
            {
                // COM_FIELD_LIST sends a strange kind of a result set that doesn't have field definitions
                set_reply_state(ReplyState::RSET_ROWS);
            }
            else
            {
                // Start of a result set
                m_num_coldefs = get_encoded_int(it);
                m_reply.add_field_count(m_num_coldefs);
                set_reply_state(ReplyState::RSET_COLDEF);
            }

            break;
    }
}

/**
 * Update @c m_error.
 *
 * @param it   Iterator that points to the first byte of the error code in an error packet.
 * @param end  Iterator pointing one past the end of the error packet.
 */
void MySQLBackendProtocol::update_error(Iter it, Iter end)
{
    uint16_t code = 0;
    code |= (*it++);
    code |= (*it++) << 8;
    ++it;
    auto sql_state_begin = it;
    it.advance(5);
    auto sql_state_end = it;
    auto message_begin = sql_state_end;
    auto message_end = end;

    m_reply.set_error(code, sql_state_begin, sql_state_end, message_begin, message_end);
}

bool MySQLBackendProtocol::consume_fetched_rows(GWBUF* buffer)
{
    // TODO: Get rid of this and do COM_STMT_FETCH processing properly by iterating over the packets and
    //       splitting them

    bool rval = false;
    bool more = false;
    int n_eof = modutil_count_signal_packets(buffer, 0, &more, (modutil_state*)&m_modutil_state);
    int num_packets = modutil_count_packets(buffer);

    // If the server responded with an error, n_eof > 0
    if (n_eof > 0)
    {
        m_reply.add_rows(num_packets - 1);
        rval = true;
    }
    else
    {
        m_reply.add_rows(num_packets);
        m_expected_rows -= num_packets;
        mxb_assert(m_expected_rows >= 0);
        rval = m_expected_rows == 0;
    }

    return rval;
}

uint64_t MySQLBackendProtocol::thread_id() const
{
    return m_thread_id;
}

void MySQLBackendProtocol::set_client_data(MySQLClientProtocol& client_protocol)
{
    /** Copy client flags to backend protocol */
    client_capabilities = client_protocol.client_capabilities;
    charset = client_protocol.charset;
    extra_capabilities = client_protocol.extra_capabilities;
    m_client_data = client_protocol.session_data();
    // TODO: authenticators may also need data swapping
}

