/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
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

#include <maxbase/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/modutil.hh>
#include <maxscale/modutil.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/session_command.hh>
#include <maxscale/utils.hh>

using namespace maxscale;

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

void RWSplitSession::handle_connection_keepalive(RWBackend* target)
{
    mxb_assert(target);
    MXB_AT_DEBUG(int nserv = 0);
    /** Each heartbeat is 1/10th of a second */
    int64_t keepalive = m_config.connection_keepalive * 10;
    int64_t now = mxs_clock();

    if (now - m_last_keepalive_check > keepalive)
    {
        for (const auto& backend : m_raw_backends)
        {
            if (backend->in_use() && backend != target && !backend->is_waiting_result())
            {
                MXB_AT_DEBUG(nserv++);
                int64_t diff = now - std::max(backend->dcb()->last_read, backend->dcb()->last_write);

                if (diff > keepalive)
                {
                    MXS_INFO("Pinging %s, idle for %ld seconds",
                             backend->name(),
                             MXS_CLOCK_TO_SEC(diff));
                    modutil_ignorable_ping(backend->dcb());
                }
            }
        }
    }

    mxb_assert(nserv < m_nbackends);
}

bool RWSplitSession::prepare_connection(RWBackend* target)
{
    mxb_assert(!target->in_use());
    bool rval = target->connect(m_client->session, &m_sescmd_list);

    if (rval)
    {
        MXS_INFO("Connected to '%s'", target->name());

        if (target->is_waiting_result())
        {
            mxb_assert_message(!m_sescmd_list.empty() && target->has_session_commands(),
                               "Session command list must not be empty and target "
                               "should have unfinished session commands.");
            m_expected_responses++;
        }
    }

    return rval;
}

bool RWSplitSession::prepare_target(RWBackend* target, route_target_t route_target)
{
    bool rval = true;

    // Check if we need to connect to the server in order to use it
    if (!target->in_use())
    {
        mxb_assert(target->can_connect() && can_recover_servers());
        mxb_assert(!TARGET_IS_MASTER(route_target) || m_config.master_reconnection);
        rval = prepare_connection(target);
    }

    return rval;
}

bool RWSplitSession::create_one_connection()
{
    mxb_assert(can_recover_servers());

    // Try to first find a master if we are allowed to connect to one
    if (m_config.lazy_connect || m_config.master_reconnection)
    {
        for (auto backend : m_raw_backends)
        {
            if (backend->can_connect() && backend->is_master())
            {
                if (prepare_target(backend, TARGET_MASTER))
                {
                    if (!m_current_master)
                    {
                        MXS_INFO("Chose '%s' as master due to session write", backend->name());
                        m_current_master = backend;
                    }

                    return true;
                }
            }
        }
    }

    // If no master was found, find a slave
    for (auto backend : m_raw_backends)
    {
        if (backend->can_connect() && backend->is_slave() && prepare_target(backend, TARGET_SLAVE))
        {
            return true;
        }
    }

    // No servers are available
    return false;
}

void RWSplitSession::retry_query(GWBUF* querybuf, int delay)
{
    mxb_assert(querybuf);
    // Try to route the query again later
    MXS_SESSION* session = m_client->session;

    /**
     * Used to distinct retried queries from new ones while we're doing transaction replay.
     * Not the cleanest way to do things but this will have to do for 2.3.
     *
     * TODO: Figure out a way to "cork" the client DCB as that would remove the need for this and be
     * architecturally more clear.
     */
    gwbuf_set_type(querybuf, GWBUF_TYPE_REPLAYED);

    session_delay_routing(session, router_as_downstream(session), querybuf, delay);
    ++m_retry_duration;
}

namespace
{

void replace_binary_ps_id(GWBUF* buffer, uint32_t id)
{
    uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
    gw_mysql_set_byte4(ptr, id);
}

uint32_t extract_binary_ps_id(GWBUF* buffer)
{
    uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
    return gw_mysql_get_byte4(ptr);
}
}

bool RWSplitSession::have_connected_slaves() const
{
    for (const auto& b : m_raw_backends)
    {
        if (b->is_slave() && b->in_use())
        {
            return true;
        }
    }

    return false;
}

bool RWSplitSession::should_try_trx_on_slave(route_target_t route_target) const
{
    return m_config.optimistic_trx          // Optimistic transactions are enabled
           && !is_locked_to_master()        // Not locked to master
           && !m_is_replay_active           // Not replaying a transaction
           && m_otrx_state == OTRX_INACTIVE // Not yet in optimistic mode
           && TARGET_IS_MASTER(route_target)// The target type is master
           && have_connected_slaves();      // At least one connected slave
}

bool RWSplitSession::track_optimistic_trx(GWBUF** buffer)
{
    bool store_stmt = true;

    if (session_trx_is_ending(m_client->session))
    {
        m_otrx_state = OTRX_INACTIVE;
    }
    else if (!m_qc.is_trx_still_read_only())
    {
        // Not a plain SELECT, roll it back on the slave and start it on the master
        MXS_INFO("Rolling back current optimistic transaction");

        // Note: This clone is here because routeQuery will always free the buffer
        m_current_query.reset(gwbuf_clone(*buffer));

        /**
         * Store the actual statement we were attempting to execute and
         * replace it with a ROLLBACK. The storing of the statement is
         * done here to avoid storage of the ROLLBACK.
         */
        *buffer = modutil_create_query("ROLLBACK");
        store_stmt = false;
        m_otrx_state = OTRX_ROLLBACK;
    }

    return store_stmt;
}

/**
 * Routing function. Find out query type, backend type, and target DCB(s).
 * Then route query to found target(s).
 * @param querybuf  GWBUF including the query
 *
 * @return true if routing succeed or if it failed due to unsupported query.
 * false if backend failure was encountered.
 */
bool RWSplitSession::route_single_stmt(GWBUF* querybuf)
{
    mxb_assert_message(m_otrx_state != OTRX_ROLLBACK,
                       "OTRX_ROLLBACK should never happen when routing queries");
    bool succp = false;
    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    uint32_t stmt_id = info.stmt_id();
    uint8_t command = info.command();
    uint32_t qtype = info.type_mask();
    route_target_t route_target = info.target();

    RWBackend* target = nullptr;

    if (TARGET_IS_ALL(route_target))
    {
        succp = handle_target_is_all(route_target, querybuf, command, qtype);
    }
    else
    {
        update_trx_statistics();

        auto next_master = get_target_backend(BE_MASTER, NULL, SERVER::RLAG_UNDEFINED);

        if (should_replace_master(next_master))
        {
            MXS_INFO("Replacing old master '%s' with new master '%s'",
                     m_current_master ?
                     m_current_master->name() : "<no previous master>",
                     next_master->name());
            replace_master(next_master);
        }

        if (m_qc.is_trx_starting()                          // A transaction is starting
            && !session_trx_is_read_only(m_client->session) // Not explicitly read-only
            && should_try_trx_on_slave(route_target))       // Qualifies for speculative routing
        {
            // Speculatively start routing the transaction to a slave
            m_otrx_state = OTRX_STARTING;
            route_target = TARGET_SLAVE;
        }
        else if (m_otrx_state == OTRX_STARTING)
        {
            // Transaction was started, begin active tracking of its progress
            m_otrx_state = OTRX_ACTIVE;
        }

        // If delayed query retry is enabled, we need to store the current statement
        bool store_stmt = m_config.delayed_retry;

        if (m_qc.large_query())
        {
            /** We're processing a large query that's split across multiple packets.
             * Route it to the same backend where we routed the previous packet. */
            mxb_assert(m_prev_target);
            target = m_prev_target;
            succp = true;
        }
        else if (m_otrx_state == OTRX_ACTIVE)
        {
            /** We are speculatively executing a transaction to the slave, keep
             * routing queries to the same server. If the query modifies data,
             * a rollback is initiated on the slave server. */
            store_stmt = track_optimistic_trx(&querybuf);
            target = m_prev_target;
            succp = true;
        }
        else if (mxs_mysql_is_ps_command(command) && stmt_id == 0)
        {
            // Unknown prepared statement ID
            succp = send_unknown_ps_error(extract_binary_ps_id(querybuf));
        }
        else if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
        {
            if ((target = handle_hinted_target(querybuf, route_target)))
            {
                succp = true;
            }
        }
        else if (TARGET_IS_LAST_USED(route_target))
        {
            if ((target = get_last_used_backend()))
            {
                succp = true;
            }
        }
        else if (TARGET_IS_SLAVE(route_target))
        {
            if ((target = handle_slave_is_target(command, stmt_id)))
            {
                succp = true;

                bool is_sql = command == MXS_COM_QUERY || command == MXS_COM_STMT_EXECUTE;
                if (is_sql)
                {
                    target->select_started();

                    target->response_stat().query_started();

                    if (m_config.retry_failed_reads)
                    {
                        store_stmt = true;
                    }
                }
            }
        }
        else if (TARGET_IS_MASTER(route_target))
        {
            if (m_config.causal_reads)
            {
                gwbuf_set_type(querybuf, GWBUF_TYPE_TRACK_STATE);
            }

            succp = handle_master_is_target(&target);

            if (!succp && should_migrate_trx(target))
            {
                return start_trx_migration(target, querybuf);
            }
        }

        if (succp && target)
        {
            // We have a valid target, reset retry duration
            m_retry_duration = 0;

            if (!prepare_target(target, route_target))
            {
                // The connection to target was down and we failed to reconnect
                succp = false;
            }
            else if (target->has_session_commands())
            {
                // We need to wait until the session commands are executed
                m_query_queue.emplace_front(gwbuf_clone(querybuf));
                MXS_INFO("Queuing query until '%s' completes session command", target->name());
            }
            else
            {
                // Target server was found and is in the correct state
                succp = handle_got_target(querybuf, target, store_stmt);
            }
        }
        else if (can_retry_query() || can_continue_trx_replay())
        {
            retry_query(gwbuf_clone(querybuf));
            succp = true;
            MXS_INFO("Delaying routing: %s", extract_sql(querybuf).c_str());
        }
        else if (m_config.master_failure_mode != RW_ERROR_ON_WRITE)
        {
            MXS_ERROR("Could not find valid server for target type %s, closing "
                      "connection.", route_target_to_string(route_target));
        }
    }

    if (succp && target && m_config.connection_keepalive && !TARGET_IS_ALL(route_target))
    {
        handle_connection_keepalive(target);
    }

    return succp;
}

/**
 * Compress session command history
 *
 * This function removes data duplication by sharing buffers between session
 * commands that have identical data. Only one copy of the actual data is stored
 * for each unique session command.
 *
 * @param sescmd Executed session command
 */
void RWSplitSession::compress_history(mxs::SSessionCommand& sescmd)
{
    auto eq = [&](mxs::SSessionCommand& scmd) {
            return scmd->eq(*sescmd);
        };

    auto first = std::find_if(m_sescmd_list.begin(), m_sescmd_list.end(), eq);

    if (first != m_sescmd_list.end())
    {
        // Duplicate command, use a reference of the old command instead of duplicating it
        sescmd->mark_as_duplicate(**first);
    }
}

void RWSplitSession::continue_large_session_write(GWBUF* querybuf, uint32_t type)
{
    for (auto it = m_raw_backends.begin(); it != m_raw_backends.end(); it++)
    {
        RWBackend* backend = *it;

        if (backend->in_use())
        {
            backend->continue_session_command(gwbuf_clone(querybuf));
        }
    }
}

void RWSplitSession::prune_to_position(uint64_t pos)
{
    /** Prune all completed responses before a certain position */
    ResponseMap::iterator it = m_sescmd_responses.lower_bound(pos);

    if (it != m_sescmd_responses.end())
    {
        // Found newer responses that were returned after this position
        m_sescmd_responses.erase(m_sescmd_responses.begin(), it);
    }
    else
    {
        // All responses are older than the requested position
        m_sescmd_responses.clear();
    }
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
    if (mxs_mysql_is_ps_command(m_qc.current_route_info().command()))
    {
        if (command == MXS_COM_STMT_CLOSE)
        {
            // Remove the command from the PS mapping
            m_qc.ps_erase(querybuf);
            m_exec_map.erase(m_qc.current_route_info().stmt_id());
        }

        /**
         * Replace the ID with our internal one, the backends will replace it with their own ID
         * when the packet is being written. We use the internal ID when we store the command
         * to remove the need for extra conversions from external to internal form when the command
         * is being replayed on a server.
         */
        replace_binary_ps_id(querybuf, m_qc.current_route_info().stmt_id());
    }

    /** The SessionCommand takes ownership of the buffer */
    uint64_t id = m_sescmd_count++;
    mxs::SSessionCommand sescmd(new mxs::SessionCommand(querybuf, id));
    bool expecting_response = mxs_mysql_command_will_respond(command);
    int nsucc = 0;
    uint64_t lowest_pos = id;

    if (expecting_response)
    {
        gwbuf_set_type(querybuf, GWBUF_TYPE_COLLECT_RESULT);
    }

    if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT)
        || qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
    {
        m_qc.ps_store(querybuf, id);
    }
    else if (qc_query_is_type(type, QUERY_TYPE_DEALLOC_PREPARE))
    {
        mxb_assert(!mxs_mysql_is_ps_command(m_qc.current_route_info().command()));
        m_qc.ps_erase(querybuf);
    }

    MXS_INFO("Session write, routing to all servers.");
    bool attempted_write = false;

    for (auto it = m_raw_backends.begin(); it != m_raw_backends.end(); it++)
    {
        RWBackend* backend = *it;

        if (backend->in_use())
        {
            attempted_write = true;
            backend->append_session_command(sescmd);

            uint64_t current_pos = backend->next_session_command()->get_position();

            if (current_pos < lowest_pos)
            {
                lowest_pos = current_pos;
            }

            if (backend->execute_session_command())
            {
                nsucc += 1;
                mxb::atomic::add(&backend->server()->stats.packets, 1, mxb::atomic::RELAXED);
                m_server_stats[backend->server()].total++;
                m_server_stats[backend->server()].read++;

                if (expecting_response)
                {
                    m_expected_responses++;
                }

                MXS_INFO("Route query to %s: %s \t%s",
                         backend->is_master() ? "master" : "slave",
                         backend->name(),
                         backend->uri());
            }
            else
            {
                backend->close();

                if (m_config.master_failure_mode == RW_FAIL_INSTANTLY && backend == m_current_master)
                {
                    MXS_ERROR("Failed to execute session command in Master: %s (%s)",
                              backend->name(), backend->uri());
                    return false;
                }
                else
                {
                    MXS_ERROR("Failed to execute session command in %s (%s)",
                              backend->name(), backend->uri());
                }
            }
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
        prune_to_position(m_sescmd_list.front()->get_position());
        m_sescmd_list.pop_front();
    }

    if (m_config.disable_sescmd_history)
    {
        prune_to_position(lowest_pos);
    }
    else
    {
        compress_history(sescmd);
        m_sescmd_list.push_back(sescmd);
    }

    if (!attempted_write && can_recover_servers())
    {
        mxb_assert(nsucc == 0);

        // If no connections are open, create one and execute the session command on it
        if (create_one_connection())
        {
            nsucc = 1;
        }
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
    /** get root master from available servers */
    RWBackend* master = get_root_master(m_raw_backends, m_current_master);

    if (master)
    {
        if (master->in_use()
            || (m_config.master_reconnection && master->can_connect() && can_recover_servers()))
        {
            if (can_continue_using_master(master))
            {
                rval = master;
            }
            else
            {
                MXS_ERROR("Server '%s' does not have the master state and "
                          "can't be chosen as the master.",
                          master->name());
            }
        }
        else
        {
            MXS_ERROR("Cannot choose server '%s' as the master because it is not "
                      "in use and a new connection to it cannot be created. Connection status: %s",
                      master->name(), get_verbose_status().c_str());
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
    if (m_target_node && session_trx_is_read_only(m_client->session))
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
    int conf_max_rlag = SERVER::RLAG_UNDEFINED;

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
                        if (strcmp(a->server()->name(), named_server) == 0)
                        {
                            status = a->server()->status_string();
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

        if (it != m_exec_map.end())
        {
            if (it->second->in_use())
            {
                target = it->second;
                MXS_INFO("%s on %s", STRPACKETTYPE(cmd), target->name());
            }
            else
            {
                MXS_ERROR("Old COM_STMT_EXECUTE target %s not in use, cannot "
                          "proceed with %s", it->second->name(), STRPACKETTYPE(cmd));
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
        m_server_stats[target->server()].read++;
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
    /** Both backends should either be empty, not connected or the DCB should
     * be a backend (the last check is slightly redundant). */
    mxb_assert(!old_master || !old_master->in_use()
               || old_master->dcb()->role == DCB::Role::BACKEND);
    mxb_assert(!curr_master || !curr_master->in_use()
               || curr_master->dcb()->role == DCB::Role::BACKEND);
    char errmsg[SERVER::MAX_ADDRESS_LEN* 2 + 100];      // Extra space for error message

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
                m_client->user,
                m_client->remote,
                errmsg);
}

bool RWSplitSession::trx_is_starting()
{
    return session_trx_is_active(m_client->session)
           && qc_query_is_type(m_qc.current_route_info().type_mask(), QUERY_TYPE_BEGIN_TRX);
}

bool RWSplitSession::should_replace_master(RWBackend* target)
{
    return m_config.master_reconnection
           &&   // We have a target server and it's not the current master
           target && target != m_current_master
           &&   // We are not inside a transaction (also checks for autocommit=1)
           (!session_trx_is_active(m_client->session) || trx_is_starting() || m_is_replay_active)
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
    return m_config.transaction_replay
           &&   // We have a target server and it's not the current master
           target && target != m_current_master
           &&   // Transaction replay is not active (replay is only attempted once)
           !m_is_replay_active
           &&   // We have an open transaction
           session_trx_is_active(m_client->session)
           &&   // The transaction can be replayed
           m_can_replay_trx;
}

bool RWSplitSession::start_trx_migration(RWBackend* target, GWBUF* querybuf)
{
    MXS_INFO("Starting transaction migration to '%s'", target->name());

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
bool RWSplitSession::handle_master_is_target(RWBackend** dest)
{
    RWBackend* target = get_target_backend(BE_MASTER, NULL, SERVER::RLAG_UNDEFINED);
    bool succp = true;

    if (target && target == m_current_master)
    {
        mxb::atomic::add(&m_router->stats().n_master, 1, mxb::atomic::RELAXED);
        m_server_stats[target->server()].write++;
    }
    else
    {
        succp = false;
        /** The original master is not available, we can't route the write */
        if (m_config.master_failure_mode == RW_ERROR_ON_WRITE)
        {
            succp = send_readonly_error(m_client);

            if (m_current_master && m_current_master->in_use())
            {
                m_current_master->close();
                m_current_master->set_close_reason("The original master is not available");
            }
        }
        else if (!m_config.delayed_retry
                 || m_retry_duration >= m_config.delayed_retry_timeout)
        {
            // Cannot retry the query, log a message that routing has failed
            log_master_routing_failure(succp, m_current_master, target);
        }
    }

    if (!m_config.strict_multi_stmt && !m_config.strict_sp_calls
        && m_target_node == m_current_master)
    {
        /** Reset the forced node as we're in relaxed multi-statement mode */
        m_target_node = nullptr;
    }

    *dest = target;
    return succp;
}

/*
 * Add a wait gitd query in front of user's query to achive causal read;
 *
 * @param inst   RWSplit
 * @param rses   RWSplitSession
 * @param server SERVER
 * @param origin origin send buffer
 * @return       A new buffer contains wait statement and origin query
 */
GWBUF* RWSplitSession::add_prefix_wait_gtid(SERVER* server, GWBUF* origin)
{

    /**
     * Pack wait function and client query into a multistatments will save a round trip latency,
     * and prevent the client query being executed on timeout.
     * For example:
     * SET @maxscale_secret_variable=(SELECT CASE WHEN MASTER_GTID_WAIT('232-1-1', 10) = 0
     * THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END); SELECT * FROM `city`;
     * when MASTER_GTID_WAIT('232-1-1', 0.05) == 1 (timeout), it will return
     * an error, and SELECT * FROM `city` will not be executed, then we can retry
     * on master;
     **/

    GWBUF* rval = origin;
    const char* wait_func = (server->type() == SERVER::Type::MARIADB) ? MARIADB_WAIT_GTID_FUNC :
        MYSQL_WAIT_GTID_FUNC;
    const char* gtid_wait_timeout = m_config.causal_reads_timeout.c_str();
    const char* gtid_position = m_gtid_pos.c_str();

    /* Create a new buffer to store prefix sql */
    size_t prefix_len = strlen(gtid_wait_stmt) + strlen(gtid_position)
        + strlen(gtid_wait_timeout) + strlen(wait_func);

    // Only do the replacement if it fits into one packet
    if (gwbuf_length(origin) + prefix_len < GW_MYSQL_MAX_PACKET_LEN + MYSQL_HEADER_LEN)
    {
        char prefix_sql[prefix_len];
        snprintf(prefix_sql, prefix_len, gtid_wait_stmt, wait_func, gtid_position, gtid_wait_timeout);
        GWBUF* prefix_buff = modutil_create_query(prefix_sql);

        // Copy the original query in case it fails on the slave
        m_current_query.copy_from(origin);

        /* Trim origin to sql, Append origin buffer to the prefix buffer */
        uint8_t header[MYSQL_HEADER_LEN];
        gwbuf_copy_data(origin, 0, MYSQL_HEADER_LEN, header);
        /* Command length = 1 */
        size_t origin_sql_len = MYSQL_GET_PAYLOAD_LEN(header) - 1;
        /* Trim mysql header and command */
        origin = gwbuf_consume(origin, MYSQL_HEADER_LEN + 1);
        rval = gwbuf_append(prefix_buff, origin);

        /* Modify totol length: Prefix sql len + origin sql len + command len */
        size_t new_payload_len = strlen(prefix_sql) + origin_sql_len + 1;
        gw_mysql_set_byte3(GWBUF_DATA(rval), new_payload_len);
    }

    return rval;
}

/**
 * @brief Handle writing to a target server
 *
 *  @return True on success
 */
bool RWSplitSession::handle_got_target(GWBUF* querybuf, RWBackend* target, bool store)
{
    mxb_assert_message(target->in_use(), "Target must be in use before routing to it");
    mxb_assert_message(!target->has_session_commands(), "The session command cursor must not be active");

    /**
     * TODO: This effectively disables pipelining of queries, very bad for batch insert performance. Replace
     *       with proper, per server tracking of which responses need to be sent to the client. This would
     *       also solve MXS-2009 by speeding up session commands.
     */
    mxb_assert_message(target->get_reply_state() == REPLY_STATE_DONE || m_qc.large_query(),
                       "Node must be idle when routing queries to it");

    MXS_INFO("Route query to %s: %s \t%s <", target->is_master() ? "master" : "slave",
             target->name(), target->uri());

    if (!m_target_node && session_trx_is_read_only(m_client->session))
    {
        // Lock the session to this node until the read-only transaction ends
        m_target_node = target;
    }

    mxs::Backend::response_type response = mxs::Backend::NO_RESPONSE;
    uint8_t cmd = mxs_mysql_get_command(querybuf);
    GWBUF* send_buf = gwbuf_clone(querybuf);

    if (m_config.causal_reads && cmd == COM_QUERY && !m_gtid_pos.empty() && target->is_slave())
    {
        // Perform the causal read only when the query is routed to a slave
        send_buf = add_prefix_wait_gtid(target->server(), send_buf);
        m_wait_gtid = WAITING_FOR_HEADER;

        // The storage for causal reads is done inside add_prefix_wait_gtid
        store = false;
    }

    if (m_qc.load_data_state() != QueryClassifier::LOAD_DATA_ACTIVE
        && !m_qc.large_query() && mxs_mysql_command_will_respond(cmd))
    {
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    bool large_query = is_large_query(querybuf);
    uint32_t orig_id = 0;

    if (!is_locked_to_master() && mxs_mysql_is_ps_command(cmd) && !m_qc.large_query())
    {
        // Store the original ID in case routing fails
        orig_id = extract_binary_ps_id(querybuf);
        // Replace the ID with our internal one, the backends will replace it with their own ID
        replace_binary_ps_id(querybuf, m_qc.current_route_info().stmt_id());
    }

    /**
     * If we are starting a new query, we use RWBackend::write, otherwise we use
     * RWBackend::continue_write to continue an ongoing query. RWBackend::write
     * will do the replacement of PS IDs which must not be done if we are
     * continuing an ongoing query.
     */
    bool success = target->write(send_buf, response);

    if (orig_id)
    {
        // Put the original ID back in case we try to route the query again
        replace_binary_ps_id(querybuf, orig_id);
    }

    if (success)
    {
        if (store)
        {
            m_current_query.copy_from(querybuf);
        }

        mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);
        mxb::atomic::add(&target->server()->stats.packets, 1, mxb::atomic::RELAXED);
        m_server_stats[target->server()].total++;

        if (!m_qc.large_query() && response == mxs::Backend::EXPECT_RESPONSE)
        {
            /** The server will reply to this command */
            m_expected_responses++;

            if (m_qc.load_data_state() == QueryClassifier::LOAD_DATA_END)
            {
                /** The final packet in a LOAD DATA LOCAL INFILE is an empty packet
                 * to which the server responds with an OK or an ERR packet */
                mxb_assert(gwbuf_length(querybuf) == 4);
                m_qc.set_load_data_state(QueryClassifier::LOAD_DATA_INACTIVE);
                session_set_load_active(m_pSession, false);
            }
        }

        m_qc.set_large_query(large_query);

        // Store the current target
        m_prev_target = target;

        if (m_target_node
            && session_trx_is_read_only(m_client->session)
            && session_trx_is_ending(m_client->session))
        {
            // Read-only transaction is over, stop routing queries to a specific node
            m_target_node = nullptr;
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
        m_exec_map[m_qc.current_route_info().stmt_id()] = target;
        MXS_INFO("%s on %s: %s", STRPACKETTYPE(cmd), target->name(), target->uri());
    }

    return success;
}
