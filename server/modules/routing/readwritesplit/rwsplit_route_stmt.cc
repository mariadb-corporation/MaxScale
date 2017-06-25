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
#include "rwsplit_internal.hh"

#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/alloc.h>
#include <maxscale/router.h>
#include <maxscale/modutil.h>

/**
 * The functions that support the routing of queries to back end
 * servers. All the functions in this module are internal to the read
 * write split router, and not intended to be called from anywhere else.
 */

extern int (*criteria_cmpfun[LAST_CRITERIA])(const SRWBackend&, const SRWBackend&);

static SRWBackend get_root_master_backend(RWSplitSession *rses);

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

    return p(a, b) < 0 ? a : b;
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
                MXS_INFO("Pinging %s, idle for %d seconds",
                         backend->name(), diff / 10);
                modutil_ignorable_ping(backend->dcb());
            }
        }
    }

    ss_dassert(nserv < rses->rses_nbackends);
}

uint32_t get_stmt_id(RWSplitSession* rses, GWBUF* buffer)
{
    uint32_t rval = 0;

    // All COM_STMT type statements store the ID in the same place
    uint32_t id = mxs_mysql_extract_ps_id(buffer);
    ClientHandleMap::iterator it = rses->ps_handles.find(id);

    if (it != rses->ps_handles.end())
    {
        rval = it->second;
    }

    return rval;
}

void replace_stmt_id(GWBUF* buffer, uint32_t id)
{
    uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
    gw_mysql_set_byte4(ptr, id);
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
bool route_single_stmt(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf)
{
    route_target_t route_target;
    bool succp = false;
    bool non_empty_packet;
    uint32_t stmt_id = 0;

    ss_dassert(querybuf->next == NULL); // The buffer must be contiguous.

    /* packet_type is a problem as it is MySQL specific */
    uint8_t command = determine_packet_type(querybuf, &non_empty_packet);
    uint32_t qtype = determine_query_type(querybuf, command, non_empty_packet);

    if (non_empty_packet)
    {
        handle_multi_temp_and_load(rses, querybuf, command, &qtype);

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_transaction_status(rses, querybuf, qtype);
        }
        /**
         * Find out where to route the query. Result may not be clear; it is
         * possible to have a hint for routing to a named server which can
         * be either slave or master.
         * If query would otherwise be routed to slave then the hint determines
         * actual target server if it exists.
         *
         * route_target is a bitfield and may include :
         * TARGET_ALL
         * - route to all connected backend servers
         * TARGET_SLAVE[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to hints, then to slave and if those
         *   failed, eventually to master
         * TARGET_MASTER[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to the hints and if they failed,
         *   eventually to master
         */

        if (command == MYSQL_COM_QUERY &&
            qc_get_operation(querybuf) == QUERY_OP_EXECUTE)
        {
            std::string id = extract_text_ps_id(querybuf);
            qtype = rses->ps_manager.get_type(id);
        }
        else if (is_ps_command(command))
        {
            stmt_id = get_stmt_id(rses, querybuf);
            qtype = rses->ps_manager.get_type(stmt_id);
            replace_stmt_id(querybuf, stmt_id);
        }

        route_target = get_route_target(rses, command, qtype, querybuf->hint);
    }
    else
    {
        /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
        route_target = TARGET_MASTER;
        rses->load_data_state = LOAD_DATA_END;
        MXS_INFO("> LOAD DATA LOCAL INFILE finished: %lu bytes sent.",
                 rses->rses_load_data_sent + gwbuf_length(querybuf));
    }

    SRWBackend target;

    if (TARGET_IS_ALL(route_target))
    {
        succp = handle_target_is_all(route_target, inst, rses, querybuf, command, qtype);
    }
    else
    {
        bool store_stmt = false;
        /**
         * There is a hint which either names the target backend or
         * hint which sets maximum allowed replication lag for the
         * backend.
         */
        if (TARGET_IS_NAMED_SERVER(route_target) ||
            TARGET_IS_RLAG_MAX(route_target))
        {
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

            if (succp && command == MYSQL_COM_STMT_EXECUTE)
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

    /** get root master from available servers */
    SRWBackend master = get_root_master_backend(rses);

    if (name) /*< Choose backend by name from a hint */
    {
        ss_dassert(btype != BE_MASTER); /*< Master dominates and no name should be passed with it */

        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            SRWBackend& backend = *it;

            /** The server must be a valid slave, relay server, or master */

            if (backend->in_use() && backend->is_active() &&
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
            if (!backend->in_use() || !backend->is_active() ||
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
        if (master && master->is_active())
        {
            /** It is possible for the server status to change at any point in time
             * so copying it locally will make possible error messages
             * easier to understand */
            SERVER server;
            server.status = master->server()->status;

            if (master->in_use())
            {
                if (SERVER_IS_MASTER(&server))
                {
                    rval = master;
                }
                else
                {
                    MXS_ERROR("Server '%s' should be master but is %s instead "
                              "and can't be chosen as the master.",
                              master->name(),
                              STRSRVSTATUS(&server));
                }
            }
            else
            {
                MXS_ERROR("Server '%s' is not in use and can't be chosen as the master.",
                          master->name());
            }
        }
    }

    return rval;
}

/**
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 *
 *  @param qtype      Type of query
 *  @param trx_active Is transacation active or not
 *  @param hint       Pointer to list of hints attached to the query buffer
 *
 *  @return bitfield including the routing target, or the target server name
 *          if the query would otherwise be routed to slave.
 */
route_target_t get_route_target(RWSplitSession *rses, uint8_t command,
                                uint32_t qtype, HINT *hint)
{
    bool trx_active = session_trx_is_active(rses->client_dcb->session);
    bool load_active = rses->load_data_state != LOAD_DATA_INACTIVE;
    mxs_target_t use_sql_variables_in = rses->rses_config.use_sql_variables_in;
    int target = TARGET_UNDEFINED;

    if (rses->target_node && rses->target_node == rses->current_master)
    {
        target = TARGET_MASTER;
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) ||
             qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT) ||
             command == MYSQL_COM_STMT_CLOSE ||
             command == MYSQL_COM_STMT_RESET)
    {
        target = TARGET_ALL;
    }
    /**
     * These queries are not affected by hints
     */
    else if (!load_active &&
             (qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE) ||
              /** Configured to allow writing user variables to all nodes */
              (use_sql_variables_in == TYPE_ALL &&
               qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE)) ||
              qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE) ||
              /** enable or disable autocommit are always routed to all */
              qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) ||
              qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT)))
    {
        /**
         * This is problematic query because it would be routed to all
         * backends but since this is SELECT that is not possible:
         * 1. response set is not handled correctly in clientReply and
         * 2. multiple results can degrade performance.
         *
         * Prepared statements are an exception to this since they do not
         * actually do anything but only prepare the statement to be used.
         * They can be safely routed to all backends since the execution
         * is done later.
         *
         * With prepared statement caching the task of routing
         * the execution of the prepared statements to the right server would be
         * an easy one. Currently this is not supported.
         */
        if (qc_query_is_type(qtype, QUERY_TYPE_READ))
        {
            MXS_WARNING("The query can't be routed to all "
                        "backend servers because it includes SELECT and "
                        "SQL variable modifications which is not supported. "
                        "Set use_sql_variables_in=master or split the "
                        "query to two, where SQL variable modifications "
                        "are done in the first and the SELECT in the "
                        "second one.");

            target = TARGET_MASTER;
        }
        target |= TARGET_ALL;
    }
    /**
     * Hints may affect on routing of the following queries
     */
    else if (!trx_active && !load_active &&
             !qc_query_is_type(qtype, QUERY_TYPE_MASTER_READ) &&
             !qc_query_is_type(qtype, QUERY_TYPE_WRITE) &&
             (qc_query_is_type(qtype, QUERY_TYPE_READ) ||
              qc_query_is_type(qtype, QUERY_TYPE_SHOW_TABLES) ||
              qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
              qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
              qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ)))
    {
        if (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ))
        {
            if (use_sql_variables_in == TYPE_ALL)
            {
                target = TARGET_SLAVE;
            }
        }
        else if (qc_query_is_type(qtype, QUERY_TYPE_READ) || // Normal read
                 qc_query_is_type(qtype, QUERY_TYPE_SHOW_TABLES) || // SHOW TABLES
                 qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) || // System variable
                 qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ)) // Global system variable
        {
            target = TARGET_SLAVE;
        }

        /** If nothing matches then choose the master */
        if ((target & (TARGET_ALL | TARGET_SLAVE | TARGET_MASTER)) == 0)
        {
            target = TARGET_MASTER;
        }
    }
    else if (session_trx_is_read_only(rses->client_dcb->session))
    {
        /* Force TARGET_SLAVE for READ ONLY tranaction (active or ending) */
        target = TARGET_SLAVE;
    }
    else
    {
        ss_dassert(trx_active || load_active ||
                   (qc_query_is_type(qtype, QUERY_TYPE_WRITE) ||
                    qc_query_is_type(qtype, QUERY_TYPE_MASTER_READ) ||
                    qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE) ||
                    (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) &&
                     use_sql_variables_in == TYPE_MASTER) ||
                    (qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) &&
                     use_sql_variables_in == TYPE_MASTER) ||
                    (qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ) &&
                     use_sql_variables_in == TYPE_MASTER) ||
                    (qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE) &&
                     use_sql_variables_in == TYPE_MASTER) ||
                    (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE) &&
                     use_sql_variables_in == TYPE_MASTER) ||
                    qc_query_is_type(qtype, QUERY_TYPE_BEGIN_TRX) ||
                    qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) ||
                    qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT) ||
                    qc_query_is_type(qtype, QUERY_TYPE_ROLLBACK) ||
                    qc_query_is_type(qtype, QUERY_TYPE_COMMIT) ||
                    qc_query_is_type(qtype, QUERY_TYPE_EXEC_STMT) ||
                    qc_query_is_type(qtype, QUERY_TYPE_CREATE_TMP_TABLE) ||
                    qc_query_is_type(qtype, QUERY_TYPE_READ_TMP_TABLE) ||
                    qc_query_is_type(qtype, QUERY_TYPE_UNKNOWN)) ||
                   qc_query_is_type(qtype, QUERY_TYPE_EXEC_STMT));

        target = TARGET_MASTER;
    }

    /** process routing hints */
    while (hint != NULL)
    {
        if (hint->type == HINT_ROUTE_TO_MASTER)
        {
            target = TARGET_MASTER; /*< override */
            MXS_DEBUG("%lu [get_route_target] Hint: route to master.",
                      pthread_self());
            break;
        }
        else if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            /**
             * Searching for a named server. If it can't be
             * found, the oroginal target is chosen.
             */
            target |= TARGET_NAMED_SERVER;
            MXS_DEBUG("%lu [get_route_target] Hint: route to "
                      "named server : ",
                      pthread_self());
        }
        else if (hint->type == HINT_ROUTE_TO_UPTODATE_SERVER)
        {
            /** not implemented */
        }
        else if (hint->type == HINT_ROUTE_TO_ALL)
        {
            /** not implemented */
        }
        else if (hint->type == HINT_PARAMETER)
        {
            if (strncasecmp((char *)hint->data, "max_slave_replication_lag",
                            strlen("max_slave_replication_lag")) == 0)
            {
                target |= TARGET_RLAG_MAX;
            }
            else
            {
                MXS_ERROR("Unknown hint parameter "
                          "'%s' when 'max_slave_replication_lag' "
                          "was expected.",
                          (char *)hint->data);
            }
        }
        else if (hint->type == HINT_ROUTE_TO_SLAVE)
        {
            target = TARGET_SLAVE;
            MXS_DEBUG("%lu [get_route_target] Hint: route to "
                      "slave.",
                      pthread_self());
        }
        hint = hint->next;
    } /*< while (hint != NULL) */

    return (route_target_t)target;
}

/**
 * @brief Handle multi statement queries and load statements
 *
 * One of the possible types of handling required when a request is routed
 *
 *  @param ses          Router session
 *  @param querybuf     Buffer containing query to be routed
 *  @param packet_type  Type of packet (database specific)
 *  @param qtype        Query type
 */
void
handle_multi_temp_and_load(RWSplitSession *rses, GWBUF *querybuf,
                           uint8_t packet_type, uint32_t *qtype)
{
    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed.
     *
     * If we do not have a master node, assigning the forced node is not
     * effective since we don't have a node to force queries to. In this
     * situation, assigning QUERY_TYPE_WRITE for the query will trigger
     * the error processing. */
    if ((rses->target_node == NULL || rses->target_node != rses->current_master) &&
        check_for_multi_stmt(querybuf, rses->client_dcb->protocol, packet_type))
    {
        if (rses->current_master)
        {
            rses->target_node = rses->current_master;
            MXS_INFO("Multi-statement query, routing all future queries to master.");
        }
        else
        {
            *qtype |= QUERY_TYPE_WRITE;
        }
    }

    /*
     * Make checks prior to calling temp tables functions
     */

    if (rses == NULL || querybuf == NULL ||
        rses->client_dcb == NULL || rses->client_dcb->data == NULL)
    {
        if (rses == NULL || querybuf == NULL)
        {
            MXS_ERROR("[%s] Error: NULL variables for temp table checks: %p %p", __FUNCTION__,
                      rses, querybuf);
        }

        if (rses->client_dcb == NULL)
        {
            MXS_ERROR("[%s] Error: Client DCB is NULL.", __FUNCTION__);
        }

        if (rses->client_dcb->data == NULL)
        {
            MXS_ERROR("[%s] Error: User data in master server DBC is NULL.",
                      __FUNCTION__);
        }
    }

    else
    {
        /**
         * Check if the query has anything to do with temporary tables.
         */
        if (rses->have_tmp_tables)
        {
            check_drop_tmp_table(rses, querybuf);
            if (is_packet_a_query(packet_type) && is_read_tmp_table(rses, querybuf, *qtype))
            {
                *qtype |= QUERY_TYPE_MASTER_READ;
            }
        }
        check_create_tmp_table(rses, querybuf, *qtype);
    }

    /**
     * Check if this is a LOAD DATA LOCAL INFILE query. If so, send all queries
     * to the master until the last, empty packet arrives.
     */
    if (rses->load_data_state == LOAD_DATA_ACTIVE)
    {
        rses->rses_load_data_sent += gwbuf_length(querybuf);
    }
    else if (is_packet_a_query(packet_type))
    {
        qc_query_op_t queryop = qc_get_operation(querybuf);
        if (queryop == QUERY_OP_LOAD)
        {
            rses->load_data_state = LOAD_DATA_START;
            rses->rses_load_data_sent = 0;
        }
    }
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

    if (cmd == MYSQL_COM_STMT_FETCH)
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
    char errmsg[MAX_SERVER_ADDRESS_LEN * 2 + 100]; // Extra space for error message

    if (!found)
    {
        sprintf(errmsg, "Could not find a valid master connection");
    }
    else if (old_master && curr_master)
    {
        /** We found a master but it's not the same connection */
        ss_dassert(old_master != curr_master);
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
    else if (old_master)
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
        else
        {
            ss_dassert(false); // A session should always have a master reference
            sprintf(errmsg, "Was supposed to route to master but couldn't "
                    "find master in a suitable state");
        }
    }

    MXS_WARNING("[%s] Write query received from %s@%s. %s. Closing client connection.",
                rses->router->service()->name, rses->client_dcb->user,
                rses->client_dcb->remote, errmsg);
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

static inline bool query_creates_reply(mysql_server_cmd_t cmd)
{
    return cmd != MYSQL_COM_QUIT &&
           cmd != MYSQL_COM_STMT_SEND_LONG_DATA &&
           cmd != MYSQL_COM_STMT_CLOSE &&
           cmd != MYSQL_COM_STMT_FETCH; // Fetch is done mid-result
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
    mysql_server_cmd_t cmd = mxs_mysql_current_command(rses->client_dcb->session);

    if (rses->load_data_state != LOAD_DATA_ACTIVE &&
        query_creates_reply(cmd))
    {
        response = mxs::Backend::EXPECT_RESPONSE;
    }

    if (target->write(gwbuf_clone(querybuf), response))
    {
        if (store && !session_store_stmt(rses->client_dcb->session, querybuf, target->server()))
        {
            MXS_ERROR("Failed to store current statement, it won't be retried if it fails.");
        }

        atomic_add_uint64(&inst->stats().n_queries, 1);

        if (response == mxs::Backend::EXPECT_RESPONSE)
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

/**
 * @brief Get the root master server from MySQL replication tree
 *
 * Finds the server with the lowest replication depth level which has the master
 * status. Servers are checked even if they are in 'maintenance'.
 *
 * @param rses Router client session
 *
 * @return The backend that points to the master server or an empty reference
 * if the master cannot be found
 */
static SRWBackend get_root_master_backend(RWSplitSession *rses)
{
    SRWBackend candidate;
    SERVER master = {};

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend& backend = *it;
        if (backend->in_use())
        {
            if (backend == rses->current_master)
            {
                /** Store master state for better error reporting */
                master.status = backend->server()->status;
            }

            if (backend->is_master())
            {
                if (!candidate ||
                    (backend->server()->depth < candidate->server()->depth))
                {
                    candidate = backend;
                }
            }
        }
    }

    if (!candidate && rses->rses_config.master_failure_mode == RW_FAIL_INSTANTLY &&
        rses->current_master && rses->current_master->in_use())
    {
        MXS_ERROR("Could not find master among the backend servers. "
                  "Previous master's state : %s", STRSRVSTATUS(&master));
    }

    return candidate;
}
