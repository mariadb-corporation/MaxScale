/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"

#include <cmath>

#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/clock.h>

using namespace maxscale;

RWSplitSession::RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::SRWBackends backends)
    : mxs::RouterSession(session)
    , m_backends(std::move(backends))
    , m_raw_backends(sptr_vec_to_ptr_vec(m_backends))
    , m_current_master(nullptr)
    , m_target_node(nullptr)
    , m_prev_target(nullptr)
    , m_config(instance->config())
    , m_last_keepalive_check(mxs_clock())
    , m_nbackends(instance->service()->n_dbref)
    , m_client(session->client_dcb)
    , m_sescmd_count(1)
    , m_expected_responses(0)
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
    , m_server_stats(instance->local_server_stats())
{
    if (m_config.rw_max_slave_conn_percent)
    {
        int n_conn = 0;
        double pct = (double)m_config.rw_max_slave_conn_percent / 100.0;
        n_conn = MXS_MAX(floor((double)m_nbackends * pct), 1);
        m_config.max_slave_connections = n_conn;
    }

    for (auto& b : m_raw_backends)
    {
        m_server_stats[b->server()].start_session();
    }
}

RWSplitSession* RWSplitSession::create(RWSplit* router, MXS_SESSION* session)
{
    RWSplitSession* rses = NULL;

    if (router->have_enough_servers())
    {
        SRWBackends backends = RWBackend::from_servers(router->service()->dbref);

        if ((rses = new(std::nothrow) RWSplitSession(router, session, std::move(backends))))
        {
            if (rses->open_connections())
            {
                router->stats().n_sessions += 1;
            }
            else
            {
                delete rses;
                rses = nullptr;
            }
        }
    }

    return rses;
}

void close_all_connections(PRWBackends& backends)
{
    for (auto& backend : backends)
    {
        if (backend->in_use())
        {
            backend->close();
        }
    }
}

void RWSplitSession::close()
{
    close_all_connections(m_raw_backends);
    m_current_query.reset();

    for (auto& backend : m_raw_backends)
    {
        ResponseStat& stat = backend->response_stat();

        if (stat.make_valid())
        {
            backend->server()->response_time_add(stat.average().secs(), stat.num_samples());
        }
        backend->response_stat().reset();

        m_server_stats[backend->server()].end_session(backend->session_timer().split(),
                                                      backend->select_timer().total(),
                                                      backend->num_selects());
    }
}

int32_t RWSplitSession::routeQuery(GWBUF* querybuf)
{
    if (!querybuf)
    {
        MXS_ERROR("MXS-2585: Null buffer passed to routeQuery, closing session");
        mxb_assert(!true);
        return 0;
    }

    mxb_assert(GWBUF_IS_CONTIGUOUS(querybuf));
    int rval = 0;

    if (m_is_replay_active && !GWBUF_IS_REPLAYED(querybuf))
    {
        MXS_INFO("New %s received while transaction replay is active: %s",
                 STRPACKETTYPE(GWBUF_DATA(querybuf)[4]),
                 mxs::extract_sql(querybuf).c_str());
        m_query_queue.emplace_back(querybuf);
        return 1;
    }

    if ((m_query_queue.empty() || GWBUF_IS_REPLAYED(querybuf)) && can_route_queries())
    {
        /** Gather the information required to make routing decisions */
        if (!m_qc.large_query())
        {
            m_qc.update_route_info(get_current_target(), querybuf);
        }

        /** No active or pending queries */
        if (route_single_stmt(querybuf))
        {
            rval = 1;
        }
    }
    else
    {
        // Already busy executing a query, put the query in a queue and route it later
        MXS_INFO("Storing query (len: %d cmd: %0x), expecting %d replies to current command: %s",
                 gwbuf_length(querybuf), GWBUF_DATA(querybuf)[4], m_expected_responses,
                 mxs::extract_sql(querybuf, 1024).c_str());
        mxb_assert(m_expected_responses > 0 || !m_query_queue.empty());

        m_query_queue.emplace_back(querybuf);
        querybuf = NULL;
        rval = 1;
        mxb_assert(m_expected_responses != 0);
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
    while (!m_query_queue.empty())
    {
        MXS_INFO(">>> Routing stored queries");
        auto query = std::move(m_query_queue.front());
        m_query_queue.pop_front();

        if (!query.get())
        {
            MXS_ALERT("MXS-2464: Query in query queue unexpectedly null. Queue has %lu queries left.",
                      m_query_queue.size());
            mxb_assert(!true);
            continue;
        }

        /** Store the query queue locally for the duration of the routeQuery call.
         * This prevents recursive calls into this function. */
        decltype(m_query_queue) temp_storage;
        temp_storage.swap(m_query_queue);

        // TODO: Move the handling of queued queries to the client protocol
        // TODO: module where the command tracking is done automatically.
        uint8_t cmd = mxs_mysql_get_command(query.get());
        mysql_protocol_set_current_command(m_client, (mxs_mysql_cmd_t)cmd);

        if (!routeQuery(query.release()))
        {
            rval = false;
            MXS_ERROR("Failed to route queued query.");
        }

        MXS_INFO("<<< Stored queries routed");

        if (m_query_queue.empty())
        {
            /** Query successfully routed and no responses are expected */
            m_query_queue.swap(temp_storage);
        }
        else
        {
            /**
             * Routing was stopped, we need to wait for a response before retrying.
             * temp_storage holds the tail end of the queue and m_query_queue contains the query we attempted
             * to route.
             */
            mxb_assert(m_query_queue.size() == 1);
            temp_storage.push_front(std::move(m_query_queue.front()));
            m_query_queue = std::move(temp_storage);
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
    mxb_assert(dcb->role == DCB::Role::BACKEND);

    for (auto it = m_raw_backends.begin(); it != m_raw_backends.end(); it++)
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
        session_dump_log(backend->dcb()->session);
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
        m_num_trx_replays = 0;

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
                else if (!m_query_queue.empty())
                {
                    route_stored_query();
                }
            }
            else
            {
                MXS_INFO("Checksum mismatch, transaction replay failed. Closing connection.");
                modutil_send_mysql_err_packet(m_client, 1, 0, 1927, "08S01",
                                              "Transaction checksum mismatch encountered "
                                              "when replaying transaction.");
                poll_fake_hangup_event(m_client);

                // Turn the replay flag back on to prevent queries from getting routed before the hangup we
                // just added is processed. For example, this can happen if the error is sent and the client
                // manages to send a COM_QUIT that gets processed before the fake hangup event.
                m_is_replay_active = true;
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

namespace
{

bool server_is_shutting_down(GWBUF* writebuf)
{
    uint64_t err = mxs_mysql_get_mysql_errno(writebuf);
    return err == ER_SERVER_SHUTDOWN || err == ER_NORMAL_SHUTDOWN || err == ER_SHUTDOWN_COMPLETE;
}

mxs::Buffer::iterator skip_packet(mxs::Buffer::iterator it)
{
    uint32_t len = *it++;
    len |= (*it++) << 8;
    len |= (*it++) << 16;
    it.advance(len + 1);    // Payload length plus the fourth header byte (packet sequence)
    return it;
}

GWBUF* erase_last_packet(GWBUF* input)
{
    mxs::Buffer buf(input);
    auto it = buf.begin();
    auto end = it;

    while ((end = skip_packet(it)) != buf.end())
    {
        it = end;
    }

    buf.erase(it, end);
    return buf.release();
}
}

void RWSplitSession::close_stale_connections()
{
    auto current_rank = get_current_rank();

    for (auto& backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            auto server = backend->server();

            if (!server->is_usable())
            {
                if (backend == m_current_master
                    && can_continue_using_master(m_current_master)
                    && !session_trx_is_ending(m_client->session))
                {
                    MXS_INFO("Keeping connection to '%s' open until transaction ends", backend->name());
                }
                else
                {
                    MXS_INFO("Discarding connection to '%s': Server is in maintenance", backend->name());
                    backend->close();
                }
            }
            else if (server->rank() != current_rank)
            {
                MXS_INFO("Discarding connection to '%s': Server has rank %ld and current rank is %ld",
                         backend->name(), backend->server()->rank(), current_rank);
                backend->close();
            }
        }
    }
}

bool RWSplitSession::handle_ignorable_error(RWBackend* backend)
{
    mxb_assert(session_trx_is_active(m_pSession) || can_retry_query());
    mxb_assert(m_expected_responses > 0);

    bool ok = false;

    MXS_INFO("%s: %s", backend->error().is_rollback() ?
             "Server triggered transaction rollback, replaying transaction" :
             "WSREP not ready, retrying query", backend->error().message().c_str());

    if (session_trx_is_active(m_pSession))
    {
        ok = start_trx_replay();
    }
    else
    {
        static bool warn_unexpected_rollback = true;

        if (!backend->error().is_wsrep_error() && warn_unexpected_rollback)
        {
            MXS_WARNING("Expected a WSREP error but got a transaction rollback error: %d, %s",
                        backend->error().code(), backend->error().message().c_str());
            warn_unexpected_rollback = false;
        }

        if (backend == m_current_master)
        {
            if (can_retry_query())
            {
                ok = retry_master_query(backend);
            }
        }
        else if (m_config.retry_failed_reads)
        {
            ok = true;
            retry_query(m_current_query.release());
        }
    }

    if (ok)
    {
        m_expected_responses--;
        session_reset_server_bookkeeping(m_pSession);
    }

    return ok;
}

void RWSplitSession::clientReply(GWBUF* writebuf, DCB* backend_dcb)
{
    DCB* client_dcb = backend_dcb->session->client_dcb;
    RWBackend* backend = get_backend_from_dcb(backend_dcb);

    if (backend->get_reply_state() == REPLY_STATE_DONE
        && !connection_was_killed(writebuf)
        && !server_is_shutting_down(writebuf))
    {
        /** If we receive an unexpected response from the server, the internal
         * logic cannot handle this situation. Routing the reply straight to
         * the client should be the safest thing to do at this point. */
        log_unexpected_response(backend, writebuf, m_current_query.get());

        if (!m_sescmd_list.empty())
        {
            auto cmd = m_sescmd_list.back()->get_command();
            auto query = m_sescmd_list.back()->to_string();
            MXS_ERROR("Latest session command: (%s) %s",
                      STRPACKETTYPE(cmd), query.empty() ? "<no query>" : query.c_str());
        }

        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
        return;
    }

    if ((writebuf = handle_causal_read_reply(writebuf, backend)) == NULL)
    {
        return;     // Nothing to route, return
    }

    backend->process_reply(writebuf);

    const RWBackend::Error& error = backend->error();

    if (error.is_unexpected_error())
    {
        // The connection was killed, we can safely ignore it. When the TCP connection is
        // closed, the router's error handling will sort it out.
        if (error.code() == ER_CONNECTION_KILLED)
        {
            backend->set_close_reason("Connection was killed");
        }
        else
        {
            mxb_assert(error.code() == ER_SERVER_SHUTDOWN
                       || error.code() == ER_NORMAL_SHUTDOWN
                       || error.code() == ER_SHUTDOWN_COMPLETE);
            backend->set_close_reason(std::string("Server '") + backend->name() + "' is shutting down");
        }

        // The server sent an error that we didn't expect: treat it as if the connection was closed. The
        // client shouldn't see this error as we can replace the closed connection.

        if (!(writebuf = erase_last_packet(writebuf)))
        {
            // Nothing to route to the client
            return;
        }
    }

    if (((m_config.trx_retry_on_deadlock && error.is_rollback()) || error.is_wsrep_error())
        && handle_ignorable_error(backend))
    {
        // We can ignore this error and treat it as if the connection to the server was broken.
        gwbuf_free(writebuf);
        return;
    }

    // Track transaction contents and handle ROLLBACK with aggressive transaction load balancing
    manage_transactions(backend, writebuf);

    if (backend->reply_is_complete())
    {
        /** Got a complete reply, decrement expected response count */
        m_expected_responses--;

        if (!backend->is_replaying_history() && !backend->local_infile_requested())
        {
            session_book_server_response(m_pSession, backend->backend()->server, m_expected_responses == 0);
        }

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
                                || backend->server()->response_time_num_samples() == 0))
        {
            backend->server()->response_time_add(stat.average().secs(), stat.num_samples());
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
            MXS_INFO("%lu session commands left on '%s'", backend->session_command_count(), backend->name());
            m_expected_responses++;
        }
    }
    else if (m_expected_responses == 0 && !m_query_queue.empty()
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

    if (m_expected_responses == 0)
    {
        /**
         * Close stale connections to servers in maintenance. Done here to avoid closing the connections
         * before all responses have been received.
         */
        close_stale_connections();
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
            && problem_dcb->server ? problem_dcb->server->name() : "CLOSED";

        MXS_ERROR("DCB connected to '%s' is not in use by the router "
                  "session, not closing it. DCB is in state '%s'",
                  remote,
                  STRDCBSTATE(problem_dcb->state));
    }
}

bool RWSplitSession::start_trx_replay()
{
    bool rval = false;

    if (m_config.transaction_replay && m_can_replay_trx && m_num_trx_replays < m_config.trx_max_attempts)
    {
        ++m_num_trx_replays;

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

            // Erase all replayed queries from the query queue to prevent checksum mismatches
            m_query_queue.erase(std::remove_if(m_query_queue.begin(), m_query_queue.end(), [](mxs::Buffer b) {
                                                   return GWBUF_IS_REPLAYED(b.get());
                                               }), m_query_queue.end());
        }

        if (m_trx.have_stmts() || m_current_query.get())
        {
            // Stash any interrupted queries while we replay the transaction
            m_interrupted_query.reset(m_current_query.release());

            MXS_INFO("Starting transaction replay %ld", m_num_trx_replays);
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
    else if (m_num_trx_replays >= m_config.trx_max_attempts)
    {
        mxb_assert(m_num_trx_replays == m_config.trx_max_attempts);
        MXS_INFO("Transaction replay attempt cap of %ld exceeded, not attempting replay",
                 m_config.trx_max_attempts);
    }

    return rval;
}

bool RWSplitSession::retry_master_query(RWBackend* backend)
{
    bool can_continue = false;

    if (backend->is_replaying_history() && !m_query_queue.empty())
    {
        // Master failed while it was replaying the session command history while a query was queued for
        // execution. Re-execute it to trigger a reconnection.
        mxb_assert(m_config.master_reconnection);

        retry_query(m_query_queue.front().release());
        m_query_queue.pop_front();
        can_continue = true;
    }
    else if (backend->has_session_commands())
    {
        // We were routing a session command to all servers but the master server from which the response
        // was expected failed: try to route the session command again. If the master is not available,
        // the response will be returned from one of the slaves if the configuration allows it.

        mxb_assert_message(backend->next_session_command()->get_position() == m_recv_sescmd + 1
                           || backend->is_replaying_history(),
                           "The master should be executing the latest session command "
                           "or attempting to replay existing history.");
        mxb_assert(m_qc.current_route_info().target() == TARGET_ALL);
        mxb_assert(!m_current_query.get());
        mxb_assert(!m_sescmd_list.empty());
        mxb_assert(m_sescmd_count >= 2);

        // MXS-2609: Maxscale crash in RWSplitSession::retry_master_query()
        // To prevent a crash from happening, we make sure the session command list is not empty before
        // we touch it. This should be converted into a debug assertion once the true root cause of the
        // problem is found.
        if (m_sescmd_count < 2 || m_sescmd_list.empty())
        {
            MXS_WARNING("Session command list was empty when it should not be");
            return false;
        }

        MXS_INFO("Retrying session command due to master failure: %s",
                 m_sescmd_list.back()->to_string().c_str());

        GWBUF* buffer = m_sescmd_list.back()->deep_copy_buffer();

        // Before routing it, pop the failed session command off the list and decrement the number of
        // executed session commands. This "overwrites" the existing command and prevents history duplication.
        m_sescmd_list.pop_back();
        --m_sescmd_count;

        retry_query(buffer);
        can_continue = true;
    }
    else if (m_current_query.get())
    {
        // A query was in progress, try to route it again
        mxb_assert(m_prev_target == backend);
        retry_query(m_current_query.release());
        can_continue = true;
    }
    else
    {
        // This should never happen
        mxb_assert_message(!true, "m_current_query is empty and no session commands being executed");
        MXS_ERROR("Current query unexpectedly empty when trying to retry query on master");
    }

    return can_continue;
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
                                 DCB* problem_dcb,
                                 mxs_error_action_t action,
                                 bool* succp)
{
    mxb_assert(problem_dcb->role == DCB::Role::BACKEND);
    MXS_SESSION* session = problem_dcb->session;
    mxb_assert(session);

    RWBackend* backend = get_backend_from_dcb(problem_dcb);
    mxb_assert(backend->in_use());

    if (backend->reply_has_started())
    {
        MXS_ERROR("Server '%s' was lost in the middle of a resultset, cannot continue the session: %s",
                  backend->name(), extract_error(errmsgbuf).c_str());

        // This effectively causes an instant termination of the client connection and prevents any errors
        // from being sent to the client (MXS-2562).
        dcb_close(m_client);
        *succp = true;
        return;
    }

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        {
            std::string errmsg;
            bool can_continue = false;

            if (m_current_master && m_current_master->in_use() && m_current_master == backend)
            {
                MXS_INFO("Master '%s' failed: %s", backend->name(), extract_error(errmsgbuf).c_str());
                /** The connection to the master has failed */

                bool expected_response = backend->is_waiting_result();

                if (!expected_response)
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
                    errmsg += " Lost connection to master server while connection was idle.";
                    if (m_config.master_failure_mode != RW_FAIL_INSTANTLY)
                    {
                        can_continue = true;
                    }
                }
                else
                {
                    // We were expecting a response but we aren't going to get one
                    mxb_assert(m_expected_responses > 0);
                    errmsg += " Lost connection to master server while waiting for a result.";

                    if (can_retry_query())
                    {
                        can_continue = retry_master_query(backend);
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
                    errmsg += " A transaction is active and cannot be replayed.";
                }

                if (!can_continue)
                {
                    int64_t idle = mxs_clock() - backend->dcb()->last_read;
                    MXS_ERROR("Lost connection to the master server '%s', closing session.%s "
                              "Connection has been idle for %.1f seconds. Error caused by: %s. "
                              "Last close reason: %s. Last error: %s",
                              backend->name(), errmsg.c_str(), (float)idle / 10.f,
                              extract_error(errmsgbuf).c_str(),
                              backend->close_reason().empty() ? "<none>" : backend->close_reason().c_str(),
                              backend->error().message().c_str());
                }

                // Decrement the expected response count only if we know we can continue the sesssion.
                // This keeps the internal logic sound even if another query is routed before the session
                // is closed.
                if (can_continue && expected_response)
                {
                    m_expected_responses--;
                }

                backend->close();
                backend->set_close_reason("Master connection failed: " + extract_error(errmsgbuf));
            }
            else
            {
                MXS_INFO("Slave '%s' failed: %s", backend->name(), extract_error(errmsgbuf).c_str());
                if (m_target_node && m_target_node == backend
                    && session_trx_is_read_only(problem_dcb->session))
                {
                    // We're no longer locked to this server as it failed
                    m_target_node = nullptr;

                    // Try to replay the transaction on another node
                    can_continue = start_trx_replay();
                    backend->close();
                    backend->set_close_reason("Read-only trx failed: " + extract_error(errmsgbuf));

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
                    backend->set_close_reason("Optimistic trx failed: " + extract_error(errmsgbuf));
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

        // Route stored queries if this was the last server we expected a response from
        route_stored = m_expected_responses == 0;

        if (!backend->has_session_commands())
        {
            // The backend was busy executing command and the client is expecting a response.
            if (m_current_query.get() && m_config.retry_failed_reads)
            {
                MXS_INFO("Re-routing failed read after server '%s' failed", backend->name());
                route_stored = false;
                retry_query(m_current_query.release(), 0);
            }
            else
            {
                // Send an error so that the client knows to proceed.
                m_client->func.write(m_client, gwbuf_clone(errmsg));
                m_current_query.reset();
            }
        }
    }

    /** Close the current connection. This needs to be done before routing any
     * of the stored queries. If we route a stored query before the connection
     * is closed, it's possible that the routing logic will pick the failed
     * server as the target. */
    backend->close();
    backend->set_close_reason("Slave connection failed: " + extract_error(errmsg));

    if (route_stored)
    {
        route_stored_query();
    }

    bool ok = can_recover_servers() || can_continue_session();

    if (!ok)
    {
        MXS_ERROR("Unable to continue session as all connections have failed and "
                  "new connections cannot be created. Last server to fail was '%s'.",
                  backend->name());
        MXS_INFO("Connection status: %s", get_verbose_status().c_str());
    }

    return ok;
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

    if (sesstate == SESSION_STATE_STARTED)
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

bool RWSplitSession::send_unknown_ps_error(uint32_t stmt_id)
{
    std::stringstream ss;
    ss << "Unknown prepared statement handler (" << stmt_id << ") given to MaxScale";
    GWBUF* err = modutil_create_mysql_err_msg(1, 0, ER_UNKNOWN_STMT_HANDLER, "HY000", ss.str().c_str());
    return m_client->func.write(m_client, err);
}
