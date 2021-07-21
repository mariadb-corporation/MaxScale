/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplitsession.hh"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mysqld_error.h>

#include <maxbase/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/modutil.hh>
#include <maxscale/modutil.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/session_command.hh>
#include <maxscale/utils.hh>

using namespace maxscale;

using std::chrono::seconds;

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

bool RWSplitSession::prepare_connection(RWBackend* target)
{
    mxb_assert(!target->in_use());
    bool rval = target->connect(&m_sescmd_list);

    if (rval)
    {
        MXS_INFO("Connected to '%s'", target->name());
        mxb_assert_message(!target->is_waiting_result()
                           || (!m_sescmd_list.empty() && target->has_session_commands()),
                           "Session command list must not be empty and target "
                           "should have unfinished session commands.");
    }

    return rval;
}

bool RWSplitSession::prepare_target(RWBackend* target, route_target_t route_target)
{
    mxb_assert(target->in_use() || (target->can_connect() && can_recover_servers()));
    return target->in_use() || prepare_connection(target);
}

void RWSplitSession::retry_query(GWBUF* querybuf, int delay)
{
    mxb_assert(querybuf);
    // Try to route the query again later
    MXS_SESSION* session = m_session;

    /**
     * Used to distinct retried queries from new ones while we're doing transaction replay.
     * Not the cleanest way to do things but this will have to do for 2.3.
     *
     * TODO: Figure out a way to "cork" the client DCB as that would remove the need for this and be
     * architecturally more clear.
     */
    gwbuf_set_type(querybuf, GWBUF_TYPE_REPLAYED);

    mxs::Downstream down;
    down.instance = (mxs_filter*)m_router;
    down.routeQuery = (DOWNSTREAMFUNC)RWSplit::routeQuery;

    // The RWSplitSession must first be static_cast into the base class MXS_ROUTER_SESSION since it is a
    // polymorphic class. This makes the subsequent static_cast in the router template's routeQuery work.
    down.session = (mxs_filter_session*)static_cast<MXS_ROUTER_SESSION*>(this);

    session_delay_routing(session, down, querybuf, delay);
    ++m_retry_duration;
}

bool RWSplitSession::have_connected_slaves() const
{
    return std::any_of(m_raw_backends.begin(), m_raw_backends.end(), [](auto b) {
                           return b->is_slave() && b->in_use();
                       });
}

bool RWSplitSession::should_try_trx_on_slave(route_target_t route_target) const
{
    return m_config.optimistic_trx          // Optimistic transactions are enabled
           && !is_locked_to_master()        // Not locked to master
           && !m_is_replay_active           // Not replaying a transaction
           && m_otrx_state == OTRX_INACTIVE // Not yet in optimistic mode
           && TARGET_IS_MASTER(route_target)// The target type is master
           && have_connected_slaves()       // At least one connected slave
           && m_qc.is_trx_still_read_only();// The start of the transaction is a read-only statement
}

bool RWSplitSession::track_optimistic_trx(mxs::Buffer* buffer)
{
    bool store_stmt = true;

    if (trx_is_ending())
    {
        m_otrx_state = OTRX_INACTIVE;
    }
    else if (!m_qc.is_trx_still_read_only())
    {
        // Not a plain SELECT, roll it back on the slave and start it on the master
        MXS_INFO("Rolling back current optimistic transaction");

        /**
         * Store the actual statement we were attempting to execute and
         * replace it with a ROLLBACK. The storing of the statement is
         * done here to avoid storage of the ROLLBACK.
         */
        m_current_query.reset(buffer->release());
        buffer->reset(modutil_create_query("ROLLBACK"));

        store_stmt = false;
        m_otrx_state = OTRX_ROLLBACK;
    }

    return store_stmt;
}

/**
 * Route query to all backends
 *
 * @param querybuf Query to route
 *
 * @return True if routing was successful
 */
bool RWSplitSession::handle_target_is_all(mxs::Buffer&& buffer)
{
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    bool result = false;
    bool is_large = is_large_query(buffer.get());

    if (m_qc.large_query())
    {
        // TODO: Append to the already stored session command instead of disabling history
        MXS_INFO("Large session write, have to disable session command history");
        m_config.disable_sescmd_history = true;

        continue_large_session_write(buffer.get(), info.type_mask());
        result = true;
    }
    else if (route_session_write(buffer.release(), info.command(), info.type_mask()))
    {
        result = true;
        mxb::atomic::add(&m_router->stats().n_all, 1, mxb::atomic::RELAXED);
        mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);
    }

    m_qc.set_large_query(is_large);

    return result;
}

bool RWSplitSession::handle_routing_failure(mxs::Buffer&& buffer, route_target_t route_target)
{
    bool ok = true;
    auto next_master = get_master_backend();

    if (should_migrate_trx(next_master))
    {
        ok = start_trx_migration(next_master, buffer.get());

        // If the current master connection is still open, we must close it to prevent the transaction from
        // being accidentally committed whenever a new transaction is started on it.
        if (m_current_master && m_current_master->in_use())
        {
            m_current_master->close();
            m_current_master->set_close_reason("Closed due to transaction migration");
        }
    }
    else if (can_retry_query() || can_continue_trx_replay())
    {
        MXS_INFO("Delaying routing: %s", extract_sql(buffer.get()).c_str());
        retry_query(buffer.release());
    }
    else if (m_config.master_failure_mode == RW_ERROR_ON_WRITE)
    {
        MXS_INFO("Sending read-only error, no valid target found for %s",
                 route_target_to_string(route_target));
        send_readonly_error();

        if (m_current_master && m_current_master->in_use())
        {
            m_current_master->close();
            m_current_master->set_close_reason("The original master is not available");
        }
    }
    else
    {
        MXS_ERROR("Could not find valid server for target type %s (%s: %s), closing connection.\n%s",
                  route_target_to_string(route_target), STRPACKETTYPE(buffer.data()[4]),
                  mxs::extract_sql(buffer.get()).c_str(), get_verbose_status().c_str());
        ok = false;
    }

    return ok;
}

void RWSplitSession::send_readonly_error()
{
    auto err = modutil_create_mysql_err_msg(1, 0, ER_OPTION_PREVENTS_STATEMENT, "HY000",
                                            "The MariaDB server is running with the --read-only"
                                            " option so it cannot execute this statement");
    mxs::ReplyRoute route;
    RouterSession::clientReply(err, route, mxs::Reply());
}

bool RWSplitSession::query_not_supported(GWBUF* querybuf)
{
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    route_target_t route_target = info.target();
    GWBUF* err = nullptr;

    if (mxs_mysql_is_ps_command(info.command()) && info.stmt_id() == 0)
    {
        if (mxs_mysql_command_will_respond(info.command()))
        {
            // Unknown PS ID, can't route this query
            std::stringstream ss;
            ss << "Unknown prepared statement handler (" << extract_binary_ps_id(querybuf)
               << ") given to MaxScale";
            err = modutil_create_mysql_err_msg(1, 0, ER_UNKNOWN_STMT_HANDLER, "HY000", ss.str().c_str());
        }
        else
        {
            // The command doesn't expect a response which means we mustn't send one. Sending an unexpected
            // error will cause the client to go out of sync.
            return true;
        }
    }
    else if (TARGET_IS_ALL(route_target) && (TARGET_IS_MASTER(route_target) || TARGET_IS_SLAVE(route_target)))
    {
        // Conflicting routing targets. Return an error to the client.
        MXS_ERROR("Can't route %s '%s'. SELECT with session data modification is not "
                  "supported with `use_sql_variables_in=all`.",
                  STRPACKETTYPE(info.command()), mxs::extract_sql(querybuf).c_str());

        err = modutil_create_mysql_err_msg(1, 0, 1064, "42000",
                                           "Routing query to backend failed. "
                                           "See the error log for further details.");
    }

    if (err)
    {
        mxs::ReplyRoute route;
        RouterSession::clientReply(err, route, mxs::Reply());
    }

    return err != nullptr;
}

/**
 * Routes a buffer containing a single packet
 *
 * @param buffer The buffer to route
 *
 * @return True if routing succeed or if it failed due to unsupported query.
 *         false if backend failure was encountered.
 */
bool RWSplitSession::route_stmt(mxs::Buffer&& buffer)
{
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    route_target_t route_target = info.target();
    mxb_assert_message(m_otrx_state != OTRX_ROLLBACK,
                       "OTRX_ROLLBACK should never happen when routing queries");

    auto next_master = get_master_backend();

    if (should_replace_master(next_master))
    {
        mxb_assert(next_master->is_master());
        MXS_INFO("Replacing old master '%s' with new master '%s'",
                 m_current_master ? m_current_master->name() : "<no previous master>", next_master->name());
        replace_master(next_master);
    }

    if (query_not_supported(buffer.get()))
    {
        return true;
    }
    else if (TARGET_IS_ALL(route_target))
    {
        return handle_target_is_all(std::move(buffer));
    }
    else
    {
        return route_single_stmt(std::move(buffer));
    }
}

bool RWSplitSession::route_single_stmt(mxs::Buffer&& buffer)
{
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    route_target_t route_target = info.target();

    update_trx_statistics();

    if (trx_is_starting() && !trx_is_read_only() && should_try_trx_on_slave(route_target))
    {
        // A normal transaction is starting and it qualifies for speculative routing
        m_otrx_state = OTRX_STARTING;
        route_target = TARGET_SLAVE;
    }
    else if (m_otrx_state == OTRX_STARTING)
    {
        // Transaction was started, begin active tracking of its progress
        m_otrx_state = OTRX_ACTIVE;
    }

    // If delayed query retry is enabled, we need to store the current statement
    bool store_stmt = m_config.delayed_retry
        || (TARGET_IS_SLAVE(route_target) && m_config.retry_failed_reads);

    if (m_qc.large_query())
    {
        /** We're processing a large query that's split across multiple packets.
         * Route it to the same backend where we routed the previous packet. */
        route_target = TARGET_LAST_USED;
    }
    else if (m_otrx_state == OTRX_ACTIVE)
    {
        /** We are speculatively executing a transaction to the slave, keep
         * routing queries to the same server. If the query modifies data,
         * a rollback is initiated on the slave server. */
        store_stmt = track_optimistic_trx(&buffer);
        route_target = TARGET_LAST_USED;
    }

    bool ok = true;

    if (auto target = get_target(buffer.get(), route_target))
    {
        // We have a valid target, reset retry duration
        m_retry_duration = 0;

        if (!prepare_target(target, route_target))
        {
            // The connection to target was down and we failed to reconnect
            ok = false;
        }
        else if (target->has_session_commands())
        {
            // We need to wait until the session commands are executed
            m_query_queue.emplace_front(std::move(buffer));
            MXS_INFO("Queuing query until '%s' completes session command", target->name());
        }
        else
        {
            // Target server was found and is in the correct state
            ok = handle_got_target(std::move(buffer), target, store_stmt);
        }
    }
    else
    {
        ok = handle_routing_failure(std::move(buffer), route_target);
    }

    return ok;
}

RWBackend* RWSplitSession::get_target(GWBUF* querybuf, route_target_t route_target)
{
    RWBackend* rval = nullptr;
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();

    // We can't use a switch here as the route_target is a bitfield where multiple values are set at one time.
    // Mostly this happens when the type is TARGET_NAMED_SERVER and TARGET_SLAVE due to a routing hint.
    if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
    {
        rval = handle_hinted_target(querybuf, route_target);
    }
    else if (TARGET_IS_LAST_USED(route_target))
    {
        rval = get_last_used_backend();
    }
    else if (TARGET_IS_SLAVE(route_target))
    {
        rval = handle_slave_is_target(info.command(), info.stmt_id());
    }
    else if (TARGET_IS_MASTER(route_target))
    {
        rval = handle_master_is_target();
    }
    else
    {
        MXS_ERROR("Unexpected target type: %s", route_target_to_string(route_target));
        mxb_assert(!true);
    }

    return rval;
}

/**
 * Execute in backends used by current router session.
 * Save session variable commands to router session property
 * struct. Thus, they can be replayed in backends which are
 * started and joined later.
 *
 * Suppress redundant OK packets sent by backends.
 *
 * The first OK packet is replied to the client.
 *
 * @param querybuf      GWBUF including the query to be routed
 * @param inst          Router instance
 * @param packet_type       Type of MySQL packet
 * @param qtype         Query type from query_classifier
 *
 * @return True if at least one backend is used and routing succeed to all
 * backends being used, otherwise false.
 *
 */
bool RWSplitSession::route_session_write(GWBUF* querybuf, uint8_t command, uint32_t type)
{
    if (!have_open_connections())
    {
        if (command == MXS_COM_QUIT)
        {
            // We have no open connections and opening one just to close it is pointless.
            gwbuf_free(querybuf);
            return true;
        }
        else if (can_recover_servers())
        {
            // No connections are open, create one and execute the session command on it
            create_one_connection_for_sescmd();
        }
    }

    /** The SessionCommand takes ownership of the buffer */
    auto sescmd = create_sescmd(querybuf);
    uint64_t id = sescmd->get_position();
    bool expecting_response = mxs_mysql_command_will_respond(command);
    int nsucc = 0;
    uint64_t lowest_pos = id;

    MXS_INFO("Session write, routing to all servers.");
    bool attempted_write = false;

    // Pick a new replier for each new session command. This allows the source server to change over
    // the course of the session. The replier will usually be the current master server.
    m_sescmd_replier = nullptr;

    for (RWBackend* backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            attempted_write = true;
            backend->append_session_command(sescmd);

            uint64_t current_pos = backend->next_session_command()->get_position();

            if (current_pos < lowest_pos)
            {
                lowest_pos = current_pos;
            }

            if (backend->is_waiting_result() || backend->execute_session_command())
            {
                nsucc += 1;
                m_server_stats[backend->target()].inc_total();
                m_server_stats[backend->target()].inc_read();

                if (!m_sescmd_replier || backend == m_current_master)
                {
                    // Return the result from this backend to the client
                    m_sescmd_replier = backend;
                }

                MXS_INFO("Route query to %s: %s",
                         backend->is_master() ? "master" : "slave",
                         backend->name());
            }
            else
            {
                backend->close();

                if (m_config.master_failure_mode == RW_FAIL_INSTANTLY && backend == m_current_master)
                {
                    MXS_ERROR("Failed to execute session command in Master: %s", backend->name());
                    return false;
                }
                else
                {
                    MXS_ERROR("Failed to execute session command in %s", backend->name());
                }
            }
        }
    }

    if (m_sescmd_replier)
    {
        mxb_assert(nsucc);
        if (expecting_response)
        {
            m_expected_responses++;
            mxb_assert(m_expected_responses == 1);
            MXS_INFO("Will return response from '%s' to the client", m_sescmd_replier->name());
        }
    }

    if (m_config.max_sescmd_history > 0 && m_sescmd_list.size() >= m_config.max_sescmd_history
        && !m_config.prune_sescmd_history)
    {
        static bool warn_history_exceeded = true;
        if (warn_history_exceeded)
        {
            MXS_WARNING("Router session exceeded session command history limit. "
                        "Server reconnection is disabled and only servers with "
                        "consistent session state are used for the duration of"
                        "the session. To disable this warning and the session "
                        "command history, add `disable_sescmd_history=true` to "
                        "service '%s'. To increase the limit (currently %lu), add "
                        "`max_sescmd_history` to the same service and increase the value.",
                        m_router->service()->name(),
                        m_config.max_sescmd_history);
            warn_history_exceeded = false;
        }

        m_config.disable_sescmd_history = true;
        m_config.max_sescmd_history = 0;
        m_sescmd_list.clear();
    }

    if (m_config.prune_sescmd_history && !m_sescmd_list.empty()
        && m_sescmd_list.size() >= m_config.max_sescmd_history)
    {
        // Close to the history limit, remove the oldest command
        discard_responses(std::min(m_sescmd_list.front()->get_position(), lowest_pos));
        m_sescmd_list.pop_front();
    }

    if (m_config.disable_sescmd_history)
    {
        discard_responses(lowest_pos);
    }
    else
    {
        discard_old_history(lowest_pos);
        compress_history(sescmd);
        m_sescmd_list.push_back(sescmd);
    }

    if (nsucc)
    {
        m_sent_sescmd = id;

        if (!expecting_response)
        {
            /** The command doesn't generate a response so we increment the
             * completed session command count */
            m_recv_sescmd++;
        }
    }
    else
    {
        MXS_ERROR("Could not route session command `%s`: %s. Connection status: %s",
                  sescmd->to_string().c_str(),
                  attempted_write ? "Write to all backends failed" : "All connections have failed",
                  get_verbose_status().c_str());
    }

    return nsucc;
}

RWBackend* RWSplitSession::get_hinted_backend(const char* name)
{
    RWBackend* rval = nullptr;

    for (auto it = m_raw_backends.begin(); it != m_raw_backends.end(); it++)
    {
        auto& backend = *it;

        /** The server must be a valid slave, relay server, or master */
        if ((backend->in_use() || (can_recover_servers() && backend->can_connect()))
            && strcasecmp(name, backend->name()) == 0)
        {
            rval = backend;
            break;
        }
    }

    return rval;
}

RWBackend* RWSplitSession::get_master_backend()
{
    RWBackend* rval = nullptr;

    if (RWBackend* master = get_root_master())
    {
        if (is_valid_for_master(master))
        {
            rval = master;
        }
    }

    return rval;
}

RWBackend* RWSplitSession::get_last_used_backend()
{
    return m_prev_target ? m_prev_target : get_master_backend();
}

/**
 * Provide the router with a reference to a suitable backend
 *
 * @param rses     Pointer to router client session
 * @param btype    Backend type
 * @param name     Name of the requested backend. May be NULL if any name is accepted.
 * @param max_rlag Maximum replication lag
 * @param target   The target backend
 *
 * @return True if a backend was found
 */
RWBackend* RWSplitSession::get_target_backend(backend_type_t btype,
                                              const char* name,
                                              int max_rlag)
{
    /** Check whether using target_node as target SLAVE */
    if (m_target_node && trx_is_read_only())
    {
        return m_target_node;
    }

    RWBackend* rval = nullptr;
    if (name)
    {
        // Choose backend by name from a hint
        rval = get_hinted_backend(name);
    }
    else if (btype == BE_SLAVE)
    {
        rval = get_slave_backend(max_rlag);
    }
    else if (btype == BE_MASTER)
    {
        rval = get_master_backend();
    }
    return rval;
}

/**
 * @brief Get the maximum replication lag for this router
 *
 * @param   rses    Router client session
 * @return  Replication lag from configuration or very large number
 */
int RWSplitSession::get_max_replication_lag()
{
    int conf_max_rlag = mxs::Target::RLAG_UNDEFINED;

    /** if there is no configured value, then longest possible int is used */
    if (m_config.max_slave_replication_lag > 0)
    {
        conf_max_rlag = m_config.max_slave_replication_lag;
    }

    return conf_max_rlag;
}

/**
 * @brief Handle hinted target query
 *
 * One of the possible types of handling required when a request is routed
 *
 *  @param ses          Router session
 *  @param querybuf     Buffer containing query to be routed
 *  @param route_target Target for the query
 *  @param target_dcb   DCB for the target server
 *
 *  @return bool - true if succeeded, false otherwise
 */
RWBackend* RWSplitSession::handle_hinted_target(GWBUF* querybuf, route_target_t route_target)
{
    const char rlag_hint_tag[] = "max_slave_replication_lag";
    const int comparelen = sizeof(rlag_hint_tag);
    int config_max_rlag = get_max_replication_lag();    // From router configuration.
    RWBackend* target = nullptr;

    for (HINT* hint = querybuf->hint; !target && hint; hint = hint->next)
    {
        if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            // Set the name of searched backend server.
            const char* named_server = (char*)hint->data;
            MXS_INFO("Hint: route to server '%s'.", named_server);
            target = get_target_backend(BE_UNDEFINED, named_server, config_max_rlag);
            if (!target)
            {
                // Target may differ from the requested name if the routing target is locked, e.g. by a trx.
                // Target is null only if not locked and named server was not found or was invalid.
                if (mxb_log_is_priority_enabled(LOG_INFO))
                {
                    std::string status;
                    for (const auto& a : m_backends)
                    {
                        if (strcmp(a->target()->name(), named_server) == 0)
                        {
                            status = a->target()->status_string();
                            break;
                        }
                    }
                    MXS_INFO("Was supposed to route to named server %s but couldn't find the server in a "
                             "suitable state. Server state: %s",
                             named_server, !status.empty() ? status.c_str() : "Could not find server");
                }
            }
        }
        else if (hint->type == HINT_PARAMETER
                 && (strncasecmp((char*)hint->data, rlag_hint_tag, comparelen) == 0))
        {
            const char* str_val = (char*)hint->value;
            int hint_max_rlag = (int)strtol(str_val, (char**)NULL, 10);
            if (hint_max_rlag != 0 || errno == 0)
            {
                MXS_INFO("Hint: %s=%d", rlag_hint_tag, hint_max_rlag);
                target = get_target_backend(BE_SLAVE, nullptr, hint_max_rlag);
                if (!target)
                {
                    MXS_INFO("Was supposed to route to server with replication lag "
                             "at most %d but couldn't find such a slave.", hint_max_rlag);
                }
            }
            else
            {
                MXS_ERROR("Hint: Could not parse value of %s: '%s' is not a valid number.",
                          rlag_hint_tag, str_val);
            }
        }
    }

    if (!target)
    {
        // If no target so far, pick any available. TODO: should this be error instead?
        // Erroring here is more appropriate when namedserverfilter allows setting multiple target types
        // e.g. target=server1,->slave

        backend_type_t btype = route_target & TARGET_SLAVE ? BE_SLAVE : BE_MASTER;
        target = get_target_backend(btype, NULL, config_max_rlag);
    }
    return target;
}

/**
 * Handle slave target type
 *
 * @param cmd     Command being executed
 * @param stmt_id Prepared statement ID
 *
 * @return The target backend if one was found
 */
RWBackend* RWSplitSession::handle_slave_is_target(uint8_t cmd, uint32_t stmt_id)
{
    int rlag_max = get_max_replication_lag();
    RWBackend* target = nullptr;

    if (m_qc.is_ps_continuation())
    {
        ExecMap::iterator it = m_exec_map.find(stmt_id);

        if (it != m_exec_map.end() && it->second.target)
        {
            auto prev_target = it->second.target;

            if (prev_target->in_use())
            {
                target = prev_target;
                MXS_INFO("%s on %s", STRPACKETTYPE(cmd), target->name());
            }
            else
            {
                MXS_ERROR("Old COM_STMT_EXECUTE target %s not in use, cannot "
                          "proceed with %s", prev_target->name(), STRPACKETTYPE(cmd));
            }
        }
        else
        {
            MXS_WARNING("Unknown statement ID %u used in %s", stmt_id, STRPACKETTYPE(cmd));
        }
    }
    else
    {
        target = get_target_backend(BE_SLAVE, NULL, rlag_max);
    }

    if (target)
    {
        mxb::atomic::add(&m_router->stats().n_slave, 1, mxb::atomic::RELAXED);
        mxb_assert(target->in_use() || target->can_connect());
    }
    else
    {
        MXS_INFO("Was supposed to route to slave but finding suitable one failed.");
    }

    return target;
}

/**
 * @brief Log master write failure
 */
void RWSplitSession::log_master_routing_failure(bool found,
                                                RWBackend* old_master,
                                                RWBackend* curr_master)
{
    char errmsg[1024 * 2 + 100];        // Extra space for error message

    if (m_config.delayed_retry && m_retry_duration >= m_config.delayed_retry_timeout)
    {
        sprintf(errmsg, "'delayed_retry_timeout' exceeded before a master could be found");
    }
    else if (!found)
    {
        sprintf(errmsg, "Could not find a valid master connection");
    }
    else if (old_master && curr_master && old_master->in_use())
    {
        /** We found a master but it's not the same connection */
        mxb_assert(old_master != curr_master);
        sprintf(errmsg,
                "Master server changed from '%s' to '%s'",
                old_master->name(),
                curr_master->name());
    }
    else if (old_master && old_master->in_use())
    {
        // TODO: Figure out if this is an impossible situation
        mxb_assert(!curr_master);
        /** We have an original master connection but we couldn't find it */
        sprintf(errmsg,
                "The connection to master server '%s' is not available",
                old_master->name());
    }
    else
    {
        /** We never had a master connection, the session must be in read-only mode */
        if (m_config.master_failure_mode != RW_FAIL_INSTANTLY)
        {
            sprintf(errmsg,
                    "Session is in read-only mode because it was created "
                    "when no master was available");
        }
        else
        {
            mxb_assert(old_master && !old_master->in_use());
            sprintf(errmsg,
                    "Was supposed to route to master but the master connection is %s",
                    old_master->is_closed() ? "closed" : "not in a suitable state");
            mxb_assert(old_master->is_closed());
        }
    }

    MXS_WARNING("[%s] Write query received from %s@%s. %s. Closing client connection.",
                m_router->service()->name(),
                m_session->user().c_str(),
                m_session->client_remote().c_str(),
                errmsg);
}

bool RWSplitSession::trx_is_starting() const
{
    return m_session->is_trx_starting();
}

bool RWSplitSession::trx_is_read_only() const
{
    return m_session->is_trx_read_only();
}

bool RWSplitSession::trx_is_open() const
{
    return m_session->is_trx_active();
}

bool RWSplitSession::trx_is_ending() const
{
    return m_session->is_trx_ending();
}

bool RWSplitSession::should_replace_master(RWBackend* target)
{
    return m_config.master_reconnection
           &&   // We have a target server and it's not the current master
           target && target != m_current_master
           &&   // We are not inside a transaction (also checks for autocommit=1)
           (!trx_is_open() || trx_is_starting() || m_is_replay_active)
           &&   // We are not locked to the old master
           !is_locked_to_master();
}

void RWSplitSession::replace_master(RWBackend* target)
{
    m_current_master = target;

    m_qc.master_replaced();
}

bool RWSplitSession::should_migrate_trx(RWBackend* target)
{
    bool migrate = false;

    if (m_config.transaction_replay
        && !m_is_replay_active  // Transaction replay is not active
        && trx_is_open()        // We have an open transaction
        && m_can_replay_trx)    // The transaction can be replayed
    {
        if (target && target != m_current_master)
        {
            // We have a target server and it's not the current master
            migrate = true;
        }
        else if (!target && (!m_current_master || !m_current_master->is_master()))
        {
            // We don't have a target but our current master is no longer usable
            migrate = true;
        }
    }

    return migrate;
}

bool RWSplitSession::start_trx_migration(RWBackend* target, GWBUF* querybuf)
{
    if (target)
    {
        MXS_INFO("Starting transaction migration to '%s'", target->name());
    }

    /**
     * Stash the current query so that the transaction replay treats
     * it as if the query was interrupted.
     */
    m_current_query.copy_from(querybuf);

    /**
     * After the transaction replay has been started, the rest of
     * the query processing needs to be skipped. This is done to avoid
     * the error logging done when no valid target is found for a query
     * as well as to prevent retrying of queries in the wrong order.
     */
    return start_trx_replay();
}

/**
 * @brief Handle master is the target
 *
 * One of the possible types of handling required when a request is routed
 *
 *  @param inst         Router instance
 *  @param ses          Router session
 *  @param target_dcb   DCB for the target server
 *
 *  @return bool - true if succeeded, false otherwise
 */
RWBackend* RWSplitSession::handle_master_is_target()
{
    RWBackend* target = get_target_backend(BE_MASTER, NULL, mxs::Target::RLAG_UNDEFINED);
    RWBackend* rval = nullptr;

    if (target && target == m_current_master)
    {
        mxb::atomic::add(&m_router->stats().n_master, 1, mxb::atomic::RELAXED);
        rval = target;
    }
    else if (!m_config.delayed_retry || m_retry_duration >= m_config.delayed_retry_timeout)
    {
        // Cannot retry the query, log a message that routing has failed
        log_master_routing_failure(target, m_current_master, target);
    }

    if (!m_locked_to_master && m_target_node == m_current_master)
    {
        // Reset the forced node as we're not permanently locked to it
        m_target_node = nullptr;
    }

    return rval;
}

void RWSplitSession::process_stmt_execute(mxs::Buffer* buf, uint32_t id, RWBackend* target)
{
    mxb_assert(buf->is_contiguous());
    mxb_assert(mxs_mysql_get_command(buf->get()) == MXS_COM_STMT_EXECUTE);
    auto params = m_qc.get_param_count(id);

    if (params > 0)
    {
        size_t types_offset = MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + ((params + 7) / 8);
        uint8_t* ptr = buf->data() + types_offset;

        if (*ptr)
        {
            ++ptr;
            // Store the metadata, two bytes per parameter, for later use
            m_exec_map[id].metadata.assign(ptr, ptr + (params * 2));
        }
        else
        {
            auto it = m_exec_map.find(id);

            if (it == m_exec_map.end())
            {
                MXS_WARNING("Malformed COM_STMT_EXECUTE (ID %u): could not find previous "
                            "execution with metadata and current execution doesn't contain it", id);
                mxb_assert(!true);
            }
            else if (it->second.metadata_sent.count(target) == 0)
            {
                const auto& info = it->second;
                mxb_assert(!info.metadata.empty());
                mxs::Buffer newbuf(buf->length() + info.metadata.size());
                auto data = newbuf.data();

                memcpy(data, buf->data(), types_offset);
                data += types_offset;

                // Set to 1, we are sending the types
                mxb_assert(*ptr == 0);
                *data++ = 1;

                // Splice the metadata into COM_STMT_EXECUTE
                memcpy(data, info.metadata.data(), info.metadata.size());
                data += info.metadata.size();

                // Copy remaining data that is being sent and update the packet length
                mxb_assert(buf->length() > types_offset + 1);
                memcpy(data, buf->data() + types_offset + 1, buf->length() - types_offset - 1);
                gw_mysql_set_byte3(newbuf.data(), newbuf.length() - MYSQL_HEADER_LEN);
                buf->reset(newbuf.release());
            }
        }
    }
}

/**
 * @brief Handle writing to a target server
 *
 *  @return True on success
 */
bool RWSplitSession::handle_got_target(mxs::Buffer&& buffer, RWBackend* target, bool store)
{
    mxb_assert_message(target->in_use(), "Target must be in use before routing to it");
    mxb_assert_message(!target->has_session_commands(), "The session command cursor must not be active");

    /**
     * TODO: This effectively disables pipelining of queries, very bad for batch insert performance. Replace
     *       with proper, per server tracking of which responses need to be sent to the client.
     */
    mxb_assert_message(!target->is_waiting_result() || m_qc.large_query(),
                       "Node must be idle when routing queries to it");

    MXS_INFO("Route query to %s: %s <", target->is_master() ? "master" : "slave", target->name());

    if (!m_target_node && trx_is_read_only())
    {
        // Lock the session to this node until the read-only transaction ends
        m_target_node = target;
    }

    mxs::Backend::response_type response = mxs::Backend::NO_RESPONSE;
    uint8_t cmd = mxs_mysql_get_command(buffer.get());

    if (cmd == MXS_COM_QUERY && target->is_slave()
        && ((m_config.causal_reads == CausalReads::LOCAL && !m_gtid_pos.empty())
            || m_config.causal_reads == CausalReads::GLOBAL))
    {
        // Perform the causal read only when the query is routed to a slave
        auto tmp = add_prefix_wait_gtid(m_router->service()->get_version(SERVICE_VERSION_MIN),
                                        buffer.release());
        buffer.reset(tmp);
        m_wait_gtid = WAITING_FOR_HEADER;

        // The storage for causal reads is done inside add_prefix_wait_gtid
        store = false;
    }
    else if (m_config.causal_reads != CausalReads::NONE && target->is_master())
    {
        gwbuf_set_type(buffer.get(), GWBUF_TYPE_TRACK_STATE);
    }

    if (m_qc.load_data_state() != QueryClassifier::LOAD_DATA_ACTIVE
        && !m_qc.large_query() && mxs_mysql_command_will_respond(cmd))
    {
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    bool large_query = is_large_query(buffer.get());
    uint32_t orig_id = 0;

    if (!is_locked_to_master() && mxs_mysql_is_ps_command(cmd) && !m_qc.large_query())
    {
        // Store the original ID in case routing fails
        orig_id = extract_binary_ps_id(buffer.get());
        // Replace the ID with our internal one, the backends will replace it with their own ID
        auto new_id = m_qc.current_route_info().stmt_id();
        replace_binary_ps_id(buffer.get(), new_id);

        if (cmd == MXS_COM_STMT_EXECUTE)
        {
            // The metadata in COM_STMT_EXECUTE is optional. If the statement contains the metadata, store it
            // for later use. If it doesn't, add it if the current target has never gotten it.
            process_stmt_execute(&buffer, new_id, target);
        }
    }

    /**
     * If we are starting a new query, we use RWBackend::write, otherwise we use
     * RWBackend::continue_write to continue an ongoing query. RWBackend::write
     * will do the replacement of PS IDs which must not be done if we are
     * continuing an ongoing query.
     */
    bool success = target->write(gwbuf_clone(buffer.get()), response);

    if (orig_id)
    {
        // Put the original ID back in case we try to route the query again
        replace_binary_ps_id(buffer.get(), orig_id);
    }

    if (success)
    {
        if (store)
        {
            m_current_query.copy_from(buffer);
        }

        mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);
        m_server_stats[target->target()].inc_total();

        const uint32_t read_only_types = QUERY_TYPE_READ | QUERY_TYPE_LOCAL_READ
            | QUERY_TYPE_USERVAR_READ | QUERY_TYPE_SYSVAR_READ | QUERY_TYPE_GSYSVAR_READ;

        if ((m_qc.current_route_info().type_mask() & ~read_only_types) && !trx_is_read_only())
        {
            m_server_stats[target->target()].inc_write();
        }
        else
        {
            m_server_stats[target->target()].inc_read();
        }

        if (TARGET_IS_SLAVE(m_qc.current_route_info().target())
            && (cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_EXECUTE))
        {
            target->select_started();
        }

        if (!m_qc.large_query() && response == mxs::Backend::EXPECT_RESPONSE)
        {
            /** The server will reply to this command */
            m_expected_responses++;

            if (m_qc.load_data_state() == QueryClassifier::LOAD_DATA_END)
            {
                /** The final packet in a LOAD DATA LOCAL INFILE is an empty packet
                 * to which the server responds with an OK or an ERR packet */
                mxb_assert(buffer.length() == 4);
                m_qc.set_load_data_state(QueryClassifier::LOAD_DATA_INACTIVE);
                session_set_load_active(m_pSession, false);
            }
        }

        m_qc.set_large_query(large_query);

        // Store the current target
        m_prev_target = target;

        if (m_config.transaction_replay && trx_is_open())
        {
            if (!m_trx.target())
            {
                MXS_INFO("Transaction starting on '%s'", target->name());
                m_trx.set_target(target);
            }
            else
            {
                mxb_assert(m_trx.target() == target);
            }
        }
    }
    else
    {
        MXS_ERROR("Routing query failed.");
    }

    if (success && !is_locked_to_master()
        && (cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA))
    {
        /** Track the targets of the COM_STMT_EXECUTE statements. This
         * information is used to route all COM_STMT_FETCH commands
         * to the same server where the COM_STMT_EXECUTE was done. */
        auto& info = m_exec_map[m_qc.current_route_info().stmt_id()];
        info.target = target;
        info.metadata_sent.insert(target);
        MXS_INFO("%s on %s", STRPACKETTYPE(cmd), target->name());
    }

    return success;
}
