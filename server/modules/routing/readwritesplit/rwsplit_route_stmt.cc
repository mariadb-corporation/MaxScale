/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"

#include <utility>

#include <mysqld_error.h>

#include <maxbase/format.hh>

using std::chrono::seconds;
using maxscale::Parser;
using maxscale::RWBackend;
using mariadb::QueryClassifier;
using RouteInfo = QueryClassifier::RouteInfo;

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

namespace
{
const auto set_last_gtid = mariadb::create_query(
    "SET @@session.session_track_system_variables = CASE @@session.session_track_system_variables "
    "WHEN '*' THEN '*' WHEN '' THEN 'last_gtid' ELSE "
    "CONCAT(@@session.session_track_system_variables, ',last_gtid') END;");
}

bool RWSplitSession::prepare_connection(RWBackend* target)
{
    mxb_assert(!target->in_use());
    bool rval = target->connect();

    if (rval)
    {
        MXB_INFO("Connected to '%s'", target->name());
        mxb_assert(!target->is_waiting_result());

        if (m_config->causal_reads != CausalReads::NONE)
        {
            target->write(set_last_gtid.shallow_clone(), mxs::Backend::IGNORE_RESPONSE);
        }

        if (m_set_trx && target == m_current_master)
        {
            MXB_INFO("Re-executing SET TRANSACTION: %s", get_sql_string(m_set_trx).c_str());
            target->write(m_set_trx.shallow_clone(), mxs::Backend::IGNORE_RESPONSE);
        }
    }

    return rval;
}

void RWSplitSession::retry_query(GWBUF&& querybuf, int delay)
{
    mxb_assert(querybuf);

    // Route the query again later
    m_pSession->delay_routing(
        this, std::move(querybuf), std::chrono::seconds(delay), [this](GWBUF&& buffer){
        mxb_assert(m_pending_retries > 0);
        --m_pending_retries;

        return route_query(std::move(buffer));
    });

    ++m_retry_duration;

    mxb_assert(m_pending_retries >= 0);
    ++m_pending_retries;
}

bool RWSplitSession::have_connected_slaves() const
{
    return std::any_of(m_raw_backends.begin(), m_raw_backends.end(), [](auto b) {
        return b->is_slave() && b->in_use();
    });
}

bool RWSplitSession::should_try_trx_on_slave(route_target_t route_target) const
{
    return m_config->optimistic_trx                 // Optimistic transactions are enabled
           && !is_locked_to_master()                // Not locked to master
           && m_state == ROUTING                    // In normal routing mode
           && TARGET_IS_MASTER(route_target)        // The target type is master
           && have_connected_slaves()               // At least one connected slave
           && route_info().is_trx_still_read_only();// The start of the transaction is a read-only statement
}

void RWSplitSession::track_optimistic_trx(GWBUF& buffer, const RoutingPlan& plan)
{
    if (plan.type == RoutingPlan::Type::OTRX_START)
    {
        mxb_assert(plan.route_target == TARGET_SLAVE);
        m_state = OTRX_STARTING;
    }
    else if (plan.type == RoutingPlan::Type::OTRX_END)
    {
        mxb_assert(plan.route_target == TARGET_LAST_USED);

        if (trx_is_ending())
        {
            m_state = ROUTING;
        }
        else if (!route_info().is_trx_still_read_only())
        {
            // Not a plain SELECT, roll it back on the slave and start it on the master
            MXB_INFO("Rolling back current optimistic transaction");

            /**
             * Store the actual statement we were attempting to execute and
             * replace it with a ROLLBACK. The storing of the statement is
             * done here to avoid storage of the ROLLBACK.
             */
            m_current_query.buffer = std::exchange(buffer, mariadb::create_query("ROLLBACK"));
            m_state = OTRX_ROLLBACK;
        }
    }
    else if (m_state == OTRX_STARTING)
    {
        mxb_assert(plan.route_target == TARGET_LAST_USED);
        m_state = OTRX_ACTIVE;
    }
}

/**
 * Route query to all backends
 *
 * @param buffer Query to route
 */
void RWSplitSession::handle_target_is_all(GWBUF&& buffer)
{
    if (route_info().multi_part_packet())
    {
        continue_large_session_write(std::move(buffer));
    }
    else
    {
        route_session_write(std::move(buffer));
    }
}

std::optional<std::string> RWSplitSession::handle_routing_failure(GWBUF&& buffer, const RoutingPlan& plan)
{
    auto old_wait_gtid = m_wait_gtid;

    if (m_wait_gtid == READING_GTID)
    {
        mxb_assert(get_sql(buffer) == "SELECT @@gtid_current_pos");
        buffer = reset_gtid_probe();
    }

    mxb_assert_message(
        !std::all_of(m_raw_backends.begin(), m_raw_backends.end(), std::mem_fn(&mxs::RWBackend::has_failed)),
        "At least one functional backend should exist if a query was routed.");

    if (should_migrate_trx() || (trx_is_open() && old_wait_gtid == READING_GTID))
    {
        // If the connection to the previous transaction target is still open, we must close it to prevent the
        // transaction from being accidentally committed whenever a new transaction is started on it.
        discard_connection(m_trx.target(), "Closed due to transaction migration");

        try
        {
            // We're inside of a exception handler and this function might throw. If we fail to migrate the
            // transaction, we'll just return an error to the calling function instead of throwing another
            // exception.
            start_trx_migration(std::move(buffer));
        }
        catch (const RWSException& e)
        {
            return mxb::string_printf("A transaction is open that could not be retried: %s", e.what());
        }
    }
    else if (can_retry_query() || can_continue_trx_replay())
    {
        MXB_INFO("Delaying routing: %s", get_sql_string(buffer).c_str());
        retry_query(std::move(buffer));
    }
    else if (m_config->master_failure_mode == RW_ERROR_ON_WRITE)
    {
        MXB_INFO("Sending read-only error, no valid target found for %s",
                 route_target_to_string(plan.route_target));
        set_response(mariadb::create_error_packet(1, ER_OPTION_PREVENTS_STATEMENT, "HY000",
                                                  "The MariaDB server is running with the --read-only"
                                                  " option so it cannot execute this statement"));
        discard_connection(m_current_master, "The original primary is not available");
    }
    else if (plan.route_target == TARGET_MASTER
             && (!m_config->delayed_retry || m_retry_duration >= m_config->delayed_retry_timeout.count()))
    {
        // Cannot retry the query, log a message that routing has failed
        return get_master_routing_failure(plan.target != nullptr, m_current_master, plan.target);
    }
    else
    {
        return mxb::string_printf(
            "Could not find valid server for target type %s (%s: %s), closing connection. %s",
            route_target_to_string(plan.route_target), mariadb::cmd_to_string(buffer),
            get_sql_string(buffer).c_str(), get_verbose_status().c_str());
    }

    return {};
}

void RWSplitSession::send_readonly_error()
{
    int errnum = ER_OPTION_PREVENTS_STATEMENT;
    const char sqlstate[] = "HY000";
    const char errmsg[] = "The MariaDB server is running with the --read-only"
                          " option so it cannot execute this statement";

    mxs::ReplyRoute route;
    mxs::Reply reply;
    reply.set_error(errnum, sqlstate, sqlstate + sizeof(sqlstate) - 1, errmsg, errmsg + sizeof(errmsg) - 1);
    RouterSession::clientReply(mariadb::create_error_packet(1, errnum, sqlstate, errmsg), route, reply);
}

bool RWSplitSession::query_not_supported(const GWBUF& querybuf)
{
    bool unsupported = false;
    const RouteInfo& info = route_info();
    route_target_t route_target = info.target();
    GWBUF err;

    if (mxs_mysql_is_ps_command(info.command()) && info.stmt_id() == 0)
    {
        if (protocol_data().will_respond(querybuf))
        {
            // Unknown PS ID, can't route this query
            std::stringstream ss;
            ss << "Unknown prepared statement handler (" << extract_binary_ps_id(querybuf)
               << ") for " << mariadb::cmd_to_string(info.command()) << " given to MaxScale";
            err = mariadb::create_error_packet(1, ER_UNKNOWN_STMT_HANDLER, "HY000", ss.str().c_str());
            mxs::unexpected_situation(ss.str().c_str());
        }
        else
        {
            // The command doesn't expect a response which means we mustn't send one. Sending an unexpected
            // error will cause the client to go out of sync.
            unsupported = true;
        }
    }
    else if (TARGET_IS_ALL(route_target) && (TARGET_IS_MASTER(route_target) || TARGET_IS_SLAVE(route_target)))
    {
        // Conflicting routing targets. Return an error to the client.
        MXB_ERROR("Can't route %s '%s'. SELECT with session data modification is not "
                  "supported with `use_sql_variables_in=all`.",
                  mariadb::cmd_to_string(info.command()),
                  get_sql_string(querybuf).c_str());

        err = mariadb::create_error_packet(1, 1064, "42000", "Routing query to backend failed. "
                                                             "See the error log for further details.");
    }

    if (!err.empty())
    {
        set_response(std::move(err));
        unsupported = true;
    }

    return unsupported;
}

bool RWSplitSession::reuse_prepared_stmt(const GWBUF& buffer)
{
    const RouteInfo& info = route_info();

    if (info.command() == MXS_COM_STMT_PREPARE)
    {
        auto it = m_ps_cache.find(get_sql_string(buffer));

        if (it != m_ps_cache.end())
        {
            set_response(it->second.shallow_clone());
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
 * Routes a query to one or more backends
 *
 * @param buffer The query to route
 * @param plan   The routing plan
 */
void RWSplitSession::route_stmt(GWBUF&& buffer, const RoutingPlan& plan)
{
    const RouteInfo& info = route_info();
    route_target_t route_target = info.target();
    mxb_assert_message(m_state != OTRX_ROLLBACK, "OTRX_ROLLBACK should never happen when routing queries");

    if (m_config->reuse_ps && reuse_prepared_stmt(buffer))
    {
        mxb::atomic::add(&m_router->stats().n_ps_reused, 1, mxb::atomic::RELAXED);
    }
    else if (query_not_supported(buffer))
    {
        // A response was already sent to the client
    }
    else if (TARGET_IS_ALL(route_target))
    {
        handle_target_is_all(std::move(buffer));
    }
    else
    {
        route_single_stmt(std::move(buffer), plan);
    }

    update_statistics(plan);

    // The query was successfully routed, reset the retry duration and store the routing plan
    m_retry_duration = 0;
    m_prev_plan = plan;
}

void RWSplitSession::route_single_stmt(GWBUF&& buffer, const RoutingPlan& plan)
{
    auto target = plan.target;

    if (plan.route_target == TARGET_MASTER && target != m_current_master)
    {
        if (should_replace_master(target))
        {
            MXB_INFO("Replacing old primary '%s' with new primary '%s'",
                     m_current_master ? m_current_master->name() : "<no previous master>",
                     target->name());
            replace_master(target);
        }
        else if (target)
        {
            throw RWSException(std::move(buffer),
                               mxb::cat("Cannot replace old primary with '", target->name(), "'"));
        }
    }

    if (!target)
    {
        throw RWSException(std::move(buffer), "Could not find a valid target");
    }

    if (!target->in_use() && !prepare_connection(target))
    {
        throw RWSException(std::move(buffer), "Failed to connect to '", target->name(), "'");
    }

    track_optimistic_trx(buffer, plan);
    handle_got_target(std::move(buffer), target, plan.route_target);
}

RWBackend* RWSplitSession::get_target(const GWBUF& buffer, route_target_t route_target)
{
    RWBackend* rval = nullptr;

    if (trx_is_open() && m_trx.target() && trx_target_still_valid() && m_wait_gtid != READING_GTID)
    {
        // A transaction that has an existing target. Continue using it as long as it remains valid.
        return m_trx.target();
    }
    else if (route_info().is_ps_continuation())
    {
        return get_ps_continuation_backend();
    }

    // We can't use a switch here as the route_target is a bitfield where multiple values are set at one time.
    // Mostly this happens when the type is TARGET_NAMED_SERVER and TARGET_SLAVE due to a routing hint.
    if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
    {
        // If transaction replay is enabled and a transaction is open, hints must be ignored. This prevents
        // them from overriding the transaction target which is what would otherwise happen and which causes
        // problems.
        if (m_config->transaction_replay && trx_is_open() && m_trx.target())
        {
            MXB_INFO("Transaction replay is enabled, ignoring routing hint while inside a transaction.");
        }
        else
        {
            return handle_hinted_target(buffer, route_target);
        }
    }

    if (TARGET_IS_LAST_USED(route_target))
    {
        rval = get_last_used_backend();
    }
    else if (TARGET_IS_SLAVE(route_target))
    {
        rval = get_slave_backend(get_max_replication_lag());
    }
    else
    {
        mxb_assert(TARGET_IS_MASTER(route_target));
        rval = get_master_backend();
    }

    return rval;
}

RWSplitSession::RoutingPlan RWSplitSession::resolve_route(const GWBUF& buffer, const RouteInfo& info)
{
    RoutingPlan rval;
    rval.route_target = info.target();

    if (info.multi_part_packet())
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

bool RWSplitSession::write_session_command(RWBackend* backend, GWBUF&& buffer, uint8_t cmd)
{
    bool ok = true;
    mxs::Backend::response_type type = mxs::Backend::NO_RESPONSE;

    if (protocol_data().will_respond(buffer))
    {
        type = backend == m_sescmd_replier ? mxs::Backend::EXPECT_RESPONSE : mxs::Backend::IGNORE_RESPONSE;
    }

    if (backend->write(std::move(buffer), type))
    {
        auto& stats = m_router->local_server_stats()[backend->target()];
        stats.inc_total();
        stats.inc_read();
        MXB_INFO("Route query to %s: %s", backend == m_current_master ? "primary" : "replica",
                 backend->name());
    }
    else
    {
        MXB_ERROR("Failed to execute session command in %s", backend->name());
        backend->close();

        if (m_config->master_failure_mode == RW_FAIL_INSTANTLY && backend == m_current_master)
        {
            ok = false;
        }
    }

    return ok;
}

/**
 * Route query to all backends
 *
 * @param buffer  The query to be routed
 */
void RWSplitSession::route_session_write(GWBUF&& buffer)
{
    MXB_INFO("Session write, routing to all servers.");
    bool ok = true;
    uint8_t command = route_info().command();
    uint32_t type = route_info().type_mask();

    if (!have_open_connections() || need_master_for_sescmd())
    {
        MXB_INFO("No connections available for session command");

        if (command == MXS_COM_QUIT)
        {
            // We have no open connections and opening one just to close it is pointless.
            MXB_INFO("Ignoring COM_QUIT");
            return;
        }
        else if (can_recover_servers())
        {
            MXB_INFO("Attempting to create a connection");
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

    if (trx_is_open() && m_trx.target() && m_trx.target()->in_use())
    {
        // A transaction is open on a backend, use it instead.
        m_sescmd_replier = m_trx.target();
    }

    if (m_sescmd_replier && need_master_for_sescmd())
    {
        MXB_INFO("Cannot use '%s' for session command: transaction is open.", m_sescmd_replier->name());
        m_sescmd_replier = nullptr;
    }

    if (m_sescmd_replier)
    {
        for (RWBackend* backend : m_raw_backends)
        {
            if (backend->in_use() && !write_session_command(backend, buffer.shallow_clone(), command))
            {
                throw RWSException(std::move(buffer), "Could not route session command",
                                   " (", mariadb::cmd_to_string(command), ": ", get_sql(buffer), ").");
            }
        }

        if (command == MXS_COM_STMT_CLOSE)
        {
            auto stmt_id = route_info().stmt_id();
            auto it = std::find(m_exec_map.begin(), m_exec_map.end(), ExecInfo {stmt_id});

            if (it != m_exec_map.end())
            {
                m_exec_map.erase(it);
            }
        }

        m_current_query.buffer = std::move(buffer);

        if (protocol_data().will_respond(m_current_query.buffer))
        {
            m_expected_responses++;
            mxb_assert(m_expected_responses == 1);
            MXB_INFO("Will return response from '%s' to the client", m_sescmd_replier->name());
        }

        if (trx_is_open() && !m_trx.target())
        {
            m_trx.set_target(m_sescmd_replier);
        }
        else
        {
            mxb_assert_message(!trx_is_open() || m_trx.target() == m_sescmd_replier,
                               "Trx target is %s when m_sescmd_replier is %s while trx is open",
                               m_trx.target() ? m_trx.target()->name() : "nullptr",
                               m_sescmd_replier->name());
        }
    }
    else
    {
        throw RWSException(std::move(buffer), "No valid candidates for session command ",
                           "(", mariadb::cmd_to_string(command), ": ", get_sql(buffer), ").");
    }
}

RWBackend* RWSplitSession::get_hinted_backend(const char* name)
{
    RWBackend* rval = nullptr;

    for (auto backend : m_raw_backends)
    {
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
 * @brief Get the maximum replication lag for this router
 *
 * @param   rses    Router client session
 * @return  Replication lag from configuration or very large number
 */
int RWSplitSession::get_max_replication_lag()
{
    int conf_max_rlag = mxs::Target::RLAG_UNDEFINED;

    /** if there is no configured value, then longest possible int is used */
    if (m_config->max_replication_lag.count() > 0)
    {
        conf_max_rlag = m_config->max_replication_lag.count();
    }

    return conf_max_rlag;
}

/**
 * Handle hinted target query
 */
RWBackend* RWSplitSession::handle_hinted_target(const GWBUF& querybuf, route_target_t route_target)
{
    RWBackend* target = nullptr;

    for (const Hint& hint : querybuf.hints())
    {
        if (hint.type == Hint::Type::ROUTE_TO_NAMED_SERVER)
        {
            // Set the name of searched backend server.
            const char* named_server = hint.data.c_str();
            target = get_hinted_backend(named_server);
            MXB_INFO("Hint: route to server '%s', %s.", named_server,
                     target ? "found target" : "target not valid");
        }
        else if (hint.type == Hint::Type::PARAMETER
                 && (mxb::sv_case_eq(hint.data, "max_replication_lag")
                     || mxb::sv_case_eq(hint.data, "max_slave_replication_lag")))
        {
            auto hint_max_rlag = strtol(hint.value.c_str(), nullptr, 10);
            if (hint_max_rlag > 0)
            {
                target = get_slave_backend(hint_max_rlag);
                MXB_INFO("Hint: %s=%s, %s.", hint.data.c_str(), hint.value.c_str(),
                         target ? "found target" : "target not valid");
            }
            else
            {
                MXB_INFO("Ignoring invalid hint value: %s", hint.value.c_str());
            }
        }

        if (target)
        {
            break;
        }
    }

    if (!target)
    {
        // If no target so far, pick any available. TODO: should this be error instead?
        // Erroring here is more appropriate when namedserverfilter allows setting multiple target types
        // e.g. target=server1,->slave

        target = route_target & TARGET_SLAVE ?
            get_slave_backend(get_max_replication_lag()) : get_master_backend();
    }
    return target;
}

std::string RWSplitSession::get_master_routing_failure(bool found,
                                                       RWBackend* old_master,
                                                       RWBackend* curr_master)
{
    std::string errmsg;

    if (m_config->delayed_retry && m_retry_duration >= m_config->delayed_retry_timeout.count())
    {
        errmsg = mxb::string_printf("'delayed_retry_timeout' exceeded before a primary could be found");
    }
    else if (!found)
    {
        errmsg = mxb::string_printf("Could not find a valid master connection");
    }
    else if (old_master && curr_master && old_master->in_use())
    {
        /** We found a master but it's not the same connection */
        mxb_assert(old_master != curr_master);
        errmsg = mxb::string_printf(
            "Master server changed from '%s' to '%s'",
            old_master->name(),
            curr_master->name());
    }
    else if (old_master && old_master->in_use())
    {
        // TODO: Figure out if this is an impossible situation
        mxb_assert(!curr_master);
        /** We have an original master connection but we couldn't find it */
        errmsg = mxb::string_printf(
            "The connection to primary server '%s' is not available",
            old_master->name());
    }
    else
    {
        /** We never had a master connection, the session must be in read-only mode */
        if (m_config->master_failure_mode != RW_FAIL_INSTANTLY)
        {
            errmsg = mxb::string_printf(
                "Session is in read-only mode because it was created "
                "when no primary was available");
        }
        else
        {
            mxb_assert(old_master && !old_master->in_use());
            errmsg = "Was supposed to route to primary but the primary connection is closed";
        }
    }

    return mxb::string_printf("Write query received from %s@%s. %s. Closing client connection.",
                              m_pSession->user().c_str(),
                              m_pSession->client_remote().c_str(),
                              errmsg.c_str());
}

bool RWSplitSession::should_replace_master(RWBackend* target)
{
    return m_config->master_reconnection
           &&   // We have a target server and it's not the current master
           target && target != m_current_master
           &&   // We are not inside a transaction (also checks for autocommit=1)
           (!trx_is_open() || trx_is_starting() || (replaying_trx() && !m_trx.target()))
           &&   // We are not locked to the old master
           !is_locked_to_master()
           &&   // The server is actually labeled as a master
           target->is_master();
}

void RWSplitSession::discard_connection(mxs::RWBackend* target, const std::string& error)
{
    if (target && target->in_use())
    {
        MXB_INFO("Discarding connection to '%s': %s", target->name(), error.c_str());
        target->close();

        if (target == m_current_master)
        {
            m_qc.master_replaced();
        }
    }
}

void RWSplitSession::replace_master(RWBackend* target)
{
    discard_connection(m_current_master, "The original primary is not available");
    m_current_master = target;
}

bool RWSplitSession::trx_target_still_valid() const
{
    bool ok = false;

    if (auto target = m_trx.target(); target != nullptr && target->in_use())
    {
        ok = target->is_master() || (trx_is_read_only() && target->is_slave());
    }

    return ok;
}

bool RWSplitSession::should_migrate_trx() const
{
    bool migrate = false;

    if (m_config->transaction_replay
        && !replaying_trx()     // Transaction replay is not active
        && trx_is_open()        // We have an open transaction
        && m_can_replay_trx)    // The transaction can be replayed
    {
        if (!trx_target_still_valid())
        {
            migrate = true;
        }
    }

    return migrate;
}

void RWSplitSession::start_trx_migration(GWBUF&& querybuf)
{
    if (mxb_log_should_log(LOG_INFO) && m_trx.target())
    {
        MXB_INFO("Transaction target '%s' is no longer valid, replaying transaction", m_trx.target()->name());
    }

    /**
     * Stash the current query so that the transaction replay treats
     * it as if the query was interrupted.
     */
    m_current_query.buffer = std::move(querybuf);

    /**
     * After the transaction replay has been started, the rest of
     * the query processing needs to be skipped. This is done to avoid
     * the error logging done when no valid target is found for a query
     * as well as to prevent retrying of queries in the wrong order.
     */
    start_trx_replay();
}

/**
 * @brief Handle writing to a target server
 *
 * @param buffer       The query to route
 * @param target       The target where the query is sent
 * @param route_target The target type
 */
void RWSplitSession::handle_got_target(GWBUF&& buffer, RWBackend* target, route_target_t route_target)
{
    mxb_assert_message(target->in_use(), "Target must be in use before routing to it");

    MXB_INFO("Route query to %s: %s <", target == m_current_master ? "primary" : "replica", target->name());

    if (route_info().multi_part_packet() || route_info().load_data_active())
    {
        // Trailing multi-part packet, route it directly. Never stored or retried.
        if (!target->write(std::move(buffer), mxs::Backend::NO_RESPONSE))
        {
            throw RWSException("Failed to route query to '", target->name(), "'");
        }

        return;
    }

    uint8_t cmd = mariadb::get_command(buffer);

    // Attempt a causal read only when the query is routed to a slave
    bool is_causal_read = !is_locked_to_master() && target->is_slave() && should_do_causal_read();
    bool add_prefix = is_causal_read && cmd == MXS_COM_QUERY;
    bool send_sync = is_causal_read && cmd == MXS_COM_STMT_EXECUTE;

    if (send_sync && !send_sync_query(target))
    {
        throw RWSException(std::move(buffer), "Failed to send sync query");
    }

    bool will_respond = parser().command_will_respond(cmd);
    auto response = will_respond ? mxs::Backend::EXPECT_RESPONSE : mxs::Backend::NO_RESPONSE;

    if (!target->write(add_prefix ? add_prefix_wait_gtid(buffer) : buffer.shallow_clone(), response))
    {
        // Don't retry this even if we still have a reference to the buffer. If we do, all components below
        // this router will not be able to know that this is a replayed query and not a real one.
        throw RWSException("Failed to route query to '", target->name(), "'");
    }

    if (will_respond)
    {
        ++m_expected_responses;     // The server will reply to this command
    }

    if (Parser::type_mask_contains(route_info().type_mask(), mxs::sql::TYPE_NEXT_TRX))
    {
        m_set_trx = buffer.shallow_clone();
    }

    if (trx_is_open())
    {
        observe_trx(target);
    }

    if (cmd == MXS_COM_STMT_PREPARE || cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA)
    {
        observe_ps_command(buffer, target, cmd);
    }

    if (TARGET_IS_SLAVE(route_target))
    {
        target->select_started();
    }

    if (m_wait_gtid == GTID_READ_DONE)
    {
        // GTID sync done but causal read wasn't started because the conditions weren't met. Go back to
        // the default state since this now a normal read.
        m_wait_gtid = NONE;
    }

    if (is_causal_read)
    {
        buffer.add_hint(Hint::Type::ROUTE_TO_MASTER);

        if (add_prefix)
        {
            m_wait_gtid = WAITING_FOR_HEADER;
        }
    }

    // If delayed query retry is enabled, we need to store the current statement
    const bool store = m_state != OTRX_ROLLBACK && m_wait_gtid != READING_GTID
        && (m_config->delayed_retry || (TARGET_IS_SLAVE(route_target) && m_config->retry_failed_reads));

    if (store)
    {
        m_current_query.buffer = std::move(buffer);
    }
}

void RWSplitSession::observe_trx(RWBackend* target)
{
    if (m_config->transaction_replay && m_config->trx_retry_safe_commit
        && parser().type_mask_contains(route_info().type_mask(), mxs::sql::TYPE_COMMIT))
    {
        MXB_INFO("Transaction is about to commit, skipping replay if it fails.");
        m_can_replay_trx = false;
    }

    if (m_wait_gtid == READING_GTID)
    {
        // Ignore transaction target if a sync query is in progress. This prevents the transaction from
        // being assigned based on the target of the sync query which would end up causing all read-only
        // transactions to be routed to the master.
        MXB_INFO("Doing GTID sync on '%s' while transaction is open, transaction target is '%s'",
                 target->name(), m_trx.target() ? m_trx.target()->name() : "<none>");
    }
    else if (!m_trx.target())
    {
        MXB_INFO("Transaction starting on '%s'", target->name());
        m_trx.set_target(target);
    }
    else if (trx_is_starting())
    {
        MXB_INFO("Transaction did not finish on '%s' before a new one started on '%s'",
                 m_trx.target()->name(), target->name());
        m_trx.close();
        m_trx.set_target(target);
    }
    else
    {
        mxb_assert(m_trx.target() == target);
    }
}

void RWSplitSession::observe_ps_command(GWBUF& buffer, RWBackend* target, uint8_t cmd)
{
    if (cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA)
    {
        // Track the targets of the COM_STMT_EXECUTE statements. This information is used to route all
        // COM_STMT_FETCH commands to the same server where the COM_STMT_EXECUTE was done.
        auto stmt_id = route_info().stmt_id();
        auto it = std::find(m_exec_map.begin(), m_exec_map.end(), ExecInfo {stmt_id});

        if (it == m_exec_map.end())
        {
            m_exec_map.emplace_back(stmt_id, target);
        }
        else
        {
            it->target = target;
        }

        MXB_INFO("%s on %s", mariadb::cmd_to_string(cmd), target->name());
    }
}

/**
 * Get the backend where the last binary protocol command was executed
 */
mxs::RWBackend* RWSplitSession::get_ps_continuation_backend()
{
    mxs::RWBackend* target = nullptr;
    auto cmd = route_info().command();
    auto stmt_id = route_info().stmt_id();
    auto it = std::find(m_exec_map.begin(), m_exec_map.end(), ExecInfo {stmt_id});

    if (it != m_exec_map.end() && it->target)
    {
        auto prev_target = it->target;

        if (prev_target->in_use())
        {
            target = prev_target;
            MXB_INFO("%s on %s", mariadb::cmd_to_string(cmd), target->name());
        }
        else
        {
            MXB_ERROR("Old COM_STMT_EXECUTE target %s not in use, cannot "
                      "proceed with %s", prev_target->name(), mariadb::cmd_to_string(cmd));
        }
    }
    else
    {
        MXB_WARNING("Unknown statement ID %u used in %s", stmt_id, mariadb::cmd_to_string(cmd));
    }

    return target;
}
