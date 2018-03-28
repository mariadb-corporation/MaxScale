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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <maxscale/alloc.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/modutil.h>
#include <maxscale/router.h>
#include <maxscale/server.h>

#include "routeinfo.hh"
#include "rwsplit_internal.hh"

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

void handle_connection_keepalive(RWSplit *inst, RWSplitSession *rses,
                                 SRWBackend& target)
{
    ss_dassert(target);
    ss_debug(int nserv = 0);
    /** Each heartbeat is 1/10th of a second */
    int keepalive = inst->config().connection_keepalive * 10;

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend backend = *it;

        if (backend->in_use() && backend != target && !backend->is_waiting_result())
        {
            ss_debug(nserv++);
            int diff = hkheartbeat - backend->dcb()->last_read;

            if (diff > keepalive)
            {
                MXS_INFO("Pinging %s, idle for %ld seconds",
                         backend->name(), HB_TO_SEC(diff));
                modutil_ignorable_ping(backend->dcb());
            }
        }
    }

    ss_dassert(nserv < rses->rses_nbackends);
}

static inline bool locked_to_master(RWSplitSession *rses)
{
    return rses->large_query || (rses->current_master && rses->target_node == rses->current_master);
}

/**
 * Routing function. Find out query type, backend type, and target DCB(s).
 * Then route query to found target(s).
 * @param inst      router instance
 * @param rses      router session
 * @param querybuf  GWBUF including the query
 *
 * @return true if routing succeed or if it failed due to unsupported query.
 * false if backend failure was encountered.
 */
bool route_single_stmt(RWSplit *inst, RWSplitSession *rses, GWBUF *querybuf, const RouteInfo& info)
{
    bool succp = false;
    uint32_t stmt_id = info.stmt_id;
    uint8_t command = info.command;
    uint32_t qtype = info.type;
    route_target_t route_target = info.target;
    bool not_locked_to_master = !locked_to_master(rses);

    if (not_locked_to_master && is_ps_command(command))
    {
        /** Replace the client statement ID with our internal one only if the
         * target node is not the current master */
        replace_binary_ps_id(querybuf, stmt_id);
    }

    SRWBackend target;

    if (TARGET_IS_ALL(route_target))
    {
        // TODO: Handle payloads larger than (2^24 - 1) bytes that are routed to all servers
        succp = handle_target_is_all(route_target, inst, rses, querybuf, command, qtype);
    }
    else
    {
        bool store_stmt = false;

        if (rses->large_query)
        {
            /** We're processing a large query that's split across multiple packets.
             * Route it to the same backend where we routed the previous packet. */
            ss_dassert(rses->prev_target);
            target = rses->prev_target;
            succp = true;
        }
        else if (TARGET_IS_NAMED_SERVER(route_target) || TARGET_IS_RLAG_MAX(route_target))
        {
            /**
             * There is a hint which either names the target backend or
             * hint which sets maximum allowed replication lag for the
             * backend.
             */
            if ((target = handle_hinted_target(rses, querybuf, route_target)))
            {
                succp = true;
            }
        }
        else if (TARGET_IS_SLAVE(route_target))
        {
            if ((target = handle_slave_is_target(inst, rses, command, stmt_id)))
            {
                succp = true;
                store_stmt = rses->rses_config.retry_failed_reads;
            }
        }
        else if (TARGET_IS_MASTER(route_target))
        {
            succp = handle_master_is_target(inst, rses, &target);

            if (!rses->rses_config.strict_multi_stmt &&
                !rses->rses_config.strict_sp_calls &&
                rses->target_node == rses->current_master)
            {
                /** Reset the forced node as we're in relaxed multi-statement mode */
                rses->target_node.reset();
            }
        }

        if (target && succp) /*< Have DCB of the target backend */
        {
            ss_dassert(!store_stmt || TARGET_IS_SLAVE(route_target));
            succp = handle_got_target(inst, rses, querybuf, target, store_stmt);

            if (succp && command == MXS_COM_STMT_EXECUTE && not_locked_to_master)
            {
                /** Track the targets of the COM_STMT_EXECUTE statements. This
                 * information is used to route all COM_STMT_FETCH commands
                 * to the same server where the COM_STMT_EXECUTE was done. */
                ss_dassert(stmt_id > 0);
                rses->exec_map[stmt_id] = target;
                MXS_INFO("COM_STMT_EXECUTE on %s", target->uri());
            }
        }
    }

    if (succp && inst->config().connection_keepalive &&
        (TARGET_IS_SLAVE(route_target) || TARGET_IS_MASTER(route_target)))
    {
        handle_connection_keepalive(inst, rses, target);
    }

    return succp;
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
 * @param router_cli_ses    Client's router session pointer
 * @param querybuf      GWBUF including the query to be routed
 * @param inst          Router instance
 * @param packet_type       Type of MySQL packet
 * @param qtype         Query type from query_classifier
 *
 * @return True if at least one backend is used and routing succeed to all
 * backends being used, otherwise false.
 *
 */
bool route_session_write(RWSplitSession *rses, GWBUF *querybuf,
                         uint8_t command, uint32_t type)
{
    /** The SessionCommand takes ownership of the buffer */
    uint64_t id = rses->sescmd_count++;
    mxs::SSessionCommand sescmd(new mxs::SessionCommand(querybuf, id));
    bool expecting_response = mxs_mysql_command_will_respond(command);
    int nsucc = 0;
    uint64_t lowest_pos = id;

    if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT) ||
        qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
    {
        gwbuf_set_type(querybuf, GWBUF_TYPE_COLLECT_RESULT);
        rses->ps_manager.store(querybuf, id);
    }

    MXS_INFO("Session write, routing to all servers.");

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
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

                if (expecting_response)
                {
                    rses->expected_responses++;
                }

                MXS_INFO("Route query to %s \t%s",
                         backend->is_master() ? "master" : "slave",
                         backend->uri());
            }
            else
            {
                MXS_ERROR("Failed to execute session command in %s", backend->uri());
            }
        }
    }

    if (rses->rses_config.max_sescmd_history > 0 &&
        rses->sescmd_count >= rses->rses_config.max_sescmd_history)
    {
        MXS_WARNING("Router session exceeded session command history limit. "
                    "Slave recovery is disabled and only slave servers with "
                    "consistent session state are used "
                    "for the duration of the session.");
        rses->rses_config.disable_sescmd_history = true;
        rses->rses_config.max_sescmd_history = 0;
        rses->sescmd_list.clear();
    }

    if (rses->rses_config.disable_sescmd_history)
    {
        /** Prune stored responses */
        ResponseMap::iterator it = rses->sescmd_responses.lower_bound(lowest_pos);

        if (it != rses->sescmd_responses.end())
        {
            rses->sescmd_responses.erase(rses->sescmd_responses.begin(), it);
        }
    }
    else
    {
        rses->sescmd_list.push_back(sescmd);
    }

    if (nsucc)
    {
        rses->sent_sescmd = id;

        if (!expecting_response)
        {
            /** The command doesn't generate a response so we increment the
             * completed session command count */
            rses->recv_sescmd++;
        }
    }

    return nsucc;
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
SRWBackend get_target_backend(RWSplitSession *rses, backend_type_t btype,
                              char *name, int max_rlag)
{
    CHK_CLIENT_RSES(rses);

    /** Check whether using rses->target_node as target SLAVE */
    if (rses->target_node && session_trx_is_read_only(rses->client_dcb->session))
    {
        MXS_DEBUG("In READ ONLY transaction, using server '%s'",
                  rses->target_node->name());
        return rses->target_node;
    }

    if (name) /*< Choose backend by name from a hint */
    {
        ss_dassert(btype != BE_MASTER); /*< Master dominates and no name should be passed with it */

        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            SRWBackend& backend = *it;

            /** The server must be a valid slave, relay server, or master */

            if (backend->in_use() &&
                (strcasecmp(name, backend->name()) == 0) &&
                (backend->is_slave() ||
                 backend->is_relay() ||
                 backend->is_master()))
            {
                return backend;
            }
        }

        /** No server found, use a normal slave for it */
        btype = BE_SLAVE;
    }

    SRWBackend rval;

    if (btype == BE_SLAVE)
    {
        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            SRWBackend& backend = *it;

            /**
             * Unused backend or backend which is not master nor
             * slave can't be used
             */
            if (!backend->in_use() ||
                (!backend->is_master() && !backend->is_slave()))
            {
                continue;
            }
            /**
             * If there are no candidates yet accept both master or
             * slave.
             */
            else if (!rval)
            {
                /**
                 * Ensure that master has not changed during
                 * session and abort if it has.
                 */
                if (backend->is_master() && backend == rses->current_master)
                {
                    /** found master */
                    rval = backend;
                }
                /**
                 * Ensure that max replication lag is not set
                 * or that candidate's lag doesn't exceed the
                 * maximum allowed replication lag.
                 */
                else if (max_rlag == MAX_RLAG_UNDEFINED ||
                         (backend->server()->rlag != MAX_RLAG_NOT_AVAILABLE &&
                          backend->server()->rlag <= max_rlag))
                {
                    /** found slave */
                    rval = backend;
                }
            }
            /**
             * If candidate is master, any slave which doesn't break
             * replication lag limits replaces it.
             */
            else if (rval->is_master() && backend->is_slave() &&
                     (max_rlag == MAX_RLAG_UNDEFINED ||
                      (backend->server()->rlag != MAX_RLAG_NOT_AVAILABLE &&
                       backend->server()->rlag <= max_rlag)) &&
                     !rses->rses_config.master_accept_reads)
            {
                /** found slave */
                rval = backend;
            }
            /**
             * When candidate exists, compare it against the current
             * backend and update assign it to new candidate if
             * necessary.
             */
            else if (backend->is_slave() ||
                     (rses->rses_config.master_accept_reads &&
                      backend->is_master()))
            {
                if (max_rlag == MAX_RLAG_UNDEFINED ||
                    (backend->server()->rlag != MAX_RLAG_NOT_AVAILABLE &&
                     backend->server()->rlag <= max_rlag))
                {
                    rval = compare_backends(rval, backend, rses->rses_config.slave_selection_criteria);
                }
                else
                {
                    MXS_INFO("Server %s is too much behind the master "
                             "(%d seconds) and can't be chosen",
                             backend->uri(), backend->server()->rlag);
                }
            }
        } /*<  for */
    }
    /**
     * If target was originally master only then the execution jumps
     * directly here.
     */
    else if (btype == BE_MASTER)
    {
        /** get root master from available servers */
        SRWBackend master = get_root_master(rses->backends);

        if (master)
        {
            if (master->in_use())
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
        else
        {
            MXS_ERROR("No master server available at this time.");
        }
    }

    return rval;
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
SRWBackend handle_hinted_target(RWSplitSession *rses, GWBUF *querybuf,
                                route_target_t route_target)
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
        rlag_max = rses_get_max_replication_lag(rses);
    }

    /** target may be master or slave */
    backend_type_t btype = route_target & TARGET_SLAVE ? BE_SLAVE : BE_MASTER;

    /**
     * Search backend server by name or replication lag.
     * If it fails, then try to find valid slave or master.
     */
    SRWBackend target = get_target_backend(rses, btype, named_server, rlag_max);

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
 * @brief Handle slave is the target
 *
 * One of the possible types of handling required when a request is routed
 *
 *  @param inst         Router instance
 *  @param ses          Router session
 *  @param target_dcb   DCB for the target server
 *
 *  @return bool - true if succeeded, false otherwise
 */
SRWBackend handle_slave_is_target(RWSplit *inst, RWSplitSession *rses,
                                  uint8_t cmd, uint32_t stmt_id)
{
    int rlag_max = rses_get_max_replication_lag(rses);
    SRWBackend target;

    if (cmd == MXS_COM_STMT_FETCH)
    {
        /** The COM_STMT_FETCH must be executed on the same server as the
         * COM_STMT_EXECUTE was executed on */
        ExecMap::iterator it = rses->exec_map.find(stmt_id);

        if (it != rses->exec_map.end())
        {
            target = it->second;
            MXS_INFO("COM_STMT_FETCH on %s", target->uri());
        }
        else
        {
            MXS_WARNING("Unknown statement ID %u used in COM_STMT_FETCH", stmt_id);
        }
    }

    if (!target)
    {
        target = get_target_backend(rses, BE_SLAVE, NULL, rlag_max);
    }

    if (target)
    {
        atomic_add_uint64(&inst->stats().n_slave, 1);
    }
    else
    {
        MXS_INFO("Was supposed to route to slave but finding suitable one failed.");
    }

    return target;
}

/**
 * @brief Log master write failure
 *
 * @param rses Router session
 */
static void log_master_routing_failure(RWSplitSession *rses, bool found,
                                       SRWBackend& old_master, SRWBackend& curr_master)
{
    ss_dassert(!old_master || !old_master->in_use() || old_master->dcb()->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    ss_dassert(!curr_master || curr_master->dcb()->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    char errmsg[MAX_SERVER_ADDRESS_LEN * 2 + 100]; // Extra space for error message

    if (!found)
    {
        sprintf(errmsg, "Could not find a valid master connection");
    }
    else if (old_master && curr_master && old_master->in_use())
    {
        /** We found a master but it's not the same connection */
        ss_dassert(old_master != curr_master);
        ss_dassert(old_master->dcb()->server && curr_master->dcb()->server);

        if (old_master != curr_master)
        {
            sprintf(errmsg, "Master server changed from '%s' to '%s'",
                    old_master->name(),
                    curr_master->name());
        }
        else
        {
            ss_dassert(false); // Currently we don't reconnect to the master
            sprintf(errmsg, "Connection to master '%s' was recreated",
                    curr_master->name());
        }
    }
    else if (old_master && old_master->in_use())
    {
        /** We have an original master connection but we couldn't find it */
        sprintf(errmsg, "The connection to master server '%s' is not available",
                old_master->name());
    }
    else
    {
        /** We never had a master connection, the session must be in read-only mode */
        if (rses->rses_config.master_failure_mode != RW_FAIL_INSTANTLY)
        {
            sprintf(errmsg, "Session is in read-only mode because it was created "
                    "when no master was available");
        }
        else if (old_master && !old_master->in_use())
        {
            sprintf(errmsg, "Was supposed to route to master but the master connection is %s",
                    old_master->is_closed() ? "closed" : "not in a suitable state");
            ss_dassert(old_master->is_closed());
        }
        else
        {
            sprintf(errmsg, "Was supposed to route to master but couldn't "
                    "find original master connection");
            ss_dassert(!true);
        }
    }

    MXS_WARNING("[%s] Write query received from %s@%s. %s. Closing client connection.",
                rses->router->service()->name, rses->client_dcb->user,
                rses->client_dcb->remote, errmsg);
}

bool should_replace_master(RWSplitSession *rses, SRWBackend& target)
{
    return rses->rses_config.allow_master_change &&
        // We have a target server and it's not the current master
        target && target != rses->current_master &&
        // We are not inside a transaction (also checks for autocommit=1)
        !session_trx_is_active(rses->client_dcb->session) &&
        // We are not locked to the old master
        !locked_to_master(rses);
}

void replace_master(RWSplitSession *rses, SRWBackend& target)
{
    rses->current_master = target;

    // As the master has changed, we can reset the temporary table information
    rses->have_tmp_tables = false;
    rses->temp_tables.clear();
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
bool handle_master_is_target(RWSplit *inst, RWSplitSession *rses,
                             SRWBackend* dest)
{
    SRWBackend target = get_target_backend(rses, BE_MASTER, NULL, MAX_RLAG_UNDEFINED);
    bool succp = true;

    if (should_replace_master(rses, target))
    {
        MXS_INFO("Replacing old master '%s' with new master '%s'", rses->current_master ?
                 rses->current_master->name() : "<no previous master>", target->name());
        replace_master(rses, target);
    }

    if (target && target == rses->current_master)
    {
        atomic_add_uint64(&inst->stats().n_master, 1);
    }
    else
    {
        /** The original master is not available, we can't route the write */
        if (rses->rses_config.master_failure_mode == RW_ERROR_ON_WRITE)
        {
            succp = send_readonly_error(rses->client_dcb);

            if (rses->current_master && rses->current_master->in_use())
            {
                rses->current_master->close();
            }
        }
        else
        {
            log_master_routing_failure(rses, succp, rses->current_master, target);
            succp = false;
        }
    }

    *dest = target;
    return succp;
}

static inline bool query_creates_reply(uint8_t cmd)
{
    return cmd != MXS_COM_QUIT &&
           cmd != MXS_COM_STMT_SEND_LONG_DATA &&
           cmd != MXS_COM_STMT_CLOSE &&
           cmd != MXS_COM_STMT_FETCH; // Fetch is done mid-result
}

static inline bool is_large_query(GWBUF* buf)
{
    uint32_t buflen = gwbuf_length(buf);

    // The buffer should contain at most (2^24 - 1) + 4 bytes ...
    ss_dassert(buflen <= MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN);
    // ... and the payload should be buflen - 4 bytes
    ss_dassert(MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buf)) == buflen - MYSQL_HEADER_LEN);

    return buflen == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;
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
GWBUF *add_prefix_wait_gtid(RWSplit *inst, RWSplitSession *rses, SERVER *server, GWBUF *origin)
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

    GWBUF *rval;
    const char* wait_func = (server->server_type == SERVER_TYPE_MARIADB) ?
            MARIADB_WAIT_GTID_FUNC : MYSQL_WAIT_GTID_FUNC;
    const char *gtid_wait_timeout = inst->config().causal_read_timeout.c_str();
    const char *gtid_pos = rses->gtid_pos.c_str();

    /* Create a new buffer to store prefix sql */
    size_t prefix_len = strlen(gtid_wait_stmt) + strlen(gtid_pos) +
        strlen(gtid_wait_timeout) + strlen(wait_func);
    char prefix_sql[prefix_len];
    snprintf(prefix_sql, prefix_len, gtid_wait_stmt, wait_func, gtid_pos, gtid_wait_timeout);
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

    return rval;
}

/**
 * @brief Handle writing to a target server
 *
 *  @return True on success
 */
bool handle_got_target(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf, SRWBackend& target, bool store)
{
    /**
     * If the transaction is READ ONLY set forced_node to this backend.
     * This SLAVE backend will be used until the COMMIT is seen.
     */
    if (!rses->target_node &&
        session_trx_is_read_only(rses->client_dcb->session))
    {
        rses->target_node = target;
        MXS_DEBUG("Setting forced_node SLAVE to %s within an opened READ ONLY transaction",
                  target->name());
    }

    MXS_INFO("Route query to %s \t%s <", target->is_master() ? "master" : "slave",
             target->uri());

    /** The session command cursor must not be active */
    ss_dassert(target->session_command_count() == 0);

    mxs::Backend::response_type response = mxs::Backend::NO_RESPONSE;
    rses->wait_gtid_state = EXPECTING_NOTHING;
    uint8_t cmd = mxs_mysql_get_command(querybuf);
    GWBUF *send_buf = gwbuf_clone(querybuf); ;
    if (cmd == COM_QUERY && inst->config().enable_causal_read && rses->gtid_pos != "")
    {
        send_buf = add_prefix_wait_gtid(inst, rses, target->server(), send_buf);
        rses->wait_gtid_state = EXPECTING_WAIT_GTID_RESULT;
    }

    if (rses->load_data_state != LOAD_DATA_ACTIVE &&
        query_creates_reply(cmd))
    {
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    bool large_query = is_large_query(querybuf);

    if (target->write(send_buf, response))
    {
        if (store && !session_store_stmt(rses->client_dcb->session, querybuf, target->server()))
        {
            MXS_ERROR("Failed to store current statement, it won't be retried if it fails.");
        }

        atomic_add_uint64(&inst->stats().n_queries, 1);

        if (!rses->large_query && response == mxs::Backend::EXPECT_RESPONSE)
        {
            /** The server will reply to this command */
            ss_dassert(target->get_reply_state() == REPLY_STATE_DONE);

            LOG_RS(target, REPLY_STATE_START);
            target->set_reply_state(REPLY_STATE_START);
            rses->expected_responses++;

            if (rses->load_data_state == LOAD_DATA_START)
            {
                /** The first packet contains the actual query and the server
                 * will respond to it */
                rses->load_data_state = LOAD_DATA_ACTIVE;
            }
            else if (rses->load_data_state == LOAD_DATA_END)
            {
                /** The final packet in a LOAD DATA LOCAL INFILE is an empty packet
                 * to which the server responds with an OK or an ERR packet */
                ss_dassert(gwbuf_length(querybuf) == 4);
                rses->load_data_state = LOAD_DATA_INACTIVE;
            }
        }

        if ((rses->large_query = large_query))
        {
            /** Store the previous target as we're processing a multi-packet query */
            rses->prev_target = target;
        }
        else
        {
            /** Otherwise reset it so we know the query is complete */
            rses->prev_target.reset();
        }

        /**
         * If a READ ONLY transaction is ending set forced_node to NULL
         */
        if (rses->target_node &&
            session_trx_is_read_only(rses->client_dcb->session) &&
            session_trx_is_ending(rses->client_dcb->session))
        {
            MXS_DEBUG("An opened READ ONLY transaction ends: forced_node is set to NULL");
            rses->target_node.reset();
        }
        return true;
    }
    else
    {
        MXS_ERROR("Routing query failed.");
        return false;
    }
}
