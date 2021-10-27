/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
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
#include <maxscale/utils.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

using std::chrono::seconds;
using maxscale::RWBackend;
using mariadb::QueryClassifier;
using RouteInfo = QueryClassifier::RouteInfo;

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

bool RWSplitSession::prepare_connection(RWBackend* target)
{
    mxb_assert(!target->in_use());
    bool rval = target->connect();

    if (rval)
    {
        MXS_INFO("Connected to '%s'", target->name());
        mxb_assert(!target->is_waiting_result());
    }

    return rval;
}

bool RWSplitSession::prepare_target(RWBackend* target, route_target_t route_target)
{
    mxb_assert(target->in_use() || (!target->has_failed() && can_recover_servers()));
    return target->in_use() || prepare_connection(target);
}

void RWSplitSession::retry_query(GWBUF* querybuf, int delay)
{
    /**
     * Used to distinguish retried queries from new ones while we're doing transaction replay.
     * Not the cleanest way to do things but this will have to do for 2.3.
     *
     * TODO: Figure out a way to "cork" the client DCB as that would remove the need for this and be
     * architecturally more clear.
     */
    gwbuf_set_type(querybuf, GWBUF_TYPE_REPLAYED);

    // Route the query again later
    session_delay_routing(m_pSession, this, querybuf, delay);
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
    return m_config.optimistic_trx                  // Optimistic transactions are enabled
           && !is_locked_to_master()                // Not locked to master
           && m_state == ROUTING                    // In normal routing mode
           && TARGET_IS_MASTER(route_target)        // The target type is master
           && have_connected_slaves()               // At least one connected slave
           && route_info().is_trx_still_read_only();// The start of the transaction is a read-only statement
}

void RWSplitSession::track_optimistic_trx(mxs::Buffer* buffer, const RoutingPlan& res)
{
    if (res.type == RoutingPlan::Type::OTRX_START)
    {
        mxb_assert(res.route_target == TARGET_SLAVE);
        m_state = OTRX_STARTING;
    }
    else if (res.type == RoutingPlan::Type::OTRX_END)
    {
        mxb_assert(res.route_target == TARGET_LAST_USED);

        if (trx_is_ending())
        {
            m_state = ROUTING;
        }
        else if (!route_info().is_trx_still_read_only())
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

            m_state = OTRX_ROLLBACK;
        }
    }
    else if (m_state == OTRX_STARTING)
    {
        mxb_assert(res.route_target == TARGET_LAST_USED);
        m_state = OTRX_ACTIVE;
    }
}

/**
 * Route query to all backends
 *
 * @param querybuf Query to route
 *
 * @return True if routing was successful
 */
bool RWSplitSession::handle_target_is_all(mxs::Buffer&& buffer, const RoutingPlan& res)
{
    const RouteInfo& info = route_info();
    bool result = false;

    if (route_info().large_query())
    {
        continue_large_session_write(buffer.get(), info.type_mask());
        result = true;
    }
    else if (route_session_write(buffer.release(), info.command(), info.type_mask()))
    {
        m_prev_plan = res;
        result = true;
        mxb::atomic::add(&m_router->stats().n_all, 1, mxb::atomic::RELAXED);
        mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);
    }

    return result;
}

bool RWSplitSession::handle_routing_failure(mxs::Buffer&& buffer, const RoutingPlan& res)
{
    bool ok = true;
    auto next_master = get_master_backend();

    if (should_migrate_trx(next_master))
    {
        ok = start_trx_migration(next_master, buffer.get());

        // If the current master connection is still open, we must close it to prevent the transaction from
        // being accidentally committed whenever a new transaction is started on it.
        discard_master_connection("Closed due to transaction migration");
    }
    else if (can_retry_query() || can_continue_trx_replay())
    {
        MXS_INFO("Delaying routing: %s", mxs::extract_sql(buffer.get()).c_str());
        retry_query(buffer.release());
    }
    else if (m_config.master_failure_mode == RW_ERROR_ON_WRITE)
    {
        MXS_INFO("Sending read-only error, no valid target found for %s",
                 route_target_to_string(res.route_target));
        send_readonly_error();
        discard_master_connection("The original master is not available");
    }
    else if (res.route_target == TARGET_MASTER
             && (!m_config.delayed_retry || m_retry_duration >= m_config.delayed_retry_timeout.count()))
    {
        // Cannot retry the query, log a message that routing has failed
        log_master_routing_failure(res.target != nullptr, m_current_master, res.target);
        ok = false;
    }

    else
    {
        MXS_ERROR("Could not find valid server for target type %s (%s: %s), closing connection.\n%s",
                  route_target_to_string(res.route_target), STRPACKETTYPE(buffer.data()[4]),
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
    const RouteInfo& info = route_info();
    route_target_t route_target = info.target();
    GWBUF* err = nullptr;

    if (mxs_mysql_is_ps_command(info.command()) && info.stmt_id() == 0)
    {
        if (mxs_mysql_command_will_respond(info.command()))
        {
            // Unknown PS ID, can't route this query
            std::stringstream ss;
            ss << "Unknown prepared statement handler (" << extract_binary_ps_id(querybuf)
               << ") for " << STRPACKETTYPE(info.command()) << " given to MaxScale";
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

bool RWSplitSession::reuse_prepared_stmt(const mxs::Buffer& buffer)
{
    const RouteInfo& info = route_info();

    if (info.command() == MXS_COM_STMT_PREPARE)
    {
        auto it = m_ps_cache.find(mxs::extract_sql(buffer));

        if (it != m_ps_cache.end())
        {
            mxs::ReplyRoute route;
            RouterSession::clientReply(gwbuf_deep_clone(it->second.get()), route, mxs::Reply());
            return true;
        }
    }
    else if (info.command() == MXS_COM_STMT_CLOSE)
    {
        return true;
    }

    return false;
}

/**
 * Routes a buffer containing a single packet
 *
 * @param buffer The buffer to route
 *
 * @return True if routing succeed or if it failed due to unsupported query.
 *         false if backend failure was encountered.
 */
bool RWSplitSession::route_stmt(mxs::Buffer&& buffer, const RoutingPlan& res)
{
    const RouteInfo& info = route_info();
    route_target_t route_target = info.target();
    mxb_assert_message(m_state != OTRX_ROLLBACK, "OTRX_ROLLBACK should never happen when routing queries");

    if (m_config.reuse_ps && reuse_prepared_stmt(buffer))
    {
        mxb::atomic::add(&m_router->stats().n_ps_reused, 1, mxb::atomic::RELAXED);
        return true;
    }

    if (query_not_supported(buffer.get()))
    {
        return true;
    }
    else if (TARGET_IS_ALL(route_target))
    {
        return handle_target_is_all(std::move(buffer), res);
    }
    else
    {
        return route_single_stmt(std::move(buffer), res);
    }
}

bool RWSplitSession::route_single_stmt(mxs::Buffer&& buffer, const RoutingPlan& res)
{
    bool ok = true;
    auto target = res.target;

    if (res.route_target == TARGET_MASTER && target != m_current_master)
    {
        if (should_replace_master(target))
        {
            MXS_INFO("Replacing old master '%s' with new master '%s'",
                     m_current_master ? m_current_master->name() : "<no previous master>",
                     target->name());
            replace_master(target);
        }
        else
        {
            target = nullptr;
        }
    }

    if (target)
    {
        update_statistics(res);

        track_optimistic_trx(&buffer, res);

        // We have a valid target, reset retry duration
        m_retry_duration = 0;

        if (!prepare_target(target, res.route_target))
        {
            // The connection to target was down and we failed to reconnect
            ok = false;
        }
        else
        {
            // If delayed query retry is enabled, we need to store the current statement
            bool store_stmt = m_state != OTRX_ROLLBACK
                && (m_config.delayed_retry
                    || (TARGET_IS_SLAVE(res.route_target) && m_config.retry_failed_reads));

            if (handle_got_target(std::move(buffer), target, store_stmt))
            {
                // Target server was found and is in the correct state. Store the original routing plan but
                // set the target as the actual target we routed it to.
                ok = true;
                m_prev_plan = res;
                m_prev_plan.target = target;

                mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);
                m_server_stats[target->target()].inc_total();
            }
        }
    }
    else
    {
        ok = handle_routing_failure(std::move(buffer), res);
    }

    return ok;
}

RWBackend* RWSplitSession::get_target(const mxs::Buffer& buffer, route_target_t route_target)
{
    RWBackend* rval = nullptr;
    const RouteInfo& info = route_info();

    // We can't use a switch here as the route_target is a bitfield where multiple values are set at one time.
    // Mostly this happens when the type is TARGET_NAMED_SERVER and TARGET_SLAVE due to a routing hint.
    if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
    {
        // If transaction replay is enabled and a transaction is open, hints must be ignored. This prevents
        // them from overriding the transaction target which is what would otherwise happen and which causes
        // problems.
        if (m_config.transaction_replay && trx_is_open() && m_trx.target())
        {
            MXS_INFO("Transaction replay is enabled, ignoring routing hint while inside a transaction.");
        }
        else
        {
            return handle_hinted_target(buffer.get(), route_target);
        }
    }

    if (TARGET_IS_LAST_USED(route_target))
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

RWSplitSession::RoutingPlan RWSplitSession::resolve_route(const mxs::Buffer& buffer, const RouteInfo& info)
{
    RoutingPlan rval;
    rval.route_target = info.target();

    if (info.large_query())
    {
        /** We're processing a large query that's split across multiple packets.
         * Route it to the same backend where we routed the previous packet. */
        rval.route_target = TARGET_LAST_USED;
    }
    else if (trx_is_starting() && !trx_is_read_only() && should_try_trx_on_slave(rval.route_target))
    {
        // A normal transaction is starting and it qualifies for speculative routing
        rval.type = RoutingPlan::Type::OTRX_START;
        rval.route_target = TARGET_SLAVE;
    }
    else if (m_state == OTRX_STARTING || m_state == OTRX_ACTIVE)
    {
        if (trx_is_ending() || !info.is_trx_still_read_only())
        {
            rval.type = RoutingPlan::Type::OTRX_END;
        }

        rval.route_target = TARGET_LAST_USED;
    }

    if (rval.route_target != TARGET_ALL)
    {
        rval.target = get_target(buffer, rval.route_target);
    }

    return rval;
}

bool RWSplitSession::write_session_command(RWBackend* backend, mxs::Buffer buffer, uint8_t cmd)
{
    bool ok = true;
    mxs::Backend::response_type type = mxs::Backend::NO_RESPONSE;

    if (mxs_mysql_command_will_respond(cmd))
    {
        type = backend == m_sescmd_replier ? mxs::Backend::EXPECT_RESPONSE : mxs::Backend::IGNORE_RESPONSE;
    }

    if (backend->write(buffer.release(), type))
    {
        m_server_stats[backend->target()].inc_total();
        m_server_stats[backend->target()].inc_read();
        MXS_INFO("Route query to %s: %s", backend->is_master() ? "master" : "slave", backend->name());
    }
    else
    {
        MXS_ERROR("Failed to execute session command in %s", backend->name());
        backend->close();

        if (m_config.master_failure_mode == RW_FAIL_INSTANTLY && backend == m_current_master)
        {
            ok = false;
        }
    }

    return ok;
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
    MXS_INFO("Session write, routing to all servers.");
    mxs::Buffer buffer(querybuf);
    bool ok = true;

    if (!have_open_connections())
    {
        if (command == MXS_COM_QUIT)
        {
            // We have no open connections and opening one just to close it is pointless.
            return true;
        }
        else if (can_recover_servers())
        {
            // No connections are open, create one and execute the session command on it
            create_one_connection_for_sescmd();
        }
    }

    // Pick a new replier for each new session command. This allows the source server to change over
    // the course of the session. The replier will usually be the current master server.
    m_sescmd_replier = nullptr;

    for (RWBackend* backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            if (!m_sescmd_replier || backend == m_current_master)
            {
                // Return the result from this backend to the client
                m_sescmd_replier = backend;
            }
        }
    }

    if (m_sescmd_replier)
    {
        for (RWBackend* backend : m_raw_backends)
        {
            if (backend->in_use() && !write_session_command(backend, buffer, command))
            {
                ok = false;
            }
        }

        if (ok)
        {
            if (command == MXS_COM_STMT_CLOSE)
            {
                // Remove the command from the PS mapping
                m_qc.ps_erase(buffer.get());
                m_exec_map.erase(route_info().stmt_id());
            }
            else if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT)
                     || qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
            {
                mxb_assert(buffer.id() != 0 || qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT));
                m_qc.ps_store(buffer.get(), buffer.id());
            }
            else if (qc_query_is_type(type, QUERY_TYPE_DEALLOC_PREPARE))
            {
                mxb_assert(!mxs_mysql_is_ps_command(route_info().command()));
                m_qc.ps_erase(buffer.get());
            }

            m_router->update_max_sescmd_sz(protocol_data()->history.size());

            m_current_query = std::move(buffer);

            if (mxs_mysql_command_will_respond(command))
            {
                m_expected_responses++;
                mxb_assert(m_expected_responses == 1);
                MXS_INFO("Will return response from '%s' to the client", m_sescmd_replier->name());
            }
        }
        else
        {
            MXS_ERROR("Could not route session command `%s`. Connection status: %s",
                      mxs::extract_sql(buffer).c_str(), get_verbose_status().c_str());
        }
    }
    else
    {
        MXS_ERROR("No valid candidates for session command `%s`. Connection status: %s",
                  mxs::extract_sql(buffer).c_str(), get_verbose_status().c_str());
    }

    return ok;
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
    return m_prev_plan.target ? m_prev_plan.target : get_master_backend();
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
    if (m_config.max_slave_replication_lag.count() > 0)
    {
        conf_max_rlag = m_config.max_slave_replication_lag.count();
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
RWBackend* RWSplitSession::handle_hinted_target(const GWBUF* querybuf, route_target_t route_target)
{
    const char rlag_hint_tag[] = "max_slave_replication_lag";
    const int comparelen = sizeof(rlag_hint_tag);
    int config_max_rlag = get_max_replication_lag();    // From router configuration.
    RWBackend* target = nullptr;

    const auto& hints = querybuf->hints;
    for (auto it = hints.begin(); !target && it != hints.end(); it++)
    {
        const Hint* hint = &*it;
        if (hint->type == Hint::Type::ROUTE_TO_NAMED_SERVER)
        {
            // Set the name of searched backend server.
            const char* named_server = hint->data.c_str();
            MXS_INFO("Hint: route to server '%s'.", named_server);
            target = get_target_backend(BE_UNDEFINED, named_server, config_max_rlag);
            if (!target)
            {
                // Target may differ from the requested name if the routing target is locked, e.g. by a trx.
                // Target is null only if not locked and named server was not found or was invalid.
                if (mxb_log_should_log(LOG_INFO))
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
        else if (hint->type == Hint::Type::PARAMETER
                 && (strncasecmp(hint->data.c_str(), rlag_hint_tag, comparelen) == 0))
        {
            const char* str_val = hint->value.c_str();
            int hint_max_rlag = (int)strtol(str_val, nullptr, 10);
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

    if (route_info().is_ps_continuation())
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

    if (!target)
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

    if (m_config.delayed_retry && m_retry_duration >= m_config.delayed_retry_timeout.count())
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
                m_pSession->user().c_str(),
                m_pSession->client_remote().c_str(),
                errmsg);
}

bool RWSplitSession::trx_is_starting() const
{
    return m_pSession->protocol_data()->is_trx_starting();
}

bool RWSplitSession::trx_is_read_only() const
{
    return m_pSession->protocol_data()->is_trx_read_only();
}

bool RWSplitSession::trx_is_open() const
{
    return m_pSession->protocol_data()->is_trx_active();
}

bool RWSplitSession::trx_is_ending() const
{
    return m_pSession->protocol_data()->is_trx_ending();
}

bool RWSplitSession::should_replace_master(RWBackend* target)
{
    return m_config.master_reconnection
           &&   // We have a target server and it's not the current master
           target && target != m_current_master
           &&   // We are not inside a transaction (also checks for autocommit=1)
           (!trx_is_open() || trx_is_starting() || m_state == TRX_REPLAY)
           &&   // We are not locked to the old master
           !is_locked_to_master();
}

void RWSplitSession::discard_master_connection(const std::string& error)
{
    if (m_current_master && m_current_master->in_use())
    {
        m_current_master->close();
        m_current_master->set_close_reason(error);
        m_qc.master_replaced();
    }
}

void RWSplitSession::replace_master(RWBackend* target)
{
    discard_master_connection("The original master is not available");
    m_current_master = target;
}

bool RWSplitSession::should_migrate_trx(RWBackend* target)
{
    bool migrate = false;

    if (m_config.transaction_replay
        && m_state != TRX_REPLAY// Transaction replay is not active
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

    if (!m_locked_to_master && m_target_node == m_current_master)
    {
        // Reset the forced node as we're not permanently locked to it
        m_target_node = nullptr;
    }

    return target;
}

/**
 * @brief Handle writing to a target server
 *
 *  @return True on success
 */
bool RWSplitSession::handle_got_target(mxs::Buffer&& buffer, RWBackend* target, bool store)
{
    mxb_assert_message(target->in_use(), "Target must be in use before routing to it");

    MXS_INFO("Route query to %s: %s <", target->is_master() ? "master" : "slave", target->name());

    if (!m_target_node && trx_is_read_only())
    {
        // Lock the session to this node until the read-only transaction ends
        m_target_node = target;
    }

    uint8_t cmd = mxs_mysql_get_command(buffer.get());

    bool attempting_causal_read = false;

    if (route_info().large_query() || route_info().loading_data())
    {
        // Never store multi-packet queries or data sent during LOAD DATA LOCAL INFILE
        store = false;
    }
    else if (!is_locked_to_master())
    {
        mxb_assert(!mxs_mysql_is_ps_command(cmd)
                   || extract_binary_ps_id(buffer.get()) == route_info().stmt_id()
                   || extract_binary_ps_id(buffer.get()) == MARIADB_PS_DIRECT_EXEC_ID);

        // Attempt a causal read only when the query is routed to a slave
        attempting_causal_read = target->is_slave()
            && ((m_config.causal_reads == CausalReads::LOCAL && !m_gtid_pos.empty())
                || m_config.causal_reads == CausalReads::GLOBAL);

        if (cmd == MXS_COM_QUERY && attempting_causal_read)
        {
            GWBUF* tmp = buffer.release();
            buffer = add_prefix_wait_gtid(tmp);
            store = false;      // The storage for causal reads is done inside add_prefix_wait_gtid
        }
        else if (m_config.causal_reads != CausalReads::NONE && target->is_master())
        {
            gwbuf_set_type(buffer.get(), GWBUF_TYPE_TRACK_STATE);
        }

        if (target->is_slave() && (cmd == MXS_COM_QUERY || cmd == MXS_COM_STMT_EXECUTE))
        {
            target->select_started();
        }

        if (cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA)
        {
            // Track the targets of the COM_STMT_EXECUTE statements. This information is used to route all
            // COM_STMT_FETCH commands to the same server where the COM_STMT_EXECUTE was done.
            auto& info = m_exec_map[route_info().stmt_id()];
            info.target = target;
            MXS_INFO("%s on %s", STRPACKETTYPE(cmd), target->name());
        }
    }
    else if (cmd == MXS_COM_STMT_PREPARE)
    {
        // This is here to avoid a debug assertion in the ps_store_response call that is hit when we're locked
        // to the master due to strict_multi_stmt or strict_sp_calls and the user executes a prepared
        // statement. The previous PS ID is tracked in ps_store and asserted to be the same in
        // ps_store_result.
        m_qc.ps_store(buffer.get(), buffer.id());
    }

    if (store)
    {
        m_current_query.copy_from(buffer);
    }

    mxs::Backend::response_type response = mxs::Backend::NO_RESPONSE;

    if (route_info().expecting_response())
    {
        mxb_assert(!route_info().large_query());

        ++m_expected_responses;     // The server will reply to this command
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    if (m_config.transaction_replay && trx_is_open())
    {
        mxb_assert(!m_trx.target() || m_trx.target() == target);

        if (!m_trx.target())
        {
            MXS_INFO("Transaction starting on '%s'", target->name());
            m_trx.set_target(target);
        }
    }

    if (attempting_causal_read && cmd == MXS_COM_STMT_EXECUTE)
    {
        send_sync_query(target);
    }

    return target->write(buffer.release(), response);
}
