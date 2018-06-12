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

#include "readwritesplit.hh"
#include "rwsplitsession.hh"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <maxscale/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/modutil.h>
#include <maxscale/modutil.hh>
#include <maxscale/router.h>
#include <maxscale/server.h>
#include <maxscale/session_command.hh>
#include <maxscale/utils.hh>

using namespace maxscale;

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

extern int (*criteria_cmpfun[LAST_CRITERIA])(const SRWBackend&, const SRWBackend&);

/**
 * Find out which of the two backend servers has smaller value for select
 * criteria property.
 *
 * @param cand  previously selected candidate
 * @param new   challenger
 * @param sc    select criteria
 *
 * @return pointer to backend reference of that backend server which has smaller
 * value in selection criteria. If either reference pointer is NULL then the
 * other reference pointer value is returned.
 */
static SRWBackend compare_backends(SRWBackend a, SRWBackend b, select_criteria_t sc)
{
    int (*p)(const SRWBackend&, const SRWBackend&) = criteria_cmpfun[sc];

    if (!a)
    {
        return b;
    }
    else if (!b)
    {
        return a;
    }

    return p(a, b) <= 0 ? a : b;
}

void RWSplitSession::handle_connection_keepalive(SRWBackend& target)
{
    ss_dassert(target);
    ss_debug(int nserv = 0);
    /** Each heartbeat is 1/10th of a second */
    int keepalive = m_config.connection_keepalive * 10;

    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        SRWBackend backend = *it;

        if (backend->in_use() && backend != target && !backend->is_waiting_result())
        {
            ss_debug(nserv++);
            int diff = mxs_clock() - backend->dcb()->last_read;

            if (diff > keepalive)
            {
                MXS_INFO("Pinging %s, idle for %ld seconds",
                         backend->name(), MXS_CLOCK_TO_SEC(diff));
                modutil_ignorable_ping(backend->dcb());
            }
        }
    }

    ss_dassert(nserv < m_nbackends);
}

bool RWSplitSession::prepare_target(SRWBackend& target, route_target_t route_target)
{
    bool rval = true;

    // Check if we need to connect to the server in order to use it
    if (!target->in_use())
    {
        ss_dassert(target->can_connect() && can_recover_servers());
        ss_dassert(!TARGET_IS_MASTER(route_target) || m_config.master_reconnection);
        rval = target->connect(m_client->session, &m_sescmd_list);
    }

    return rval;
}

void RWSplitSession::retry_query(GWBUF* querybuf, int delay)
{
    ss_dassert(querybuf);
    // Try to route the query again later
    MXS_SESSION* session = m_client->session;
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

}

/**
 * Routing function. Find out query type, backend type, and target DCB(s).
 * Then route query to found target(s).
 * @param querybuf  GWBUF including the query
 *
 * @return true if routing succeed or if it failed due to unsupported query.
 * false if backend failure was encountered.
 */
bool RWSplitSession::route_single_stmt(GWBUF *querybuf)
{
    bool succp = false;

    const QueryClassifier::RouteInfo& info = m_qc.current_route_info();
    uint32_t stmt_id = info.stmt_id();
    uint8_t command = info.command();
    uint32_t qtype = info.type_mask();
    route_target_t route_target = info.target();
    bool not_locked_to_master = !is_locked_to_master();

    if (not_locked_to_master && mxs_mysql_is_ps_command(command) && !m_qc.large_query())
    {
        /** Replace the client statement ID with our internal one only if the
         * target node is not the current master */
        replace_binary_ps_id(querybuf, stmt_id);
    }

    SRWBackend target;

    if (TARGET_IS_ALL(route_target))
    {
        succp = handle_target_is_all(route_target, querybuf, command, qtype);
    }
    else
    {
        // If delayed query retry is enabled, we need to store the current statement
        bool store_stmt = m_config.delayed_retry;

        if (m_qc.large_query())
        {
            /** We're processing a large query that's split across multiple packets.
             * Route it to the same backend where we routed the previous packet. */
            ss_dassert(m_prev_target);
            target = m_prev_target;
            succp = true;
        }
        else if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
        {
            /**
             * There is a hint which either names the target backend or
             * hint which sets maximum allowed replication lag for the
             * backend.
             */
            if ((target = handle_hinted_target(querybuf, route_target)))
            {
                succp = true;
            }
        }
        else if (TARGET_IS_SLAVE(route_target))
        {
            if ((target = handle_slave_is_target(command, stmt_id)))
            {
                succp = true;

                if (m_config.retry_failed_reads &&
                    (command == MXS_COM_QUERY || command == MXS_COM_STMT_EXECUTE))
                {
                    // Only commands that can contain an SQL statement should be stored
                    store_stmt = true;
                }
            }
        }
        else if (TARGET_IS_MASTER(route_target))
        {
            succp = handle_master_is_target(&target);

            if (!m_config.strict_multi_stmt &&
                !m_config.strict_sp_calls &&
                m_target_node == m_current_master)
            {
                /** Reset the forced node as we're in relaxed multi-statement mode */
                m_target_node.reset();
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
                m_expected_responses++;
                m_query_queue = gwbuf_append(m_query_queue, gwbuf_clone(querybuf));
                MXS_INFO("Queuing query until '%s' completes session command", target->name());
            }
            else
            {
                // Target server was found and is in the correct state
                succp = handle_got_target(querybuf, target, store_stmt);

                if (succp && command == MXS_COM_STMT_EXECUTE && not_locked_to_master)
                {
                    /** Track the targets of the COM_STMT_EXECUTE statements. This
                     * information is used to route all COM_STMT_FETCH commands
                     * to the same server where the COM_STMT_EXECUTE was done. */
                    m_exec_map[stmt_id] = target;
                    MXS_INFO("COM_STMT_EXECUTE on %s: %s", target->name(), target->uri());
                }
            }
        }
        else if (can_retry_query() || m_is_replay_active)
        {
            retry_query(gwbuf_clone(querybuf));
            succp = true;

            MXS_INFO("Delaying routing: %s", extract_sql(querybuf).c_str());
        }
    }

    if (succp && m_router->config().connection_keepalive &&
        (TARGET_IS_SLAVE(route_target) || TARGET_IS_MASTER(route_target)))
    {
        handle_connection_keepalive(target);
    }

    return succp;
}

/**
 * Purge session command history
 *
 * @param sescmd Executed session command
 */
void RWSplitSession::purge_history(mxs::SSessionCommand& sescmd)
{
    /**
     * We can try to purge duplicate text protocol session commands. This
     * makes the history size smaller but at the cost of being able to handle
     * the more complex user variable modifications. To keep the best of both
     * worlds, keeping the first and last copy of each command should be
     * an adequate compromise. This way executing the following SQL will still
     * produce the correct result.
     *
     * USE test;
     * SET @myvar = (SELECT COUNT(*) FROM t1);
     * USE test;
     *
     * Another option would be to keep the first session command but that would
     * require more work to be done in the session command response processing.
     * This would be a better alternative but the gain might not be optimal.
     */

    // As the PS handles map to explicit IDs, we must retain all COM_STMT_PREPARE commands
    if (sescmd->get_command() != MXS_COM_STMT_PREPARE)
    {
        auto eq = [&](mxs::SSessionCommand& scmd)
        {
            return scmd->eq(*sescmd);
        };

        auto first = std::find_if(m_sescmd_list.begin(), m_sescmd_list.end(), eq);

        if (first != m_sescmd_list.end())
        {
            // We have at least one of these commands. See if we have a second one
            auto second = std::find_if(std::next(first), m_sescmd_list.end(), eq);

            if (second != m_sescmd_list.end())
            {
                // We have a total of three commands, remove the middle one
                auto old_cmd = *second;
                m_sescmd_responses.erase(old_cmd->get_position());
                m_sescmd_list.erase(second);
            }
        }
    }
}

void RWSplitSession::continue_large_session_write(GWBUF *querybuf, uint32_t type)
{
    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        SRWBackend& backend = *it;

        if (backend->in_use())
        {
            backend->continue_session_command(gwbuf_clone(querybuf));
        }
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
bool RWSplitSession::route_session_write(GWBUF *querybuf, uint8_t command, uint32_t type)
{
    /** The SessionCommand takes ownership of the buffer */
    uint64_t id = m_sescmd_count++;
    mxs::SSessionCommand sescmd(new mxs::SessionCommand(querybuf, id));
    bool expecting_response = mxs_mysql_command_will_respond(command);
    int nsucc = 0;
    uint64_t lowest_pos = id;
    gwbuf_set_type(querybuf, GWBUF_TYPE_COLLECT_RESULT);

    if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT) ||
        qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
    {
        m_qc.ps_store(querybuf, id);
    }
    else if (qc_query_is_type(type, QUERY_TYPE_DEALLOC_PREPARE))
    {
        m_qc.ps_erase(querybuf);
    }

    MXS_INFO("Session write, routing to all servers.");

    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        SRWBackend& backend = *it;

        if (backend->in_use())
        {
            backend->append_session_command(sescmd);

            uint64_t current_pos = backend->next_session_command()->get_position();

            if (current_pos < lowest_pos)
            {
                lowest_pos = current_pos;
            }

            if (backend->execute_session_command())
            {
                nsucc += 1;
                atomic_add_uint64(&backend->server()->stats.packets, 1);

                if (expecting_response)
                {
                    m_expected_responses++;
                }

                MXS_INFO("Route query to %s: %s \t%s",
                         backend->is_master() ? "master" : "slave",
                         backend->name(), backend->uri());
            }
            else
            {
                MXS_ERROR("Failed to execute session command in %s (%s)",
                          backend->name(),backend->uri());
            }
        }
    }

    if (m_config.max_sescmd_history > 0 && m_sescmd_list.size() >= m_config.max_sescmd_history)
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
                        m_router->service()->name, m_config.max_sescmd_history);
            warn_history_exceeded = false;
        }

        m_config.disable_sescmd_history = true;
        m_config.max_sescmd_history = 0;
        m_sescmd_list.clear();
    }

    if (m_config.disable_sescmd_history)
    {
        /** Prune stored responses */
        ResponseMap::iterator it = m_sescmd_responses.lower_bound(lowest_pos);

        if (it != m_sescmd_responses.end())
        {
            m_sescmd_responses.erase(m_sescmd_responses.begin(), it);
        }
    }
    else
    {
        purge_history(sescmd);
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

    return nsucc;
}

/**
 * Check if replication lag is below acceptable levels
 */
static inline bool rpl_lag_is_ok(SRWBackend& backend, int max_rlag)
{
    return max_rlag == MAX_RLAG_UNDEFINED ||
           (backend->server()->rlag != MAX_RLAG_NOT_AVAILABLE &&
            backend->server()->rlag <= max_rlag);
}

SRWBackend RWSplitSession::get_hinted_backend(char *name)
{
    SRWBackend rval;

    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        auto& backend = *it;

        /** The server must be a valid slave, relay server, or master */
        if ((backend->in_use() || (can_recover_servers() && backend->can_connect())) &&
            strcasecmp(name, backend->name()) == 0 &&
            (backend->is_slave() || backend->is_relay() || backend->is_master()))
        {
            rval = backend;
            break;
        }
    }

    return rval;
}

SRWBackend RWSplitSession::get_slave_backend(int max_rlag)
{
    SRWBackend rval;
    auto counts = get_slave_counts(m_backends, m_current_master);

    for (auto it = m_backends.begin(); it != m_backends.end(); it++)
    {
        auto& backend = *it;

        if ((backend->is_master() || backend->is_slave()) && // Either a master or a slave
            rpl_lag_is_ok(backend, max_rlag)) // Not lagging too much
        {
            if (backend->in_use() || (can_recover_servers() && backend->can_connect()))
            {
                if (!rval)
                {
                    // No previous candidate, accept any valid server (includes master)
                    if ((backend->is_master() && backend == m_current_master) ||
                        backend->is_slave())
                    {
                        rval = backend;
                    }
                }
                else if (backend->in_use() || counts.second < m_router->max_slave_count())
                {
                    if (!m_config.master_accept_reads && rval->is_master())
                    {
                        // Pick slaves over masters with master_accept_reads=false
                        rval = backend;
                    }
                    else
                    {
                        // Compare the two servers and pick the best one
                        rval = compare_backends(rval, backend, m_config.slave_selection_criteria);
                    }
                }
            }
        }
    }

    return rval;
}

SRWBackend RWSplitSession::get_master_backend()
{
    SRWBackend rval;
    /** get root master from available servers */
    SRWBackend master = get_root_master(m_backends);

    if (master)
    {
        if (master->in_use() || (m_config.master_reconnection && master->can_connect()))
        {
            if (master->is_master())
            {
                rval = master;
            }
            else
            {
                MXS_ERROR("Server '%s' does not have the master state and "
                          "can't be chosen as the master.", master->name());
            }
        }
        else
        {
            MXS_ERROR("Server '%s' is not in use and can't be chosen as the master.",
                      master->name());
        }
    }

    return rval;
}

/**
 * Provide the router with a reference to a suitable backend
 *
 * @param rses     Pointer to router client session
 * @param btype    Backend type
 * @param name     Name of the backend which is primarily searched. May be NULL.
 * @param max_rlag Maximum replication lag
 * @param target   The target backend
 *
 * @return True if a backend was found
 */
SRWBackend RWSplitSession::get_target_backend(backend_type_t btype,
                                              char *name, int max_rlag)
{
    /** Check whether using target_node as target SLAVE */
    if (m_target_node && session_trx_is_read_only(m_client->session))
    {
        MXS_DEBUG("In READ ONLY transaction, using server '%s'", m_target_node->name());
        return m_target_node;
    }

    SRWBackend rval;

    if (name) /*< Choose backend by name from a hint */
    {
        ss_dassert(btype != BE_MASTER);
        btype = BE_SLAVE;
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
    int conf_max_rlag;

    /** if there is no configured value, then longest possible int is used */
    if (m_config.max_slave_replication_lag > 0)
    {
        conf_max_rlag = m_config.max_slave_replication_lag;
    }
    else
    {
        conf_max_rlag = ~(1 << 31);
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
SRWBackend RWSplitSession::handle_hinted_target(GWBUF *querybuf, route_target_t route_target)
{
    char *named_server = NULL;
    int rlag_max = MAX_RLAG_UNDEFINED;

    HINT* hint = querybuf->hint;

    while (hint != NULL)
    {
        if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            /**
             * Set the name of searched
             * backend server.
             */
            named_server = (char*)hint->data;
            MXS_INFO("Hint: route to server '%s'", named_server);
        }
        else if (hint->type == HINT_PARAMETER &&
                 (strncasecmp((char *)hint->data, "max_slave_replication_lag",
                              strlen("max_slave_replication_lag")) == 0))
        {
            int val = (int)strtol((char *)hint->value, (char **)NULL, 10);

            if (val != 0 || errno == 0)
            {
                /** Set max. acceptable replication lag value for backend srv */
                rlag_max = val;
                MXS_INFO("Hint: max_slave_replication_lag=%d", rlag_max);
            }
        }
        hint = hint->next;
    } /*< while */

    if (rlag_max == MAX_RLAG_UNDEFINED) /*< no rlag max hint, use config */
    {
        rlag_max = get_max_replication_lag();
    }

    /** target may be master or slave */
    backend_type_t btype = route_target & TARGET_SLAVE ? BE_SLAVE : BE_MASTER;

    /**
     * Search backend server by name or replication lag.
     * If it fails, then try to find valid slave or master.
     */
    SRWBackend target = get_target_backend(btype, named_server, rlag_max);

    if (!target)
    {
        if (TARGET_IS_NAMED_SERVER(route_target))
        {
            MXS_INFO("Was supposed to route to named server "
                     "%s but couldn't find the server in a "
                     "suitable state.", named_server);
        }
        else if (TARGET_IS_RLAG_MAX(route_target))
        {
            MXS_INFO("Was supposed to route to server with "
                     "replication lag at most %d but couldn't "
                     "find such a slave.", rlag_max);
        }
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
SRWBackend RWSplitSession::handle_slave_is_target(uint8_t cmd, uint32_t stmt_id)
{
    int rlag_max = get_max_replication_lag();
    SRWBackend target;

    if (cmd == MXS_COM_STMT_FETCH)
    {
        /** The COM_STMT_FETCH must be executed on the same server as the
         * COM_STMT_EXECUTE was executed on */
        ExecMap::iterator it = m_exec_map.find(stmt_id);

        if (it != m_exec_map.end())
        {
            target = it->second;
            MXS_INFO("COM_STMT_FETCH on %s (%s)", target->name(), target->uri());
        }
        else
        {
            MXS_WARNING("Unknown statement ID %u used in COM_STMT_FETCH", stmt_id);
        }
    }

    if (!target)
    {
        target = get_target_backend(BE_SLAVE, NULL, rlag_max);
    }

    if (target)
    {
        atomic_add_uint64(&m_router->stats().n_slave, 1);
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
                                                SRWBackend& old_master,
                                                SRWBackend& curr_master)
{
    /** Both backends should either be empty, not connected or the DCB should
     * be a backend (the last check is slightly redundant). */
    ss_dassert(!old_master || !old_master->in_use() || old_master->dcb()->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    ss_dassert(!curr_master || !curr_master->in_use() ||
               curr_master->dcb()->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    char errmsg[MAX_SERVER_ADDRESS_LEN * 2 + 100]; // Extra space for error message

    if (!found)
    {
        sprintf(errmsg, "Could not find a valid master connection");
    }
    else if (old_master && curr_master && old_master->in_use())
    {
        /** We found a master but it's not the same connection */
        ss_dassert(old_master != curr_master);
        sprintf(errmsg, "Master server changed from '%s' to '%s'",
                old_master->name(), curr_master->name());
    }
    else if (old_master && old_master->in_use())
    {
        // TODO: Figure out if this is an impossible situation
        ss_dassert(!curr_master);
        /** We have an original master connection but we couldn't find it */
        sprintf(errmsg, "The connection to master server '%s' is not available",
                old_master->name());
    }
    else
    {
        /** We never had a master connection, the session must be in read-only mode */
        if (m_config.master_failure_mode != RW_FAIL_INSTANTLY)
        {
            sprintf(errmsg, "Session is in read-only mode because it was created "
                    "when no master was available");
        }
        else
        {
            ss_dassert(old_master && !old_master->in_use());
            sprintf(errmsg, "Was supposed to route to master but the master connection is %s",
                    old_master->is_closed() ? "closed" : "not in a suitable state");
            ss_dassert(old_master->is_closed());
        }
    }

    MXS_WARNING("[%s] Write query received from %s@%s. %s. Closing client connection.",
                m_router->service()->name, m_client->user,
                m_client->remote, errmsg);
}

bool RWSplitSession::should_replace_master(SRWBackend& target)
{
    return m_config.master_reconnection &&
           // We have a target server and it's not the current master
           target && target != m_current_master &&
           // We are not inside a transaction (also checks for autocommit=1)
           (!session_trx_is_active(m_client->session) || m_is_replay_active) &&
           // We are not locked to the old master
           !is_locked_to_master();
}

void RWSplitSession::replace_master(SRWBackend& target)
{
    m_current_master = target;

    m_qc.master_replaced();
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
bool RWSplitSession::handle_master_is_target(SRWBackend* dest)
{
    SRWBackend target = get_target_backend(BE_MASTER, NULL, MAX_RLAG_UNDEFINED);
    bool succp = true;

    if (should_replace_master(target))
    {
        MXS_INFO("Replacing old master '%s' with new master '%s'", m_current_master ?
                 m_current_master->name() : "<no previous master>", target->name());
        replace_master(target);
    }

    if (target && target == m_current_master)
    {
        atomic_add_uint64(&m_router->stats().n_master, 1);
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
            }
        }
        else if (!can_retry_query())
        {
            log_master_routing_failure(succp, m_current_master, target);
        }
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
GWBUF* RWSplitSession::add_prefix_wait_gtid(SERVER *server, GWBUF *origin)
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
    const char* wait_func = (server->server_type == SERVER_TYPE_MARIADB) ?
                            MARIADB_WAIT_GTID_FUNC : MYSQL_WAIT_GTID_FUNC;
    const char *gtid_wait_timeout = m_router->config().causal_reads_timeout.c_str();
    const char *gtid_position = m_gtid_pos.c_str();

    /* Create a new buffer to store prefix sql */
    size_t prefix_len = strlen(gtid_wait_stmt) + strlen(gtid_position) +
                        strlen(gtid_wait_timeout) + strlen(wait_func);

    // Only do the replacement if it fits into one packet
    if (gwbuf_length(origin) + prefix_len < GW_MYSQL_MAX_PACKET_LEN + MYSQL_HEADER_LEN)
    {
        char prefix_sql[prefix_len];
        snprintf(prefix_sql, prefix_len, gtid_wait_stmt, wait_func, gtid_position, gtid_wait_timeout);
        GWBUF *prefix_buff = modutil_create_query(prefix_sql);

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
bool RWSplitSession::handle_got_target(GWBUF* querybuf, SRWBackend& target, bool store)
{
    ss_dassert(target->in_use());
    /**
     * If the transaction is READ ONLY set forced_node to this backend.
     * This SLAVE backend will be used until the COMMIT is seen.
     */
    if (!m_target_node && session_trx_is_read_only(m_client->session))
    {
        m_target_node = target;
        MXS_DEBUG("Setting forced_node SLAVE to %s within an opened READ ONLY transaction",
                  target->name());
    }

    MXS_INFO("Route query to %s: %s \t%s <", target->is_master() ? "master" : "slave",
             target->name(), target->uri());

    /** The session command cursor must not be active */
    ss_dassert(!target->has_session_commands());

    mxs::Backend::response_type response = mxs::Backend::NO_RESPONSE;
    uint8_t cmd = mxs_mysql_get_command(querybuf);
    GWBUF *send_buf = gwbuf_clone(querybuf);

    if (cmd == COM_QUERY && m_router->config().causal_reads && !m_gtid_pos .empty())
    {
        send_buf = add_prefix_wait_gtid(target->server(), send_buf);
        m_wait_gtid = WAITING_FOR_HEADER;
    }

    if (m_qc.load_data_state() != QueryClassifier::LOAD_DATA_ACTIVE &&
        !m_qc.large_query() && mxs_mysql_command_will_respond(cmd))
    {
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    bool large_query = is_large_query(querybuf);

    /**
     * If we are starting a new query, we use RWBackend::write, otherwise we use
     * RWBackend::continue_write to continue an ongoing query. RWBackend::write
     * will do the replacement of PS IDs which must not be done if we are
     * continuing an ongoing query.
     */
    bool success = !m_qc.large_query() ?
                   target->write(send_buf, response) :
                   target->continue_write(send_buf);

    if (success)
    {
        if (store)
        {
            m_current_query.copy_from(querybuf);
        }

        atomic_add_uint64(&m_router->stats().n_queries, 1);
        atomic_add_uint64(&target->server()->stats.packets, 1);

        if (!m_qc.large_query() && response == mxs::Backend::EXPECT_RESPONSE)
        {
            /** The server will reply to this command */
            ss_dassert(target->get_reply_state() == REPLY_STATE_DONE);
            target->set_reply_state(REPLY_STATE_START);
            m_expected_responses++;

            if (m_qc.load_data_state() == QueryClassifier::LOAD_DATA_END)
            {
                /** The final packet in a LOAD DATA LOCAL INFILE is an empty packet
                 * to which the server responds with an OK or an ERR packet */
                ss_dassert(gwbuf_length(querybuf) == 4);
                m_qc.set_load_data_state(QueryClassifier::LOAD_DATA_INACTIVE);
            }
        }

        m_qc.set_large_query(large_query);

        if (large_query)
        {
            /** Store the previous target as we're processing a multi-packet query */
            m_prev_target = target;
        }
        else
        {
            /** Otherwise reset it so we know the query is complete */
            m_prev_target.reset();
        }

        /**
         * If a READ ONLY transaction is ending set forced_node to NULL
         */
        if (m_target_node &&
            session_trx_is_read_only(m_client->session) &&
            session_trx_is_ending(m_client->session))
        {
            MXS_DEBUG("An opened READ ONLY transaction ends: forced_node is set to NULL");
            m_target_node.reset();
        }
        return true;
    }
    else
    {
        MXS_ERROR("Routing query failed.");
        return false;
    }
}
