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

#include "rwsplitsession.hh"

#include <cmath>

#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>

using namespace maxscale;

RWSplitSession::RWSplitSession(RWSplit* instance,
                               MXS_SESSION* session,
                               PRWBackends  backends,
                               RWBackend*   master)
    : mxs::RouterSession(session)
    , m_backends(backends)
    , m_current_master(master)
    , m_config(instance->config())
    , m_nbackends(instance->service()->n_dbref)
    , m_client(session->client_dcb)
    , m_sescmd_count(1)
    ,                   // Needs to be a positive number to work
    m_expected_responses(0)
    , m_query_queue(NULL)
    , m_router(instance)
    , m_sent_sescmd(0)
    , m_recv_sescmd(0)
    , m_gtid_pos("")
    , m_wait_gtid(NONE)
    , m_next_seq(0)
    , m_qc(this, session, m_config.use_sql_variables_in)
    , m_retry_duration(0)
    , m_is_replay_active(false)
    , m_can_replay_trx(true)
{
    if (m_config.rw_max_slave_conn_percent)
    {
        int n_conn = 0;
        double pct = (double)m_config.rw_max_slave_conn_percent / 100.0;
        n_conn = MXS_MAX(floor((double)m_nbackends * pct), 1);
        m_config.max_slave_connections = n_conn;
    }
}

RWSplitSession* RWSplitSession::create(RWSplit* router, MXS_SESSION* session)
{
    RWSplitSession* rses = NULL;

    if (router->have_enough_servers())
    {
        PRWBackends backends = RWBackend::from_servers(router->service()->dbref);

        /**
         * At least the master must be found if the router is in the strict mode.
         * If sessions without master are allowed, only a slave must be found.
         */

        RWBackend* master;

        if (router->select_connect_backend_servers(session,
                                                   backends,
                                                   &master,
                                                   NULL,
                                                   NULL,
                                                   connection_type::ALL))
        {
            if ((rses = new RWSplitSession(router, session, backends, master)))
            {
                router->stats().n_sessions += 1;
            }

            for (auto& b : backends)
            {
                router->server_stats(b->server()).start_session();
            }
        }
    }

    return rses;
}

void close_all_connections(PRWBackends& backends)
{
    for (PRWBackends::iterator it = backends.begin(); it != backends.end(); it++)
    {
        RWBackend* backend = *it;

        if (backend->in_use())
        {
            backend->close();
        }
    }
}

void RWSplitSession::close()
{
    close_all_connections(m_backends);
    m_current_query.reset();

    for (auto& backend : m_backends)
    {
        ResponseStat& stat = backend->response_stat();

        if (stat.make_valid())
        {
            server_add_response_average(backend->server(),
                                        stat.average().secs(),
                                        stat.num_samples());
        }
        backend->response_stat().reset();

        m_router->server_stats(backend->server()).end_session(backend->session_timer().split(),
                                                              backend->select_timer().total(),
                                                              backend->num_selects());
    }
}

int32_t RWSplitSession::routeQuery(GWBUF* querybuf)
{
    int rval = 0;

    if (m_is_replay_active && !GWBUF_IS_REPLAYED(querybuf))
    {
        MXS_INFO("New query received while transaction replay is active: %s",
                 mxs::extract_sql(querybuf).c_str());
        mxb_assert(!m_interrupted_query.get());
        m_interrupted_query.reset(querybuf);
        return 1;
    }

    if (m_query_queue == NULL
        && (m_expected_responses == 0
            || m_qc.load_data_state() == QueryClassifier::LOAD_DATA_ACTIVE
            || m_qc.large_query()))
    {
        /** Gather the information required to make routing decisions */

        QueryClassifier::current_target_t current_target;

        if (m_target_node == NULL)
        {
            current_target = QueryClassifier::CURRENT_TARGET_UNDEFINED;
        }
        else if (m_target_node == m_current_master)
        {
            current_target = QueryClassifier::CURRENT_TARGET_MASTER;
        }
        else
        {
            current_target = QueryClassifier::CURRENT_TARGET_SLAVE;
        }

        if (!m_qc.large_query())
        {
            m_qc.update_route_info(current_target, querybuf);
        }

        /** No active or pending queries */
        if (route_single_stmt(querybuf))
        {
            rval = 1;
        }
    }
    else
    {
        /**
         * We are already processing a request from the client. Store the
         * new query and wait for the previous one to complete.
         */
        mxb_assert(m_expected_responses > 0 || m_query_queue);
        MXS_INFO("Storing query (len: %d cmd: %0x), expecting %d replies to current command",
                 gwbuf_length(querybuf),
                 GWBUF_DATA(querybuf)[4],
                 m_expected_responses);
        m_query_queue = gwbuf_append(m_query_queue, querybuf);
        querybuf = NULL;
        rval = 1;

        if (m_expected_responses == 0 && !route_stored_query())
        {
            rval = 0;
        }
    }

    if (querybuf != NULL)
    {
        gwbuf_free(querybuf);
    }

    return rval;
}

/**
 * @brief Route a stored query
 *
 * When multiple queries are executed in a pipeline fashion, the readwritesplit
 * stores the extra queries in a queue. This queue is emptied after reading a
 * reply from the backend server.
 *
 * @param rses Router client session
 * @return True if a stored query was routed successfully
 */
bool RWSplitSession::route_stored_query()
{
    bool rval = true;

    /** Loop over the stored statements as long as the routeQuery call doesn't
     * append more data to the queue. If it appends data to the queue, we need
     * to wait for a response before attempting another reroute */
    while (m_query_queue)
    {
        MXS_INFO("Routing stored queries");
        GWBUF* query_queue = modutil_get_next_MySQL_packet(&m_query_queue);
        query_queue = gwbuf_make_contiguous(query_queue);
        mxb_assert(query_queue);

        if (query_queue == NULL)
        {
            MXS_ALERT("Queued query unexpectedly empty. Bytes queued: %d Hexdump: ",
                      gwbuf_length(m_query_queue));
            gwbuf_hexdump(m_query_queue, LOG_ALERT);
            return true;
        }

        /** Store the query queue locally for the duration of the routeQuery call.
         * This prevents recursive calls into this function. */
        GWBUF* temp_storage = m_query_queue;
        m_query_queue = NULL;

        // TODO: Move the handling of queued queries to the client protocol
        // TODO: module where the command tracking is done automatically.
        uint8_t cmd = mxs_mysql_get_command(query_queue);
        mysql_protocol_set_current_command(m_client, (mxs_mysql_cmd_t)cmd);

        if (!routeQuery(query_queue))
        {
            rval = false;
            MXS_ERROR("Failed to route queued query.");
        }

        if (m_query_queue == NULL)
        {
            /** Query successfully routed and no responses are expected */
            m_query_queue = temp_storage;
        }
        else
        {
            /** Routing was stopped, we need to wait for a response before retrying */
            m_query_queue = gwbuf_append(temp_storage, m_query_queue);
            break;
        }
    }

    return rval;
}

/**
 * @bref discard the result of MASTER_GTID_WAIT statement
 *
 * The result will be an error or an OK packet.
 *
 * @param buffer Original reply buffer
 *
 * @return Any data after the ERR/OK packet, NULL for no data
 */
GWBUF* RWSplitSession::discard_master_wait_gtid_result(GWBUF* buffer)
{
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN + 1, header_and_command);

    if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_OK)
    {
        // MASTER_WAIT_GTID is complete, discard the OK packet or return the ERR packet
        m_wait_gtid = UPDATING_PACKETS;

        // Discard the OK packet and start updating sequence numbers
        uint8_t packet_len = MYSQL_GET_PAYLOAD_LEN(header_and_command) + MYSQL_HEADER_LEN;
        m_next_seq = 1;
        buffer = gwbuf_consume(buffer, packet_len);
    }
    else if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_ERR)
    {
        // The MASTER_WAIT_GTID command failed and no further packets will come
        m_wait_gtid = RETRYING_ON_MASTER;
    }

    return buffer;
}

/**
 * @brief Find the backend reference that matches the given DCB
 *
 * @param dcb DCB to match
 *
 * @return The correct reference
 */
RWBackend* RWSplitSession::get_backend_from_dcb(DCB* dcb)
{
    mxb_assert(dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);

    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        RWBackend* backend = *it;

        if (backend->in_use() && backend->dcb() == dcb)
        {
            return backend;
        }
    }

    /** We should always have a valid backend reference and in case we don't,
     * something is terribly wrong. */
    MXS_ALERT("No reference to DCB %p found, aborting.", dcb);
    raise(SIGABRT);

    // Make the compiler happy
    abort();
}

/**
 * @bref After discarded the wait result, we need correct the seqence number of every packet
 *
 * @param buffer origin reply buffer
 * @param proto  MySQLProtocol
 *
 */
void RWSplitSession::correct_packet_sequence(GWBUF* buffer)
{
    uint8_t header[3];
    uint32_t offset = 0;

    while (gwbuf_copy_data(buffer, offset, 3, header) == 3)
    {
        uint32_t packet_len = MYSQL_GET_PAYLOAD_LEN(header) + MYSQL_HEADER_LEN;
        uint8_t* seq = gwbuf_byte_pointer(buffer, offset + MYSQL_SEQ_OFFSET);
        *seq = m_next_seq++;
        offset += packet_len;
    }
}

static bool connection_was_killed(GWBUF* buffer)
{
    bool rval = false;

    if (mxs_mysql_is_err_packet(buffer))
    {
        uint8_t buf[2];
        // First two bytes after the 0xff byte are the error code
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN + 1, 2, buf);
        uint16_t errcode = gw_mysql_get_byte2(buf);
        rval = errcode == ER_CONNECTION_KILLED;
    }

    return rval;
}

static void log_unexpected_response(RWBackend* backend, GWBUF* buffer, GWBUF* current_query)
{
    if (mxs_mysql_is_err_packet(buffer))
    {
        /** This should be the only valid case where the server sends a response
         * without the client sending one first. MaxScale does not yet advertise
         * the progress reporting flag so we don't need to handle it. */
        uint8_t* data = GWBUF_DATA(buffer);
        size_t len = MYSQL_GET_PAYLOAD_LEN(data);
        uint16_t errcode = MYSQL_GET_ERRCODE(data);
        std::string errstr((char*)data + 7, (char*)data + 7 + len - 3);

        mxb_assert(errcode != ER_CONNECTION_KILLED);
        MXS_WARNING("Server '%s' sent an unexpected error: %hu, %s",
                    backend->name(),
                    errcode,
                    errstr.c_str());
    }
    else
    {
        std::string sql = current_query ? mxs::extract_sql(current_query, 1024) : "<not available>";
        MXS_ERROR("Unexpected internal state: received response 0x%02hhx from "
                  "server '%s' when no response was expected. Command: 0x%02hhx "
                  "Query: %s",
                  mxs_mysql_get_command(buffer),
                  backend->name(),
                  backend->current_command(),
                  sql.c_str());
        session_dump_statements(backend->dcb()->session);
        mxb_assert(false);
    }
}

GWBUF* RWSplitSession::handle_causal_read_reply(GWBUF* writebuf, RWBackend* backend)
{
    if (m_config.causal_reads)
    {
        if (GWBUF_IS_REPLY_OK(writebuf) && backend == m_current_master)
        {
            if (char* tmp = gwbuf_get_property(writebuf, MXS_LAST_GTID))
            {
                m_gtid_pos = std::string(tmp);
            }
        }

        if (m_wait_gtid == WAITING_FOR_HEADER)
        {
            writebuf = discard_master_wait_gtid_result(writebuf);
        }

        if (m_wait_gtid == UPDATING_PACKETS && writebuf)
        {
            correct_packet_sequence(writebuf);
        }
    }

    return writebuf;
}

void RWSplitSession::trx_replay_next_stmt()
{
    if (m_replayed_trx.have_stmts())
    {
        // More statements to replay, pop the oldest one and execute it
        GWBUF* buf = m_replayed_trx.pop_stmt();
        MXS_INFO("Replaying: %s", mxs::extract_sql(buf, 1024).c_str());
        retry_query(buf, 0);
    }
    else
    {
        // No more statements to execute
        m_is_replay_active = false;
        mxb::atomic::add(&m_router->stats().n_trx_replay, 1, mxb::atomic::RELAXED);

        if (!m_replayed_trx.empty())
        {
            // Check that the checksums match.
            SHA1Checksum chksum = m_trx.checksum();
            chksum.finalize();

            if (chksum == m_replayed_trx.checksum())
            {
                MXS_INFO("Checksums match, replay successful.");

                if (m_interrupted_query.get())
                {
                    MXS_INFO("Resuming execution: %s", mxs::extract_sql(m_interrupted_query.get()).c_str());
                    retry_query(m_interrupted_query.release(), 0);
                }
            }
            else
            {
                MXS_INFO("Checksum mismatch, transaction replay failed. Closing connection.");
                modutil_send_mysql_err_packet(m_client,
                                              0,
                                              0,
                                              1927,
                                              "08S01",
                                              "Transaction checksum mismatch encountered "
                                              "when replaying transaction.");
                poll_fake_hangup_event(m_client);
            }
        }
        else
        {
            /**
             * The transaction was "empty". This means that the start of the transaction
             * did not finish before we started the replay process.
             *
             * The transaction that is being currently replayed has a result,
             * whereas the original interrupted transaction had none. Due to this,
             * the checksums would not match if they were to be compared.
             */
            mxb_assert_message(!m_interrupted_query.get(), "Interrupted query should be empty");
        }
    }
}

void RWSplitSession::manage_transactions(RWBackend* backend, GWBUF* writebuf)
{
    if (m_otrx_state == OTRX_ROLLBACK)
    {
        /** This is the response to the ROLLBACK. If it fails, we must close
         * the connection. The replaying of the transaction can continue
         * regardless of the ROLLBACK result. */
        mxb_assert(backend == m_prev_target);

        if (!mxs_mysql_is_ok_packet(writebuf))
        {
            poll_fake_hangup_event(backend->dcb());
        }
    }
    else if (m_config.transaction_replay && m_can_replay_trx
             && session_trx_is_active(m_client->session))
    {
        if (!backend->has_session_commands())
        {
            /**
             * Session commands are tracked separately from the transaction.
             * We must not put any response to a session command into
             * the transaction as they are tracked separately.
             *
             * TODO: It might be wise to include the session commands to guarantee
             * that the session state during the transaction replay remains
             * consistent if the state change in the middle of the transaction
             * is intentional.
             */

            size_t size {m_trx.size() + m_current_query.length()};
            // A transaction is open and it is eligible for replaying
            if (size < m_config.trx_max_size)
            {
                /** Transaction size is OK, store the statement for replaying and
                 * update the checksum of the result */
                m_trx.add_result(writebuf);

                if (m_current_query.get())
                {
                    // TODO: Don't replay transactions interrupted mid-result. Currently
                    // the client will receive a `Packets out of order` error if this happens.

                    // Add the statement to the transaction once the first part
                    // of the result is received.
                    m_trx.add_stmt(m_current_query.release());
                }
            }
            else
            {
                MXS_INFO("Transaction is too big (%lu bytes), can't replay if it fails.", size);
                m_current_query.reset();
                m_trx.close();
                m_can_replay_trx = false;
            }
        }
    }
    else if (m_wait_gtid == RETRYING_ON_MASTER)
    {
        // We're retrying the query on the master and we need to keep the current query
    }
    else
    {
        /** Normal response, reset the currently active query. This is done before
         * the whole response is complete to prevent it from being retried
         * in case the connection breaks in the middle of a resultset. */
        m_current_query.reset();
    }
}

static bool server_is_shutting_down(GWBUF* writebuf)
{
    uint64_t err = mxs_mysql_get_mysql_errno(writebuf);
    return err == ER_SERVER_SHUTDOWN || err == ER_NORMAL_SHUTDOWN || err == ER_SHUTDOWN_COMPLETE;
}

void RWSplitSession::clientReply(GWBUF* writebuf, DCB* backend_dcb)
{
    DCB* client_dcb = backend_dcb->session->client_dcb;
    RWBackend* backend = get_backend_from_dcb(backend_dcb);

    if (backend->get_reply_state() == REPLY_STATE_DONE)
    {
        if (connection_was_killed(writebuf))
        {
            // The connection was killed, we can safely ignore it. When the TCP connection is
            // closed, the router's error handling will sort it out.
            gwbuf_free(writebuf);
        }
        else
        {
            /** If we receive an unexpected response from the server, the internal
             * logic cannot handle this situation. Routing the reply straight to
             * the client should be the safest thing to do at this point. */
            log_unexpected_response(backend, writebuf, m_current_query.get());
            MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
        }
        return;
    }
    else if (backend->get_reply_state() == REPLY_STATE_START && server_is_shutting_down(writebuf))
    {
        // The server is shutting down, ignore this error and wait for the TCP connection to die.
        // This allows the query to be retried on another server without the client noticing it.
        gwbuf_free(writebuf);
        return;
    }

    if ((writebuf = handle_causal_read_reply(writebuf, backend)) == NULL)
    {
        return;     // Nothing to route, return
    }

    // Track transaction contents and handle ROLLBACK with aggressive transaction load balancing
    manage_transactions(backend, writebuf);

    backend->process_reply(writebuf);

    if (backend->reply_is_complete())
    {
        /** Got a complete reply, decrement expected response count */
        m_expected_responses--;

        session_book_server_response(m_pSession, backend->backend()->server, m_expected_responses == 0);

        mxb_assert(m_expected_responses >= 0);
        mxb_assert(backend->get_reply_state() == REPLY_STATE_DONE);
        MXS_INFO("Reply complete, last reply from %s", backend->name());

        if (m_wait_gtid == RETRYING_ON_MASTER)
        {
            m_wait_gtid = NONE;

            // Discard the error
            gwbuf_free(writebuf);
            writebuf = NULL;

            // Retry the query on the master
            GWBUF* buf = m_current_query.release();
            buf->hint = hint_create_route(buf->hint, HINT_ROUTE_TO_MASTER, NULL);
            retry_query(buf, 0);

            // Stop the response processing early
            return;
        }

        ResponseStat& stat = backend->response_stat();
        stat.query_ended();
        if (stat.is_valid() && (stat.sync_time_reached()
                                || server_response_time_num_samples(backend->server()) == 0))
        {
            server_add_response_average(backend->server(),
                                        stat.average().secs(),
                                        stat.num_samples());
            stat.reset();
        }

        if (m_config.causal_reads)
        {
            // The reply should never be complete while we are still waiting for the header.
            mxb_assert(m_wait_gtid != WAITING_FOR_HEADER);
            m_wait_gtid = NONE;
        }

        if (backend->local_infile_requested())
        {
            // Server requested a local file, go into data streaming mode
            m_qc.set_load_data_state(QueryClassifier::LOAD_DATA_ACTIVE);
            session_set_load_active(m_pSession, true);
        }

        backend->select_ended();

        if (m_otrx_state == OTRX_ROLLBACK)
        {
            // Transaction rolled back, start replaying it on the master
            m_otrx_state = OTRX_INACTIVE;
            start_trx_replay();
            gwbuf_free(writebuf);
            session_reset_server_bookkeeping(m_pSession);
            return;
        }
    }
    else
    {
        MXS_INFO("Reply not yet complete. Waiting for %d replies, got one from %s",
                 m_expected_responses,
                 backend->name());
    }

    // Later on we need to know whether we processed a session command
    bool processed_sescmd = backend->has_session_commands();

    if (processed_sescmd)
    {
        /** Process the reply to an executed session command. This function can
         * close the backend if it's a slave. */
        process_sescmd_response(backend, &writebuf);
    }
    else if (m_is_replay_active)
    {
        mxb_assert(m_config.transaction_replay);

        if (m_expected_responses == 0)
        {
            // Current statement is complete, continue with the next one
            trx_replay_next_stmt();
        }

        /**
         * If the start of the transaction was interrupted, we need to return
         * the result to the client.
         *
         * This retrying of START TRANSACTION is done with the transaction replay
         * mechanism instead of the normal query retry mechanism because the safeguards
         * in the routing logic prevent retrying of individual queries inside transactions.
         *
         * If the transaction was not empty and some results have already been
         * sent to the client, we must discard all responses that the client already has.
         */

        if (!m_replayed_trx.empty())
        {
            // Client already has this response, discard it
            gwbuf_free(writebuf);
            return;
        }
    }
    else if (m_config.transaction_replay && session_trx_is_ending(m_client->session))
    {
        MXS_INFO("Transaction complete");
        m_trx.close();
        m_can_replay_trx = true;
    }

    if (backend->in_use() && backend->has_session_commands())
    {
        // Backend is still in use and has more session commands to execute
        if (backend->execute_session_command() && backend->is_waiting_result())
        {
            m_expected_responses++;
        }
    }
    else if (m_expected_responses == 0 && m_query_queue
             && (!m_is_replay_active || processed_sescmd))
    {
        /**
         * All replies received, route any stored queries. This should be done
         * even when transaction replay is active as long as we just completed
         * a session command.
         */
        route_stored_query();
    }

    if (writebuf)
    {
        mxb_assert(client_dcb);
        mxb_assert_message(backend->in_use(), "Backend should be in use when routing reply");
        /** Write reply to client DCB */
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    }
}

void check_and_log_backend_state(const RWBackend* backend, DCB* problem_dcb)
{
    if (backend)
    {
        /** This is a valid DCB for a backend ref */
        if (backend->in_use() && backend->dcb() == problem_dcb)
        {
            MXS_ERROR("Backend '%s' is still in use and points to the problem DCB.",
                      backend->name());
            mxb_assert(false);
        }
    }
    else
    {
        const char* remote = problem_dcb->state == DCB_STATE_POLLING
            && problem_dcb->server ? problem_dcb->server->name : "CLOSED";

        MXS_ERROR("DCB connected to '%s' is not in use by the router "
                  "session, not closing it. DCB is in state '%s'",
                  remote,
                  STRDCBSTATE(problem_dcb->state));
    }
}

bool RWSplitSession::start_trx_replay()
{
    bool rval = false;

    if (m_config.transaction_replay && m_can_replay_trx)
    {
        if (!m_is_replay_active)
        {
            // This is the first time we're retrying this transaction, store it and the interrupted query
            m_orig_trx = m_trx;
            m_orig_stmt.copy_from(m_current_query);
        }
        else
        {
            // Not the first time, copy the original
            m_replayed_trx.close();
            m_trx.close();
            m_trx = m_orig_trx;
            m_current_query.copy_from(m_orig_stmt);
        }

        if (m_trx.have_stmts() || m_current_query.get())
        {
            // Stash any interrupted queries while we replay the transaction
            m_interrupted_query.reset(m_current_query.release());

            MXS_INFO("Starting transaction replay");
            m_is_replay_active = true;

            /**
             * Copy the transaction for replaying and finalize it. This
             * allows the checksums to be compared. The current transaction
             * is closed as the replaying opens a new transaction.
             */
            m_replayed_trx = m_trx;
            m_replayed_trx.finalize();
            m_trx.close();

            if (m_replayed_trx.have_stmts())
            {
                // Pop the first statement and start replaying the transaction
                GWBUF* buf = m_replayed_trx.pop_stmt();
                MXS_INFO("Replaying: %s", mxs::extract_sql(buf, 1024).c_str());
                retry_query(buf, 1);
            }
            else
            {
                /**
                 * The transaction was only opened and no queries have been
                 * executed. The buffer should contain a query that starts
                 * a transaction.
                 */
                mxb_assert_message(qc_get_trx_type_mask(m_interrupted_query.get()) & QUERY_TYPE_BEGIN_TRX,
                                   "The current query should start a transaction");
                MXS_INFO("Retrying interrupted query: %s",
                         mxs::extract_sql(m_interrupted_query.get()).c_str());
                retry_query(m_interrupted_query.release(), 1);
            }
        }
        else
        {
            mxb_assert_message(!session_is_autocommit(m_client->session)
                               || session_trx_is_ending(m_client->session),
                               "Session should have autocommit disabled or transaction just ended if the "
                               "transaction had no statements and no query was interrupted");
        }

        rval = true;
    }

    return rval;
}

/**
 * @brief Router error handling routine
 *
 * Error Handler routine to resolve backend failures. If it succeeds then
 * there are enough operative backends available and connected. Otherwise it
 * fails, and session is terminated.
 *
 * @param instance       The router instance
 * @param router_session The router session
 * @param errmsgbuf      The error message to reply
 * @param backend_dcb    The backend DCB
 * @param action         The action: ERRACT_NEW_CONNECTION or
 *                       ERRACT_REPLY_CLIENT
 * @param succp          Result of action: true if router can continue
 */
void RWSplitSession::handleError(GWBUF* errmsgbuf,
                                 DCB*   problem_dcb,
                                 mxs_error_action_t action,
                                 bool* succp)
{
    mxb_assert(problem_dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    MXS_SESSION* session = problem_dcb->session;
    mxb_assert(session);

    RWBackend* backend = get_backend_from_dcb(problem_dcb);
    mxb_assert(backend->in_use());

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        {
            bool can_continue = false;

            if (m_current_master && m_current_master->in_use() && m_current_master == backend)
            {
                MXS_INFO("Master '%s' failed", backend->name());
                /** The connection to the master has failed */

                if (!backend->is_waiting_result())
                {
                    /** The failure of a master is not considered a critical
                     * failure as partial functionality still remains. If
                     * master_failure_mode is not set to fail_instantly, reads
                     * are allowed as long as slave servers are available
                     * and writes will cause an error to be returned.
                     *
                     * If we were waiting for a response from the master, we
                     * can't be sure whether it was executed or not. In this
                     * case the safest thing to do is to close the client
                     * connection. */
                    if (m_config.master_failure_mode != RW_FAIL_INSTANTLY)
                    {
                        can_continue = true;
                    }
                }
                else
                {
                    // We were expecting a response but we aren't going to get one
                    mxb_assert(m_expected_responses > 0);
                    m_expected_responses--;

                    if (can_retry_query())
                    {
                        can_continue = true;
                        retry_query(m_current_query.release());
                    }
                    else if (m_config.master_failure_mode == RW_ERROR_ON_WRITE)
                    {
                        /** In error_on_write mode, the session can continue even
                         * if the master is lost. Send a read-only error to
                         * the client to let it know that the query failed. */
                        can_continue = true;
                        send_readonly_error(m_client);
                    }
                }

                if (session_trx_is_active(session) && m_otrx_state == OTRX_INACTIVE)
                {
                    can_continue = start_trx_replay();
                }

                if (!can_continue)
                {
                    if (!backend->is_master() && !backend->server()->master_err_is_logged)
                    {
                        MXS_ERROR("Server %s (%s) lost the master status while waiting"
                                  " for a result. Client sessions will be closed.",
                                  backend->name(),
                                  backend->uri());
                        backend->server()->master_err_is_logged = true;
                    }
                    else
                    {
                        MXS_ERROR("Lost connection to the master server, closing session.");
                    }
                }

                backend->close();
            }
            else
            {
                MXS_INFO("Slave '%s' failed", backend->name());
                if (m_target_node && m_target_node == backend
                    && session_trx_is_read_only(problem_dcb->session))
                {
                    // We're no longer locked to this server as it failed
                    m_target_node = nullptr;

                    // Try to replay the transaction on another node
                    can_continue = start_trx_replay();
                    backend->close();

                    if (!can_continue)
                    {
                        MXS_ERROR("Connection to server %s failed while executing a read-only transaction",
                                  backend->name());
                    }
                }
                else if (m_otrx_state != OTRX_INACTIVE)
                {
                    /**
                     * The connection was closed mid-transaction or while we were
                     * executing the ROLLBACK. In both cases the transaction will
                     * be closed. We can safely start retrying the transaction
                     * on the master.
                     */

                    mxb_assert(session_trx_is_active(session));
                    m_otrx_state = OTRX_INACTIVE;
                    can_continue = start_trx_replay();
                    backend->close();
                }
                else
                {
                    /** Try to replace the failed connection with a new one */
                    can_continue = handle_error_new_connection(problem_dcb, errmsgbuf);
                }
            }

            *succp = can_continue;
            check_and_log_backend_state(backend, problem_dcb);
            break;
        }

    case ERRACT_REPLY_CLIENT:
        {
            handle_error_reply_client(problem_dcb, errmsgbuf);
            *succp = false;     /*< no new backend servers were made available */
            break;
        }

    default:
        mxb_assert(!true);
        *succp = false;
        break;
    }
}

/**
 * Check if there is backend reference pointing at failed DCB, and reset its
 * flags. Then clear DCB's callback and finally : try to find replacement(s)
 * for failed slave(s).
 *
 * This must be called with router lock.
 *
 * @param inst      router instance
 * @param rses      router client session
 * @param dcb       failed DCB
 * @param errmsg    error message which is sent to client if it is waiting
 *
 * @return true if there are enough backend connections to continue, false if
 * not
 */
bool RWSplitSession::handle_error_new_connection(DCB* backend_dcb, GWBUF* errmsg)
{
    RWBackend* backend = get_backend_from_dcb(backend_dcb);
    MXS_SESSION* ses = backend_dcb->session;
    bool route_stored = false;

    if (backend->is_waiting_result())
    {
        mxb_assert(m_expected_responses > 0);
        m_expected_responses--;

        /**
         * A query was sent through the backend and it is waiting for a reply.
         * Try to reroute the statement to a working server or send an error
         * to the client.
         */
        GWBUF* stored = m_current_query.release();

        if (stored && m_config.retry_failed_reads)
        {
            MXS_INFO("Re-routing failed read after server '%s' failed", backend->name());
            retry_query(stored, 0);
        }
        else
        {
            gwbuf_free(stored);

            if (!backend->has_session_commands())
            {
                /** The backend was not executing a session command so the client
                 * is expecting a response. Send an error so they know to proceed. */
                m_client->func.write(m_client, gwbuf_clone(errmsg));
            }

            if (m_expected_responses == 0)
            {
                // This was the last response, try to route pending queries
                route_stored = true;
            }
        }
    }

    /** Close the current connection. This needs to be done before routing any
     * of the stored queries. If we route a stored query before the connection
     * is closed, it's possible that the routing logic will pick the failed
     * server as the target. */
    backend->close();

    if (route_stored)
    {
        route_stored_query();
    }

    bool succp = false;
    /**
     * Try to get replacement slave or at least the minimum
     * number of slave connections for router session.
     */
    if (m_recv_sescmd > 0 && m_config.disable_sescmd_history)
    {
        for (const auto& a : m_backends)
        {
            if (a->in_use())
            {
                succp = true;
                break;
            }
        }

        if (!succp)
        {
            MXS_ERROR("Unable to continue session as all connections have failed, "
                      "last server to fail was '%s'.", backend->name());
        }
    }
    else
    {
        succp = m_router->select_connect_backend_servers(ses,
                                                         m_backends,
                                                         &m_current_master,
                                                         &m_sescmd_list,
                                                         &m_expected_responses,
                                                         connection_type::SLAVE);
    }

    return succp;
}

/**
 * @brief Handle an error reply for a client
 *
 * @param ses           Session
 * @param rses          Router session
 * @param backend_dcb   DCB for the backend server that has failed
 * @param errmsg        GWBUF containing the error message
 */
void RWSplitSession::handle_error_reply_client(DCB* backend_dcb, GWBUF* errmsg)
{
    mxs_session_state_t sesstate = m_client->session->state;
    RWBackend* backend = get_backend_from_dcb(backend_dcb);

    backend->close();

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        m_client->func.write(m_client, gwbuf_clone(errmsg));
    }
    else
    {
        MXS_INFO("Closing router session that is not ready");
    }
}

bool RWSplitSession::lock_to_master()
{
    bool rv = false;

    if (m_current_master && m_current_master->in_use())
    {
        m_target_node = m_current_master;
        rv = true;
    }

    return rv;
}

bool RWSplitSession::is_locked_to_master() const
{
    return m_current_master && m_target_node == m_current_master;
}

bool RWSplitSession::supports_hint(HINT_TYPE hint_type) const
{
    bool rv = true;

    switch (hint_type)
    {
    case HINT_ROUTE_TO_MASTER:
    case HINT_ROUTE_TO_SLAVE:
    case HINT_ROUTE_TO_NAMED_SERVER:
    case HINT_ROUTE_TO_LAST_USED:
    case HINT_PARAMETER:
        break;

    case HINT_ROUTE_TO_UPTODATE_SERVER:
    case HINT_ROUTE_TO_ALL:
        mxb_assert(!true);
        rv = false;
        break;

    default:
        mxb_assert(!true);
        rv = false;
    }

    return rv;
}
