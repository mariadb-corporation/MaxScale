/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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
#include <maxscale/protocol/mariadb/protocol_classes.hh>

using namespace maxscale;
using namespace std::chrono;
using mariadb::QueryClassifier;

RWSplitSession::RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::SRWBackends backends)
    : mxs::RouterSession(session)
    , m_backends(std::move(backends))
    , m_raw_backends(sptr_vec_to_ptr_vec(m_backends))
    , m_current_master(nullptr)
    , m_target_node(nullptr)
    , m_config(instance->config())
    , m_expected_responses(0)
    , m_router(instance)
    , m_wait_gtid(NONE)
    , m_next_seq(0)
    , m_qc(this, session, m_config.use_sql_variables_in)
    , m_retry_duration(0)
    , m_can_replay_trx(true)
    , m_server_stats(instance->local_server_stats())
{
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

RWSplitSession::~RWSplitSession()
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

    m_router->local_avg_sescmd_sz().add(protocol_data()->history.size());
}

bool RWSplitSession::routeQuery(GWBUF* querybuf)
{
    if (!querybuf)
    {
        MXS_ERROR("MXS-2585: Null buffer passed to routeQuery, closing session");
        mxb_assert(!true);
        return 0;
    }

    mxs::Buffer buffer(querybuf);
    mxb_assert(buffer.is_contiguous());

    int rval = 0;

    if (m_state == TRX_REPLAY && !gwbuf_is_replayed(buffer.get()))
    {
        MXS_INFO("New %s received while transaction replay is active: %s",
                 STRPACKETTYPE(buffer.data()[4]),
                 mxs::extract_sql(buffer).c_str());
        m_query_queue.emplace_back(std::move(buffer));
        return 1;
    }

    m_qc.update_route_info(get_current_target(), buffer.get());
    RoutingPlan res = resolve_route(buffer, route_info());

    if (can_route_query(buffer, res))
    {
        /** No active or pending queries */
        if (route_stmt(std::move(buffer), res))
        {
            rval = 1;
        }
    }
    else
    {
        // Roll back the query classifier state to keep it consistent.
        m_qc.revert_update();

        // Already busy executing a query, put the query in a queue and route it later
        MXS_INFO("Storing query (len: %lu cmd: %0x), expecting %d replies to current command: %s. "
                 "Would route %s to '%s'.",
                 buffer.length(), buffer.data()[4], m_expected_responses,
                 mxs::extract_sql(buffer, 1024).c_str(),
                 route_target_to_string(res.route_target),
                 res.target ? res.target->name() : "<no target>");

        mxb_assert(m_expected_responses >= 1 || !m_query_queue.empty());
        mxb_assert(!gwbuf_is_replayed(querybuf));

        m_query_queue.emplace_back(std::move(buffer));
        rval = 1;
        mxb_assert(m_expected_responses >= 1);
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
    if (m_query_queue.empty())
    {
        return true;
    }

    bool rval = true;

    /** Loop over the stored statements as long as the routeQuery call doesn't
     * append more data to the queue. If it appends data to the queue, we need
     * to wait for a response before attempting another reroute */
    MXS_INFO(">>> Routing stored queries");

    while (!m_query_queue.empty())
    {
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

    MXS_INFO("<<< Stored queries routed");

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
    mxb_assert(m_state == TRX_REPLAY);

    if (m_replayed_trx.have_stmts())
    {
        // More statements to replay, pop the oldest one and execute it
        GWBUF* buf = m_replayed_trx.pop_stmt();
        const char* cmd = STRPACKETTYPE(mxs_mysql_get_command(buf));
        MXS_INFO("Replaying %s: %s", cmd, mxs::extract_sql(buf, 1024).c_str());
        retry_query(buf, 0);
    }
    else
    {
        // No more statements to execute, return to normal routing mode
        m_state = ROUTING;
        mxb::atomic::add(&m_router->stats().n_trx_replay, 1, mxb::atomic::RELAXED);
        m_num_trx_replays = 0;

        if (!m_replayed_trx.empty())
        {
            // Check that the checksums match.
            SHA1Checksum chksum = m_trx.checksum();
            chksum.finalize();

            if (chksum == m_replayed_trx.checksum())
            {
                MXS_INFO("Checksums match, replay successful. Replay took %ld seconds.",
                         trx_replay_seconds());

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

                m_pSession->kill(buf);

                // Turn the replay flag back on to prevent queries from getting routed before the hangup we
                // just added is processed. For example, this can happen if the error is sent and the client
                // manages to send a COM_QUIT that gets processed before the fake hangup event.
                m_state = TRX_REPLAY;
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
    if (m_state == OTRX_ROLLBACK)
    {
        /** This is the response to the ROLLBACK. If it fails, we must close
         * the connection. The replaying of the transaction can continue
         * regardless of the ROLLBACK result. */
        mxb_assert(backend == m_prev_plan.target);

        if (!mxs_mysql_is_ok_packet(writebuf))
        {
            m_pSession->kill();
        }
    }
    else if (m_config.transaction_replay && m_can_replay_trx && trx_is_open())
    {
        // Never add something we should ignore into the transaction. The checksum is calculated from the
        // response that is sent upstream via clientReply and this is implied by `should_ignore_response()`
        // being false.
        if (!backend->should_ignore_response())
        {
            int64_t size = m_trx.size() + m_current_query.length();

            // A transaction is open and it is eligible for replaying
            if (size < m_config.trx_max_size)
            {
                /** Transaction size is OK, store the statement for replaying and
                 * update the checksum of the result */

                if (include_in_checksum(reply))
                {
                    m_trx.add_result(writebuf);
                }

                if (m_current_query.get())
                {
                    const char* cmd = STRPACKETTYPE(mxs_mysql_get_command(m_current_query.get()));
                    MXS_INFO("Adding %s to trx: %s", cmd, mxs::extract_sql(m_current_query, 512).c_str());

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
    else if (!backend->should_ignore_response())
    {
        /** Normal response, reset the currently active query. This is done before
         * the whole response is complete to prevent it from being retried
         * in case the connection breaks in the middle of a resultset. */
        m_current_query.reset();
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
    if (backend->should_ignore_response())
    {
        // Never bypass errors for session commands. TODO: Check whether it would make sense to do so.
        return false;
    }

    mxb_assert(trx_is_open() || can_retry_query());
    mxb_assert(m_expected_responses >= 1);

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

        if (m_expected_responses > 1)
        {
            MXS_INFO("Cannot retry the query as multiple queries were in progress");
        }
        else if (backend == m_current_master)
        {
            if (can_retry_query() && can_recover_master())
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
        m_pSession->reset_server_bookkeeping();
        backend->close();
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

bool RWSplitSession::clientReply(GWBUF* writebuf, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    RWBackend* backend = static_cast<RWBackend*>(down.back()->get_userdata());

    if (!backend->should_ignore_response())
    {
        if ((writebuf = handle_causal_read_reply(writebuf, reply, backend)) == NULL)
        {
            return 1;       // Nothing to route, return
        }
    }

    const auto& error = reply.error();

    if (error.is_unexpected_error())
    {
        // All unexpected errors are related to server shutdown.
        backend->set_close_reason(std::string("Server '") + backend->name() + "' is shutting down");

        // The server sent an error that we either didn't expect or we don't want. If retrying is going to
        // take place, it'll be done in handleError.
        if (!backend->is_waiting_result() || !reply.has_started())
        {
            // The buffer contains either an ERR packet, in which case the resultset hasn't started yet, or a
            // resultset with a trailing ERR packet. The full resultset can be discarded as the client hasn't
            // received it yet. In theory we could return this to the client but we don't know if it was
            // interrupted or not so the safer option is to retry it.
            gwbuf_free(writebuf);
            return 1;
        }
    }

    if (((m_config.trx_retry_on_deadlock && error.is_rollback()) || is_wsrep_error(error))
        && handle_ignorable_error(backend, error))
    {
        // We can ignore this error and treat it as if the connection to the server was broken.
        gwbuf_free(writebuf);
        return 1;
    }

    // TODO: Do this in the client protocol, it seems to be a pretty logical place for it as it already
    // assigns the prepared statement IDs.
    if (m_config.reuse_ps && reply.command() == MXS_COM_STMT_PREPARE)
    {
        if (m_current_query.get() && !backend->should_ignore_response())
        {
            std::string current_sql = mxs::extract_sql(m_current_query);
            m_ps_cache[current_sql].append(gwbuf_clone(writebuf));
        }
    }

    // Track transaction contents and handle ROLLBACK with aggressive transaction load balancing
    manage_transactions(backend, writebuf, reply);

    if (reply.is_complete())
    {
        if (backend->should_ignore_response())
        {
            MXS_INFO("Reply complete from '%s', discarding it.", backend->name());
            gwbuf_free(writebuf);
            writebuf = nullptr;
        }
        else
        {
            MXS_INFO("Reply complete from '%s' (%s)", backend->name(), reply.describe().c_str());
            /** Got a complete reply, decrement expected response count */
            m_expected_responses--;
            mxb_assert(m_expected_responses >= 0);

            constexpr const char* LEVEL = "SERIALIZABLE";

            if (reply.get_variable("trx_characteristics").find(LEVEL) != std::string::npos
                || reply.get_variable("tx_isolation").find(LEVEL) != std::string::npos)
            {
                MXS_INFO("Transaction isolation level set to %s, locking session to master", LEVEL);
                m_locked_to_master = true;
                lock_to_master();
            }

            if (reply.command() == MXS_COM_STMT_PREPARE && reply.is_ok())
            {
                m_qc.ps_store_response(reply.generated_id(), reply.param_count());
            }

            if (!finish_causal_read())
            {
                // The query timed out on the slave, retry it on the master
                gwbuf_free(writebuf);
                return 1;
            }

            if (m_state == OTRX_ROLLBACK)
            {
                // Transaction rolled back, start replaying it on the master
                m_state = ROUTING;
                start_trx_replay();
                gwbuf_free(writebuf);
                m_pSession->reset_server_bookkeeping();
                return 1;
            }
        }

        backend->ack_write();
        backend->select_finished();

        mxb_assert(m_expected_responses >= 0);
    }
    else if (backend->should_ignore_response())
    {
        MXS_INFO("Reply not yet complete from '%s', discarding partial result.", backend->name());
        gwbuf_free(writebuf);
        writebuf = nullptr;
    }
    else
    {
        MXS_INFO("Reply not yet complete. Waiting for %d replies, got one from %s",
                 m_expected_responses, backend->name());
    }

    if (writebuf)
    {
        if (m_state == TRX_REPLAY)
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
                return 1;
            }
        }
        else if (m_config.transaction_replay && trx_is_ending())
        {
            finish_transaction(backend);
        }
    }

    int32_t rc = 1;

    if (writebuf)
    {
        mxb_assert_message(backend->in_use(), "Backend should be in use when routing reply");
        /** Write reply to client DCB */
        rc = RouterSession::clientReply(writebuf, down, reply);
    }

    if (reply.is_complete() && m_expected_responses == 0)
    {
        execute_queued_commands(backend);
    }

    if (m_expected_responses == 0)
    {
        /**
         * Close stale connections to servers in maintenance. Done here to avoid closing the connections
         * before all responses have been received.
         */
        close_stale_connections();
    }

    return rc;
}

void RWSplitSession::execute_queued_commands(mxs::RWBackend* backend)
{
    mxb_assert(m_expected_responses == 0);

    if (!m_query_queue.empty() && m_state != TRX_REPLAY)
    {
        /**
         * All replies received, route any stored queries. This should be done
         * even when transaction replay is active as long as we just completed
         * a session command.
         */
        route_stored_query();
    }
}

bool RWSplitSession::can_start_trx_replay() const
{
    bool can_replay = false;

    if (m_can_replay_trx)
    {
        if (m_config.trx_timeout > 0s)
        {
            // m_trx_replay_timer is only set when the first replay starts, this is why we must check how many
            // attempts we've made.
            if (m_num_trx_replays == 0 || m_trx_replay_timer.split() < m_config.trx_timeout)
            {
                can_replay = true;
            }
            else
            {
                MXS_INFO("Transaction replay time limit of %ld seconds exceeded, not attempting replay",
                         m_config.trx_timeout.count());
            }
        }
        else
        {
            if (m_num_trx_replays < m_config.trx_max_attempts)
            {
                can_replay = true;
            }
            else
            {
                mxb_assert(m_num_trx_replays == m_config.trx_max_attempts);
                MXS_INFO("Transaction replay attempt cap of %ld exceeded, not attempting replay",
                         m_config.trx_max_attempts);
            }
        }
    }

    return can_replay;
}

bool RWSplitSession::start_trx_replay()
{
    bool rval = false;

    if (m_config.transaction_replay && can_start_trx_replay())
    {
        ++m_num_trx_replays;

        if (m_state != TRX_REPLAY)
        {
            // This is the first time we're retrying this transaction, store it and the interrupted query
            m_orig_trx = m_trx;
            m_orig_stmt.copy_from(m_current_query);
            m_trx_replay_timer.restart();
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

            MXS_INFO("Starting transaction replay %ld. Replay has been ongoing for %ld seconds.",
                     m_num_trx_replays, trx_replay_seconds());
            m_state = TRX_REPLAY;

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
                const char* cmd = STRPACKETTYPE(mxs_mysql_get_command(buf));
                MXS_INFO("Replaying %s: %s", cmd, mxs::extract_sql(buf, 1024).c_str());
                retry_query(buf, 1);
            }
            else
            {
                /**
                 * The transaction was only opened and no queries have been
                 * executed. The buffer should contain a query that starts
                 * a transaction or autocommit should be disabled.
                 */
                mxb_assert_message(qc_get_trx_type_mask(m_interrupted_query.get()) & QUERY_TYPE_BEGIN_TRX
                                   || !protocol_data()->is_autocommit,
                                   "The current query should start a transaction "
                                   "or autocommit should be disabled");

                MXS_INFO("Retrying interrupted query: %s",
                         mxs::extract_sql(m_interrupted_query.get()).c_str());
                retry_query(m_interrupted_query.release(), 1);
            }
        }
        else
        {
            mxb_assert_message(!protocol_data()->is_autocommit || trx_is_ending(),
                               "Session should have autocommit disabled or transaction just ended if the "
                               "transaction had no statements and no query was interrupted");
        }

        rval = true;
    }

    return rval;
}

bool RWSplitSession::retry_master_query(RWBackend* backend)
{
    bool can_continue = false;

    if (m_current_query.get())
    {
        // A query was in progress, try to route it again
        mxb_assert(m_prev_plan.target == backend || m_prev_plan.route_target == TARGET_ALL);
        retry_query(m_current_query.release());
        can_continue = true;
    }
    else
    {
        // This should never happen
        mxb_assert_message(!true, "m_current_query is empty");
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
        m_pSession->kill();
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
            // We were expecting a response but we aren't going to get one
            mxb_assert(m_expected_responses >= 1);

            errmsg += " Lost connection to master server while waiting for a result.";

            if (m_expected_responses > 1)
            {
                can_continue = false;
                errmsg += " Cannot retry query as multiple queries were in progress.";
            }
            else if (can_retry_query() && can_recover_master())
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

        if (trx_is_open() && !in_optimistic_trx() && (!m_trx.target() || m_trx.target() == backend))
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
        else if (in_optimistic_trx())
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
        // Slaves should never have more than one response waiting
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
            retry_query(m_current_query.release());
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
