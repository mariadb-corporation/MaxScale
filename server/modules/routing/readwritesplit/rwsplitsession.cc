/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"

#include <cmath>
#include <mysqld_error.h>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/clock.h>

using namespace maxscale;
using namespace std::chrono;

RWSplitSession::RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::SRWBackends backends)
    : mxs::RouterSession(session)
    , m_backends(std::move(backends))
    , m_raw_backends(sptr_vec_to_ptr_vec(m_backends))
    , m_current_master(nullptr)
    , m_target_node(nullptr)
    , m_prev_target(nullptr)
    , m_config(instance->config())
    , m_session(session)
    , m_sescmd_count(1)
    , m_expected_responses(0)
    , m_last_keepalive_check(maxbase::Clock::now(maxbase::NowType::EPollTick))
    , m_router(instance)
    , m_sent_sescmd(0)
    , m_recv_sescmd(0)
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
        n_conn = MXS_MAX(floor((double)m_backends.size() * pct), 1);
        m_config.max_slave_connections = n_conn;
    }
}

RWSplitSession* RWSplitSession::create(RWSplit* router, MXS_SESSION* session, const Endpoints& endpoints)
{
    RWSplitSession* rses = NULL;

    if (router->have_enough_servers())
    {
        SRWBackends backends = RWBackend::from_endpoints(endpoints);

        if ((rses = new(std::nothrow) RWSplitSession(router, session, std::move(backends))))
        {
            if (rses->open_connections())
            {
                mxb::atomic::add(&router->stats().n_sessions, 1, mxb::atomic::RELAXED);
            }
            else
            {
                delete rses;
                rses = nullptr;
            }
        }
    }
    else
    {
        MXS_ERROR("Service has no servers.");
    }

    return rses;
}

void RWSplitSession::close()
{
    m_current_query.reset();

    for (auto& backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            backend->close();
        }

        m_server_stats[backend->target()].update(backend->session_timer().split(),
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

    mxb_assert(gwbuf_is_contiguous(querybuf));
    int rval = 0;

    if (m_is_replay_active && !gwbuf_is_replayed(querybuf))
    {
        MXS_INFO("New %s received while transaction replay is active: %s",
                 STRPACKETTYPE(GWBUF_DATA(querybuf)[4]),
                 mxs::extract_sql(querybuf).c_str());
        m_query_queue.emplace_back(querybuf);
        return 1;
    }

    if ((m_query_queue.empty() || gwbuf_is_replayed(querybuf)) && can_route_queries())
    {
        /** Gather the information required to make routing decisions */
        if (!m_qc.large_query())
        {
            if (m_qc.load_data_state() == QueryClassifier::LOAD_DATA_INACTIVE
                && session_is_load_active(m_session))
            {
                m_qc.set_load_data_state(QueryClassifier::LOAD_DATA_ACTIVE);
            }

            m_qc.update_route_info(get_current_target(), querybuf);
        }

        /** No active or pending queries */
        if (route_stmt(querybuf))
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
        mxb_assert(m_expected_responses == 1 || !m_query_queue.empty());
        mxb_assert(!gwbuf_is_replayed(querybuf));

        m_query_queue.emplace_back(querybuf);
        rval = 1;
        mxb_assert(m_expected_responses == 1);
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
                GWBUF* buf = modutil_create_mysql_err_msg(1, 0, 1927, "08S01",
                                                          "Transaction checksum mismatch encountered "
                                                          "when replaying transaction.");

                m_session->kill(buf);

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

void RWSplitSession::manage_transactions(RWBackend* backend, GWBUF* writebuf, const mxs::Reply& reply)
{
    if (m_otrx_state == OTRX_ROLLBACK)
    {
        /** This is the response to the ROLLBACK. If it fails, we must close
         * the connection. The replaying of the transaction can continue
         * regardless of the ROLLBACK result. */
        mxb_assert(backend == m_prev_target);

        if (!mxs_mysql_is_ok_packet(writebuf))
        {
            m_session->kill();
        }
    }
    else if (m_config.transaction_replay && m_can_replay_trx && trx_is_open())
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
                    // Add the statement to the transaction once the first part of the result is received.
                    m_trx.add_stmt(backend, m_current_query.release());
                }
            }
            else
            {
                // We leave the transaction open to retain the information where it was being executed. This
                // is needed in case the server where it's being executed on fails.
                MXS_INFO("Transaction is too big (%lu bytes), can't replay if it fails.", size);
                m_can_replay_trx = false;
            }
        }
    }
    else if (m_wait_gtid == RETRYING_ON_MASTER)
    {
        // We're retrying the query on the master and we need to keep the current query
    }
    else if (!backend->has_session_commands())
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
            auto server = backend->target();

            if (!server->is_usable())
            {
                if (backend == m_current_master
                    && can_continue_using_master(m_current_master)
                    && !trx_is_ending())
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
                         backend->name(), backend->target()->rank(), current_rank);
                backend->close();
            }
        }
    }
}

bool is_wsrep_error(const mxs::Error& error)
{
    return error.code() == 1047 && error.sql_state() == "08S01"
           && error.message() == "WSREP has not yet prepared node for application use";
}

bool RWSplitSession::handle_ignorable_error(RWBackend* backend, const mxs::Error& error)
{
    if (backend->has_session_commands())
    {
        // Never bypass errors for session commands. TODO: Check whether it would make sense to do so.
        return false;
    }

    mxb_assert(trx_is_open() || can_retry_query());
    mxb_assert(m_expected_responses == 1);

    bool ok = false;

    MXS_INFO("%s: %s", error.is_rollback() ?
             "Server triggered transaction rollback, replaying transaction" :
             "WSREP not ready, retrying query", error.message().c_str());

    if (trx_is_open())
    {
        ok = start_trx_replay();
    }
    else
    {
        static bool warn_unexpected_rollback = true;

        if (!is_wsrep_error(error) && warn_unexpected_rollback)
        {
            MXS_WARNING("Expected a WSREP error but got a transaction rollback error: %d, %s",
                        error.code(), error.message().c_str());
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
        backend->ack_write();
        m_expected_responses--;
        session_reset_server_bookkeeping(m_pSession);
    }

    return ok;
}

void RWSplitSession::finish_transaction(mxs::RWBackend* backend)
{
    MXS_INFO("Transaction complete");
    m_trx.close();
    m_can_replay_trx = true;

    if (m_target_node && trx_is_read_only())
    {
        // Read-only transaction is over, stop routing queries to a specific node
        m_target_node = nullptr;
    }
}

void RWSplitSession::clientReply(GWBUF* writebuf, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    RWBackend* backend = static_cast<RWBackend*>(down.back()->get_userdata());

    if (!backend->has_session_commands())
    {
        if ((writebuf = handle_causal_read_reply(writebuf, reply, backend)) == NULL)
        {
            return;     // Nothing to route, return
        }
    }

    const auto& error = reply.error();

    if (error.is_unexpected_error())
    {
        if (error.code() == ER_CONNECTION_KILLED)
        {
            // The connection was killed, we can safely ignore it. When the TCP connection is
            // closed, the router's error handling will sort it out.
            backend->set_close_reason("Connection was killed");
        }
        else
        {
            // All other unexpected errors are related to server shutdown.
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

    if (((m_config.trx_retry_on_deadlock && error.is_rollback()) || is_wsrep_error(error))
        && handle_ignorable_error(backend, error))
    {
        // We can ignore this error and treat it as if the connection to the server was broken.
        gwbuf_free(writebuf);
        return;
    }

    // Track transaction contents and handle ROLLBACK with aggressive transaction load balancing
    manage_transactions(backend, writebuf, reply);

    if (reply.is_complete())
    {
        MXS_INFO("Reply complete, last reply from %s", backend->name());
        backend->ack_write();

        /** Got a complete reply, decrement expected response count */
        if (!backend->has_session_commands())
        {
            m_expected_responses--;
            mxb_assert(m_expected_responses == 0);

            if (!session_is_load_active(m_pSession))
            {
                // TODO: This would make more sense if it was done at the client protocol level
                session_book_server_response(m_pSession, (SERVER*)backend->target(), true);
            }

            if (!finish_causal_read())
            {
                // The query timed out on the slave, retry it on the master
                gwbuf_free(writebuf);
                return;
            }
        }

        mxb_assert(m_expected_responses >= 0);

        backend->select_finished();

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
                 m_expected_responses, backend->name());
    }

    // Later on we need to know whether we processed a session command
    bool processed_sescmd = backend->has_session_commands();

    if (processed_sescmd)
    {
        /** Process the reply to an executed session command. This function can
         * close the backend if it's a slave. */
        process_sescmd_response(backend, &writebuf, reply);
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
    else if (m_config.transaction_replay && trx_is_ending())
    {
        finish_transaction(backend);
    }

    if (writebuf)
    {
        mxb_assert_message(backend->in_use(), "Backend should be in use when routing reply");
        /** Write reply to client DCB */
        RouterSession::clientReply(writebuf, down, reply);
    }

    if (reply.is_complete())
    {
        execute_queued_commands(backend, processed_sescmd);
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

void RWSplitSession::execute_queued_commands(mxs::RWBackend* backend, bool processed_sescmd)
{
    mxb_assert(!backend->in_use() || !backend->is_waiting_result());

    while (backend->in_use() && backend->has_session_commands() && !backend->is_waiting_result())
    {
        if (backend->execute_session_command())
        {
            MXS_INFO("%lu session commands left on '%s'", backend->session_command_count(), backend->name());
        }
        else
        {
            MXS_INFO("Failed to execute session command on '%s'", backend->name());
            backend->close();
        }
    }

    if (backend->in_use() && backend->is_waiting_result())
    {
        // Backend is still in use and it executed something. Wait for the result before
        // routing queued queries.
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
                                                   return gwbuf_is_replayed(b.get());
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
            mxb_assert_message(!m_session->is_autocommit() || trx_is_ending(),
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

        mxb_assert(m_sescmd_replier == backend);
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

        if (!backend->is_replaying_history())
        {
            for (auto b : m_raw_backends)
            {
                if (b != backend && b->in_use() && b->is_waiting_result())
                {
                    MXS_INFO("Master failed, electing '%s' as the replier to session command %lu",
                             b->name(), b->next_session_command()->get_position());
                    m_sescmd_replier = b;
                    m_expected_responses++;
                    break;
                }
            }
        }

        if (m_sescmd_replier == backend)
        {
            // All of the slaves delivered their response before the master failed. This means that we don't
            // have the result of the session command available and to get it we have to execute it again.
            // This could be avoided if one of the slave responses was stored up until the master returned its
            // response.

            // Before routing it, pop the failed session command off the list and decrement the number of
            // executed session commands. This "overwrites" the existing command and prevents history
            // duplication.
            GWBUF* buffer = m_sescmd_list.back()->deep_copy_buffer();
            m_sescmd_list.pop_back();
            --m_sescmd_count;
            retry_query(buffer);

            MXS_INFO("Master failed, retrying session command %lu",
                     backend->next_session_command()->get_position());
        }

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

bool RWSplitSession::handleError(mxs::ErrorType type, GWBUF* errmsgbuf, mxs::Endpoint* endpoint,
                                 const mxs::Reply& reply)
{
    RWBackend* backend = static_cast<RWBackend*>(endpoint->get_userdata());
    mxb_assert(backend && backend->in_use());

    if (reply.has_started())
    {
        MXS_ERROR("Server '%s' was lost in the middle of a resultset, cannot continue the session: %s",
                  backend->name(), mxs::extract_error(errmsgbuf).c_str());

        // This effectively causes an instant termination of the client connection and prevents any errors
        // from being sent to the client (MXS-2562).
        m_session->kill();
        return false;
    }

    auto failure_type = type == mxs::ErrorType::PERMANENT ? RWBackend::CLOSE_FATAL : RWBackend::CLOSE_NORMAL;

    std::string errmsg;
    bool can_continue = false;

    if (m_current_master && m_current_master->in_use() && m_current_master == backend)
    {
        MXS_INFO("Master '%s' failed: %s", backend->name(), mxs::extract_error(errmsgbuf).c_str());
        /** The connection to the master has failed */

        bool expected_response = backend->is_waiting_result();

        if (!expected_response)
        {
            // We have to use Backend::is_waiting_result as the check since it's updated immediately after a
            // write to the backend is done. The mxs::Reply is updated only when the backend protocol
            // processes the query which can be out of sync when handleError is called if the disconnection
            // happens before authentication completes.
            mxb_assert(reply.is_complete());

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
            if (backend->is_replaying_history() && m_expected_responses == 0)
            {
                // This can happen if we're replaying an idle transaction and the connection fails either when
                // it is trying to connect or if it's during the session command replay.
                //
                // In 2.6 this will be automatically detected by the explicit response states of the backends.
                // There we can check the response stack to see if we eventually expected to see a result from
                // the backend. Sadly in 2.5 this information is only implied by the combination of
                // is_replaying_history() and m_expected_responses.
                expected_response = false;
            }
            else
            {
                // We were expecting a response but we aren't going to get one
                mxb_assert(m_expected_responses == 1);
            }

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
                send_readonly_error();
            }
        }

        // If we have an open transaction, the replay can be done immediately. If there's no open transaction
        // but a replay is in progress, we must still retry the replay. The target can be null if a replay
        // fails during the reconnetion to the master.
        if ((trx_is_open() || m_is_replay_active)
            && m_otrx_state == OTRX_INACTIVE
            && (!m_trx.target() || m_trx.target() == backend))
        {
            can_continue = start_trx_replay();
            errmsg += " A transaction is active and cannot be replayed.";
        }

        if (!can_continue)
        {

            int idle = duration_cast<seconds>(
                maxbase::Clock::now(maxbase::NowType::EPollTick) - backend->last_write()).count();
            MXS_ERROR("Lost connection to the master server, closing session.%s "
                      "Connection has been idle for %d seconds. Error caused by: %s. "
                      "Last close reason: %s. Last error: %s", errmsg.c_str(), idle,
                      mxs::extract_error(errmsgbuf).c_str(),
                      backend->close_reason().empty() ? "<none>" : backend->close_reason().c_str(),
                      reply.error().message().c_str());
        }

        // Decrement the expected response count only if we know we can continue the sesssion.
        // This keeps the internal logic sound even if another query is routed before the session
        // is closed.
        if (can_continue && expected_response)
        {
            m_expected_responses--;
        }

        backend->close(failure_type);
        backend->set_close_reason("Master connection failed: " + mxs::extract_error(errmsgbuf));
    }
    else
    {
        MXS_INFO("Slave '%s' failed: %s", backend->name(), mxs::extract_error(errmsgbuf).c_str());

        if (m_target_node && m_target_node == backend && trx_is_read_only())
        {
            mxb_assert(!m_config.transaction_replay || m_trx.target() == backend);

            if (backend->is_waiting_result())
            {
                mxb_assert(m_expected_responses == 1);
                m_expected_responses--;
            }

            // We're no longer locked to this server as it failed
            m_target_node = nullptr;

            // Try to replay the transaction on another node
            can_continue = start_trx_replay();
            backend->close(failure_type);
            backend->set_close_reason("Read-only trx failed: " + mxs::extract_error(errmsgbuf));

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

            if (backend->is_waiting_result())
            {
                mxb_assert(m_expected_responses == 1);
                m_expected_responses--;
            }

            mxb_assert(trx_is_open());
            m_otrx_state = OTRX_INACTIVE;
            can_continue = start_trx_replay();
            backend->close(failure_type);
            backend->set_close_reason("Optimistic trx failed: " + mxs::extract_error(errmsgbuf));
        }
        else
        {
            /** Try to replace the failed connection with a new one */
            can_continue = handle_error_new_connection(backend, errmsgbuf, failure_type);
        }
    }

    // We lost the connection, metadata needs to be sent again.
    for (auto& kv : m_exec_map)
    {
        kv.second.metadata_sent.erase(backend);
    }

    return can_continue;
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
bool RWSplitSession::handle_error_new_connection(RWBackend* backend, GWBUF* errmsg,
                                                 RWBackend::close_type failure_type)
{
    bool route_stored = false;

    if (backend->is_waiting_result())
    {
        // Route stored queries if this was the last server we expected a response from
        route_stored = m_expected_responses == 0;

        if (!backend->has_session_commands())
        {
            mxb_assert(m_expected_responses == 1);
            m_expected_responses--;

            // The backend was busy executing command and the client is expecting a response.
            if (m_current_query.get() && m_config.retry_failed_reads)
            {
                if (!m_config.delayed_retry && is_last_backend(backend))
                {
                    MXS_INFO("Cannot retry failed read as there are no candidates to "
                             "try it on and delayed_retry is not enabled");
                    return false;
                }

                MXS_INFO("Re-routing failed read after server '%s' failed", backend->name());
                route_stored = false;
                retry_query(m_current_query.release(), 0);
            }
            else
            {
                // Send an error so that the client knows to proceed.
                mxs::ReplyRoute route;
                RouterSession::clientReply(gwbuf_clone(errmsg), route, mxs::Reply());
                m_current_query.reset();
                route_stored = true;
            }
        }
    }

    /** Close the current connection. This needs to be done before routing any
     * of the stored queries. If we route a stored query before the connection
     * is closed, it's possible that the routing logic will pick the failed
     * server as the target. */
    backend->close(failure_type);
    backend->set_close_reason("Slave connection failed: " + mxs::extract_error(errmsg));

    if (route_stored)
    {
        route_stored_query();
    }

    bool ok = can_recover_servers() || have_open_connections();

    if (!ok)
    {
        MXS_ERROR("Unable to continue session as all connections have failed and "
                  "new connections cannot be created. Last server to fail was '%s'.",
                  backend->name());
        MXS_INFO("Connection status: %s", get_verbose_status().c_str());
    }

    return ok;
}

bool RWSplitSession::lock_to_master()
{
    bool rv = false;

    if (m_current_master && m_current_master->in_use())
    {
        m_target_node = m_current_master;
        rv = true;

        if (m_config.strict_multi_stmt || m_config.strict_sp_calls)
        {
            m_locked_to_master = true;
        }
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

/**
 * See if the current master is still a valid TARGET_MASTER candidate
 *
 * The master is valid if it's a master state or it is in maintenance mode while a transaction is open. If a
 * transaction is open to a master in maintenance mode, the connection is closed on the next COMMIT or
 * ROLLBACK.
 *
 * @see RWSplitSession::close_stale_connections()
 */
bool RWSplitSession::can_continue_using_master(const mxs::RWBackend* master)
{
    auto tgt = master->target();
    return tgt->is_master() || (master->in_use() && tgt->is_in_maint() && trx_is_open());
}

bool RWSplitSession::is_valid_for_master(const mxs::RWBackend* master)
{
    bool rval = false;

    if (master->in_use()
        || (m_config.master_reconnection && master->can_connect() && can_recover_servers()))
    {
        rval = master->target()->is_master()
            || (master->in_use() && master->target()->is_in_maint() && trx_is_open());
    }

    return rval;
}
