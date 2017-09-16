/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.h"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <maxscale/alloc.h>

#include <maxscale/router.h>
#include "rwsplit_internal.h"
/**
 * @file rwsplit_route_stmt.c   The functions that support the routing of
 * queries to back end servers. All the functions in this module are internal
 * to the read write split router, and not intended to be called from
 * anywhere else.
 *
 * @verbatim
 * Revision History
 *
 * Date          Who                 Description
 * 08/08/2016    Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

extern int (*criteria_cmpfun[LAST_CRITERIA])(const void *, const void *);

static backend_ref_t *check_candidate_bref(backend_ref_t *cand,
                                           backend_ref_t *new,
                                           select_criteria_t sc);
static backend_ref_t *get_root_master_bref(ROUTER_CLIENT_SES *rses);

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
bool route_single_stmt(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf)
{
    qc_query_type_t qtype = QUERY_TYPE_UNKNOWN;
    int packet_type;
    DCB *target_dcb = NULL;
    route_target_t route_target;
    bool succp = false;
    bool non_empty_packet;

    ss_dassert(querybuf->next == NULL); // The buffer must be contiguous.
    ss_dassert(!GWBUF_IS_TYPE_UNDEFINED(querybuf));

    /* packet_type is a problem as it is MySQL specific */
    packet_type = determine_packet_type(querybuf, &non_empty_packet);
    qtype = determine_query_type(querybuf, packet_type, non_empty_packet);

    if (non_empty_packet)
    {
        handle_multi_temp_and_load(rses, querybuf, packet_type, (int *)&qtype);

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
        route_target = get_route_target(rses, qtype, querybuf->hint);
    }
    else
    {
        route_target = TARGET_MASTER;
        /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
        rses->rses_load_active = false;
        MXS_INFO("> LOAD DATA LOCAL INFILE finished: %lu bytes sent.",
                 rses->rses_load_data_sent + gwbuf_length(querybuf));
    }
    if (TARGET_IS_ALL(route_target))
    {
        succp = handle_target_is_all(route_target, inst, rses, querybuf, packet_type, qtype);
    }
    else
    {
        /* Now we have a lock on the router session */
        bool store_stmt = false;
        /**
         * There is a hint which either names the target backend or
         * hint which sets maximum allowed replication lag for the
         * backend.
         */
        if (TARGET_IS_NAMED_SERVER(route_target) ||
            TARGET_IS_RLAG_MAX(route_target))
        {
            succp = handle_hinted_target(rses, querybuf, route_target, &target_dcb);
        }
        else if (TARGET_IS_SLAVE(route_target))
        {
            succp = handle_slave_is_target(inst, rses, &target_dcb);
            store_stmt = rses->rses_config.retry_failed_reads;
        }
        else if (TARGET_IS_MASTER(route_target))
        {
            succp = handle_master_is_target(inst, rses, &target_dcb);

            if (!rses->rses_config.strict_multi_stmt &&
                rses->forced_node == rses->rses_master_ref)
            {
                /** Reset the forced node as we're in relaxed multi-statement mode */
                rses->forced_node = NULL;
            }
        }

        if (target_dcb && succp) /*< Have DCB of the target backend */
        {
            ss_dassert(!store_stmt || TARGET_IS_SLAVE(route_target));
            handle_got_target(inst, rses, querybuf, target_dcb, store_stmt);
        }
    }

    return succp;
} /* route_single_stmt */

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
bool route_session_write(ROUTER_CLIENT_SES *router_cli_ses,
                         GWBUF *querybuf, ROUTER_INSTANCE *inst,
                         int packet_type,
                         qc_query_type_t qtype)
{
    bool succp;
    rses_property_t *prop;
    backend_ref_t *backend_ref;
    int i;
    int max_nslaves;
    int nbackends;
    int nsucc;

    MXS_INFO("Session write, routing to all servers.");
    /** Maximum number of slaves in this router client session */
    max_nslaves =
        rses_get_max_slavecount(router_cli_ses, router_cli_ses->rses_nbackends);
    nsucc = 0;
    nbackends = 0;
    backend_ref = router_cli_ses->rses_backend_ref;

    /**
     * These are one-way messages and server doesn't respond to them.
     * Therefore reply processing is unnecessary and session
     * command property is not needed. It is just routed to all available
     * backends.
     */
    if (is_packet_a_one_way_message(packet_type))
    {
        int rc;

        for (i = 0; i < router_cli_ses->rses_nbackends; i++)
        {
            DCB *dcb = backend_ref[i].bref_dcb;

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO) &&
                BREF_IS_IN_USE((&backend_ref[i])))
            {
                MXS_INFO("Route query to %s \t[%s]:%d%s",
                         (SERVER_IS_MASTER(backend_ref[i].ref->server)
                          ? "master" : "slave"),
                         backend_ref[i].ref->server->name,
                         backend_ref[i].ref->server->port,
                         (i + 1 == router_cli_ses->rses_nbackends ? " <" : " "));
            }

            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                nbackends += 1;
                if ((rc = dcb->func.write(dcb, gwbuf_clone(querybuf))) == 1)
                {
                    nsucc += 1;
                }
            }
        }
        gwbuf_free(querybuf);
        goto return_succp;
    }

    if (router_cli_ses->rses_nbackends <= 0)
    {
        MXS_INFO("Router session doesn't have any backends in use. Routing failed. <");
        goto return_succp;
    }

    if (router_cli_ses->rses_config.max_sescmd_history > 0 &&
        router_cli_ses->rses_nsescmd >=
        router_cli_ses->rses_config.max_sescmd_history)
    {
        MXS_WARNING("Router session exceeded session command history limit. "
                    "Slave recovery is disabled and only slave servers with "
                    "consistent session state are used "
                    "for the duration of the session.");
        router_cli_ses->rses_config.disable_sescmd_history = true;
        router_cli_ses->rses_config.max_sescmd_history = 0;
    }

    if (router_cli_ses->rses_config.disable_sescmd_history)
    {
        rses_property_t *prop, *tmp;
        backend_ref_t *bref;
        bool conflict;

        prop = router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD];
        while (prop)
        {
            conflict = false;

            for (i = 0; i < router_cli_ses->rses_nbackends; i++)
            {
                bref = &backend_ref[i];
                if (BREF_IS_IN_USE(bref))
                {

                    if (bref->bref_sescmd_cur.position <=
                        prop->rses_prop_data.sescmd.position + 1)
                    {
                        conflict = true;
                        break;
                    }
                }
            }

            if (conflict)
            {
                break;
            }

            tmp = prop;
            router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD] = prop->rses_prop_next;
            rses_property_done(tmp);
            prop = router_cli_ses->rses_properties[RSES_PROP_TYPE_SESCMD];
        }
    }

    /**
     * Additional reference is created to querybuf to
     * prevent it from being released before properties
     * are cleaned up as a part of router sessionclean-up.
     */
    if ((prop = rses_property_init(RSES_PROP_TYPE_SESCMD)) == NULL)
    {
        MXS_ERROR("Router session property initialization failed");
        return false;
    }

    mysql_sescmd_init(prop, querybuf, packet_type, router_cli_ses);

    /** Add sescmd property to router client session */
    if (rses_property_add(router_cli_ses, prop) != 0)
    {
        MXS_ERROR("Session property addition failed.");
        return false;
    }

    for (i = 0; i < router_cli_ses->rses_nbackends; i++)
    {
        if (BREF_IS_IN_USE((&backend_ref[i])))
        {
            sescmd_cursor_t *scur;

            nbackends += 1;

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s \t[%s]:%d%s",
                         (SERVER_IS_MASTER(backend_ref[i].ref->server)
                          ? "master" : "slave"),
                         backend_ref[i].ref->server->name,
                         backend_ref[i].ref->server->port,
                         (i + 1 == router_cli_ses->rses_nbackends ? " <" : " "));
            }

            scur = backend_ref_get_sescmd_cursor(&backend_ref[i]);

            /**
             * Add one waiter to backend reference.
             */
            bref_set_state(get_bref_from_dcb(router_cli_ses, backend_ref[i].bref_dcb),
                           BREF_WAITING_RESULT);
            /**
             * Start execution if cursor is not already executing or this is the
             * master server. Otherwise, cursor will execute pending commands
             * when it completes the previous command.
             */
            if (sescmd_cursor_is_active(scur) && &backend_ref[i] != router_cli_ses->rses_master_ref)
            {
                nsucc += 1;
                MXS_INFO("Backend [%s]:%d already executing sescmd.",
                         backend_ref[i].ref->server->name,
                         backend_ref[i].ref->server->port);
            }
            else
            {
                if (execute_sescmd_in_backend(&backend_ref[i]))
                {
                    nsucc += 1;
                }
                else
                {
                    MXS_ERROR("Failed to execute session command in [%s]:%d",
                              backend_ref[i].ref->server->name,
                              backend_ref[i].ref->server->port);
                }
            }
        }
    }

    atomic_add(&router_cli_ses->rses_nsescmd, 1);

return_succp:
    /**
     * Routing must succeed to all backends that are used.
     * There must be at least one and at most max_nslaves+1 backends.
     */
    succp = (nbackends > 0 && nsucc == nbackends && nbackends <= max_nslaves + 1);
    return succp;
}

/**
 * @brief Function to hash keys in read-write split router
 *
 * Used to store information about temporary tables.
 *
 * @param key   key to be hashed, actually a character string
 * @result      the hash value integer
 */
int rwsplit_hashkeyfun(const void *key)
{
    if (key == NULL)
    {
        return 0;
    }

    unsigned int hash = 0, c = 0;
    const char *ptr = (const char *)key;

    while ((c = *ptr++))
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

/**
 * @brief Function to compare hash keys in read-write split router
 *
 * Used to manage information about temporary tables.
 *
 * @param key   first key to be compared, actually a character string
 * @param v2    second key to be compared, actually a character string
 * @result      1 if keys are equal, 0 otherwise
 */
int rwsplit_hashcmpfun(const void *v1, const void *v2)
{
    const char *i1 = (const char *)v1;
    const char *i2 = (const char *)v2;

    return strcmp(i1, i2);
}

/**
 * @brief Function to duplicate a hash value in read-write split router
 *
 * Used to manage information about temporary tables.
 *
 * @param fval  value to be duplicated, actually a character string
 * @result      the duplicated value, actually a character string
 */
void *rwsplit_hstrdup(const void *fval)
{
    char *str = (char *)fval;
    return MXS_STRDUP(str);
}

/**
 * @brief Function to free hash values in read-write split router
 *
 * Used to manage information about temporary tables.
 *
 * @param key   value to be freed
 */
void rwsplit_hfree(void *fval)
{
    MXS_FREE(fval);
}

/**
 * Provide the router with a pointer to a suitable backend dcb.
 *
 * Detect failures in server statuses and reselect backends if necessary.
 * If name is specified, server name becomes primary selection criteria.
 * Similarly, if max replication lag is specified, skip backends which lag too
 * much.
 *
 * @param p_dcb Address of the pointer to the resulting DCB
 * @param rses  Pointer to router client session
 * @param btype Backend type
 * @param name  Name of the backend which is primarily searched. May be NULL.
 *
 * @return True if proper DCB was found, false otherwise.
 */
bool rwsplit_get_dcb(DCB **p_dcb, ROUTER_CLIENT_SES *rses, backend_type_t btype,
                     char *name, int max_rlag)
{
    backend_ref_t *backend_ref;
    backend_ref_t *master_bref;
    int i;
    bool succp = false;

    CHK_CLIENT_RSES(rses);
    ss_dassert(p_dcb != NULL && *(p_dcb) == NULL);

    if (p_dcb == NULL)
    {
        goto return_succp;
    }
    backend_ref = rses->rses_backend_ref;

    /** Check whether using rses->forced_node as target SLAVE */
    if (rses->forced_node &&
        session_trx_is_read_only(rses->client_dcb->session))
    {
        *p_dcb = rses->forced_node->bref_dcb;
        succp = true;

        MXS_DEBUG("force_node found in READ ONLY transaction: use slave %s",
                  (*p_dcb)->server->unique_name);

        goto return_succp;
    }

    /** get root master from available servers */
    master_bref = get_root_master_bref(rses);

    if (name != NULL) /*< Choose backend by name from a hint */
    {
        ss_dassert(btype != BE_MASTER); /*< Master dominates and no name should be passed with it */

        for (i = 0; i < rses->rses_nbackends; i++)
        {
            SERVER_REF *b = backend_ref[i].ref;
            SERVER server;
            server.status = b->server->status;
            /**
             * To become chosen:
             * backend must be in use, name must match,
             * backend's role must be either slave, relay
             * server, or master.
             */
            if (BREF_IS_IN_USE((&backend_ref[i])) &&
                (strncasecmp(name, b->server->unique_name, PATH_MAX) == 0) &&
                (SERVER_IS_SLAVE(&server) || SERVER_IS_RELAY_SERVER(&server) ||
                 SERVER_IS_MASTER(&server)))
            {
                *p_dcb = backend_ref[i].bref_dcb;
                succp = true;
                ss_dassert(backend_ref[i].bref_dcb->state != DCB_STATE_ZOMBIE);
                break;
            }
        }
        if (succp)
        {
            goto return_succp;
        }
        else
        {
            btype = BE_SLAVE;
        }
    }

    if (btype == BE_SLAVE)
    {
        backend_ref_t *candidate_bref = NULL;

        for (i = 0; i < rses->rses_nbackends; i++)
        {
            SERVER_REF *b = backend_ref[i].ref;
            SERVER server;
            SERVER candidate;
            server.status = b->server->status;
            /**
             * Unused backend or backend which is not master nor
             * slave can't be used
             */
            if (!BREF_IS_IN_USE(&backend_ref[i]) ||
                (!SERVER_IS_MASTER(&server) && !SERVER_IS_SLAVE(&server)))
            {
                continue;
            }
            /**
             * If there are no candidates yet accept both master or
             * slave.
             */
            else if (candidate_bref == NULL)
            {
                /**
                 * Ensure that master has not changed dunring
                 * session and abort if it has.
                 */
                if (SERVER_IS_MASTER(&server) && &backend_ref[i] == master_bref)
                {
                    /** found master */
                    candidate_bref = &backend_ref[i];
                    candidate.status = candidate_bref->ref->server->status;
                    succp = true;
                }
                /**
                 * Ensure that max replication lag is not set
                 * or that candidate's lag doesn't exceed the
                 * maximum allowed replication lag.
                 */
                else if (max_rlag == MAX_RLAG_UNDEFINED ||
                         (b->server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                          b->server->rlag <= max_rlag))
                {
                    /** found slave */
                    candidate_bref = &backend_ref[i];
                    candidate.status = candidate_bref->ref->server->status;
                    succp = true;
                }
            }
            /**
             * If candidate is master, any slave which doesn't break
             * replication lag limits replaces it.
             */
            else if (SERVER_IS_MASTER(&candidate) && SERVER_IS_SLAVE(&server) &&
                     (max_rlag == MAX_RLAG_UNDEFINED ||
                      (b->server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                       b->server->rlag <= max_rlag)) &&
                     !rses->rses_config.master_accept_reads)
            {
                /** found slave */
                candidate_bref = &backend_ref[i];
                candidate.status = candidate_bref->ref->server->status;
                succp = true;
            }
            /**
             * When candidate exists, compare it against the current
             * backend and update assign it to new candidate if
             * necessary.
             */
            else if (SERVER_IS_SLAVE(&server) ||
                     (rses->rses_config.master_accept_reads && SERVER_IS_MASTER(&server)))
            {
                if (max_rlag == MAX_RLAG_UNDEFINED ||
                    (b->server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                     b->server->rlag <= max_rlag))
                {
                    candidate_bref = check_candidate_bref(candidate_bref, &backend_ref[i],
                                                          rses->rses_config.slave_selection_criteria);
                    candidate.status = candidate_bref->ref->server->status;
                }
                else
                {
                    MXS_INFO("Server [%s]:%d is too much behind the master, %d s. and can't be chosen.",
                             b->server->name, b->server->port, b->server->rlag);
                }
            }
        } /*<  for */

        /** Assign selected DCB's pointer value */
        if (candidate_bref != NULL)
        {
            *p_dcb = candidate_bref->bref_dcb;
        }

        goto return_succp;
    } /*< if (btype == BE_SLAVE) */
    /**
     * If target was originally master only then the execution jumps
     * directly here.
     */
    if (btype == BE_MASTER)
    {
        if (master_bref)
        {
            /** It is possible for the server status to change at any point in time
             * so copying it locally will make possible error messages
             * easier to understand */
            SERVER server;
            server.status = master_bref->ref->server->status;

            if (BREF_IS_IN_USE(master_bref))
            {
                if (SERVER_IS_MASTER(&server))
                {
                    *p_dcb = master_bref->bref_dcb;
                    succp = true;
                    /** if bref is in use DCB should not be closed */
                    ss_dassert(master_bref->bref_dcb->state != DCB_STATE_ZOMBIE);
                }
                else
                {
                    MXS_ERROR("Server '%s' should be master but "
                              "is %s instead and can't be chosen as the master.",
                              master_bref->ref->server->unique_name,
                              STRSRVSTATUS(&server));
                    succp = false;
                }
            }
            else
            {
                MXS_ERROR("Server '%s' is not in use and can't be "
                          "chosen as the master.",
                          master_bref->ref->server->unique_name);
                succp = false;
            }
        }
    }

return_succp:
    return succp;
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
route_target_t get_route_target(ROUTER_CLIENT_SES *rses,
                                qc_query_type_t qtype, HINT *hint)
{
    bool trx_active = session_trx_is_active(rses->client_dcb->session);
    bool load_active = rses->rses_load_active;
    mxs_target_t use_sql_variables_in = rses->rses_config.use_sql_variables_in;
    route_target_t target = TARGET_UNDEFINED;

    if (rses->forced_node && rses->forced_node == rses->rses_master_ref)
    {
        target = TARGET_MASTER;
    }
    /**
     * A cloned session, route everything to the master
     */
    else if (DCB_IS_CLONE(rses->client_dcb))
    {
        target = TARGET_MASTER;
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
        if (qc_query_is_type(qtype, QUERY_TYPE_READ) &&
            !(qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) ||
              qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT)))
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
             !qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) &&
             !qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT) &&
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
                   qc_query_is_type(qtype, QUERY_TYPE_EXEC_STMT) ||
                   qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) ||
                   qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT));

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

    return target;
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
handle_multi_temp_and_load(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                           int packet_type, int *qtype)
{
    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed.
     *
     * If we do not have a master node, assigning the forced node is not
     * effective since we don't have a node to force queries to. In this
     * situation, assigning QUERY_TYPE_WRITE for the query will trigger
     * the error processing. */
    if ((rses->forced_node == NULL || rses->forced_node != rses->rses_master_ref) &&
        check_for_multi_stmt(querybuf, rses->client_dcb->protocol, packet_type))
    {
        if (rses->rses_master_ref)
        {
            rses->forced_node = rses->rses_master_ref;
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
            check_drop_tmp_table(rses, querybuf, packet_type);
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
    if (rses->rses_load_active)
    {
        rses->rses_load_data_sent += gwbuf_length(querybuf);
    }
    else if (is_packet_a_query(packet_type))
    {
        qc_query_op_t queryop = qc_get_operation(querybuf);
        if (queryop == QUERY_OP_LOAD)
        {
            rses->rses_load_active = true;
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
bool handle_hinted_target(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                          route_target_t route_target, DCB **target_dcb)
{
    HINT *hint;
    char *named_server = NULL;
    backend_type_t btype; /*< target backend type */
    int rlag_max = MAX_RLAG_UNDEFINED;
    bool succp;

    hint = querybuf->hint;

    while (hint != NULL)
    {
        if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            /**
             * Set the name of searched
             * backend server.
             */
            named_server = hint->data;
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
    btype = route_target & TARGET_SLAVE ? BE_SLAVE : BE_MASTER;

    /**
     * Search backend server by name or replication lag.
     * If it fails, then try to find valid slave or master.
     */
    succp = rwsplit_get_dcb(target_dcb, rses, btype, named_server, rlag_max);

    if (!succp)
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
    return succp;
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
bool handle_slave_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                            DCB **target_dcb)
{
    int rlag_max = rses_get_max_replication_lag(rses);

    /**
     * Search suitable backend server, get DCB in target_dcb
     */
    if (rwsplit_get_dcb(target_dcb, rses, BE_SLAVE, NULL, rlag_max))
    {
        atomic_add_uint64(&inst->stats.n_slave, 1);
        return true;
    }
    else
    {
        MXS_INFO("Was supposed to route to slave but finding suitable one failed.");
        return false;
    }
}

/**
 * @brief Log master write failure
 *
 * @param rses Router session
 */
static void log_master_routing_failure(ROUTER_CLIENT_SES *rses, bool found,
                                       DCB *master_dcb, DCB *curr_master_dcb)
{
    char errmsg[MAX_SERVER_NAME_LEN * 2 + 100]; // Extra space for error message

    if (!found)
    {
        sprintf(errmsg, "Could not find a valid master connection");
    }
    else if (master_dcb && curr_master_dcb)
    {
        /** We found a master but it's not the same connection */
        ss_dassert(master_dcb != curr_master_dcb);
        if (master_dcb->server != curr_master_dcb->server)
        {
            sprintf(errmsg, "Master server changed from '%s' to '%s'",
                    master_dcb->server->unique_name,
                    curr_master_dcb->server->unique_name);
        }
        else
        {
            ss_dassert(false); // Currently we don't reconnect to the master
            sprintf(errmsg, "Connection to master '%s' was recreated",
                    curr_master_dcb->server->unique_name);
        }
    }
    else if (master_dcb)
    {
        /** We have an original master connection but we couldn't find it */
        sprintf(errmsg, "The connection to master server '%s' is not available",
                master_dcb->server->unique_name);
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
                rses->router->service->name, rses->client_dcb->user,
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
bool handle_master_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                             DCB **target_dcb)
{
    DCB *master_dcb = rses->rses_master_ref ? rses->rses_master_ref->bref_dcb : NULL;
    DCB *curr_master_dcb = NULL;
    bool succp = rwsplit_get_dcb(&curr_master_dcb, rses, BE_MASTER, NULL, MAX_RLAG_UNDEFINED);

    if (succp && master_dcb == curr_master_dcb)
    {
        atomic_add_uint64(&inst->stats.n_master, 1);
        *target_dcb = master_dcb;
    }
    else
    {
        if (succp && master_dcb == curr_master_dcb)
        {
            atomic_add_uint64(&inst->stats.n_master, 1);
            *target_dcb = master_dcb;
        }
        else
        {
            /** The original master is not available, we can't route the write */
            if (rses->rses_config.master_failure_mode == RW_ERROR_ON_WRITE)
            {
                succp = send_readonly_error(rses->client_dcb);

                if (rses->rses_master_ref && BREF_IS_IN_USE(rses->rses_master_ref))
                {
                    close_failed_bref(rses->rses_master_ref, true);
                    RW_CHK_DCB(rses->rses_master_ref, rses->rses_master_ref->bref_dcb);
                    dcb_close(rses->rses_master_ref->bref_dcb);
                    RW_CLOSE_BREF(rses->rses_master_ref);
                }
            }
            else
            {
                log_master_routing_failure(rses, succp, master_dcb, curr_master_dcb);
                succp = false;
            }
        }
    }

    return succp;
}

/**
 * @brief Handle got a target
 *
 * One of the possible types of handling required when a request is routed
 *
 *  @param inst         Router instance
 *  @param ses          Router session
 *  @param querybuf     Buffer containing query to be routed
 *  @param target_dcb   DCB for the target server
 *
 *  @return bool - true if succeeded, false otherwise
 */
bool
handle_got_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                  GWBUF *querybuf, DCB *target_dcb, bool store)
{
    backend_ref_t *bref;
    sescmd_cursor_t *scur;

    bref = get_bref_from_dcb(rses, target_dcb);

    /**
     * If the transaction is READ ONLY set forced_node to bref
     * That SLAVE backend will be used until COMMIT is seen
     */
    if (!rses->forced_node &&
        session_trx_is_read_only(rses->client_dcb->session))
    {
        rses->forced_node = bref;
        MXS_DEBUG("Setting forced_node SLAVE to %s within an opened READ ONLY transaction\n",
                  target_dcb->server->unique_name);
    }

    scur = &bref->bref_sescmd_cur;

    ss_dassert(target_dcb != NULL);

    MXS_INFO("Route query to %s \t[%s]:%d <",
             (SERVER_IS_MASTER(bref->ref->server) ? "master"
              : "slave"), bref->ref->server->name, bref->ref->server->port);
    /**
     * Store current statement if execution of previous session command is still
     * active. Since the master server's response is always used, we can safely
     * write session commands to the master even if it is already executing.
     */
    if (sescmd_cursor_is_active(scur) && bref != rses->rses_master_ref)
    {
        bref->bref_pending_cmd = gwbuf_append(bref->bref_pending_cmd, gwbuf_clone(querybuf));
        return true;
    }

    if (target_dcb->func.write(target_dcb, gwbuf_clone(querybuf)) == 1)
    {
        if (store && !session_store_stmt(rses->client_dcb->session, querybuf, target_dcb->server))
        {
            MXS_ERROR("Failed to store current statement, it won't be retried if it fails.");
        }

        backend_ref_t *bref;

        atomic_add_uint64(&inst->stats.n_queries, 1);
        /**
         * Add one query response waiter to backend reference
         */
        bref = get_bref_from_dcb(rses, target_dcb);
        bref_set_state(bref, BREF_QUERY_ACTIVE);
        bref_set_state(bref, BREF_WAITING_RESULT);

        /**
         * If a READ ONLYtransaction is ending set forced_node to NULL
         */
        if (rses->forced_node &&
            session_trx_is_read_only(rses->client_dcb->session) &&
            session_trx_is_ending(rses->client_dcb->session))
        {
            MXS_DEBUG("An opened READ ONLY transaction ends: forced_node is set to NULL");
            rses->forced_node = NULL;
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
 * @brief Create a generic router session property structure.
 *
 * @param prop_type     Property type
 *
 * @return property structure of requested type, or NULL if failed
 */
rses_property_t *rses_property_init(rses_property_type_t prop_type)
{
    rses_property_t *prop;

    prop = (rses_property_t *)MXS_CALLOC(1, sizeof(rses_property_t));
    if (prop == NULL)
    {
        return NULL;
    }
    prop->rses_prop_type = prop_type;
#if defined(SS_DEBUG)
    prop->rses_prop_chk_top = CHK_NUM_ROUTER_PROPERTY;
    prop->rses_prop_chk_tail = CHK_NUM_ROUTER_PROPERTY;
#endif

    CHK_RSES_PROP(prop);
    return prop;
}

/**
 * @brief Add property to the router client session
 *
 * Add property to the router_client_ses structure's rses_properties
 * array. The slot is determined by the type of property.
 * In each slot there is a list of properties of similar type.
 *
 * Router client session must be locked.
 *
 * @param rses      Router session
 * @param prop      Router session property to be added
 *
 * @return -1 on failure, 0 on success
 */
int rses_property_add(ROUTER_CLIENT_SES *rses, rses_property_t *prop)
{
    if (rses == NULL)
    {
        MXS_ERROR("Router client session is NULL. (%s:%d)", __FILE__, __LINE__);
        return -1;
    }
    if (prop == NULL)
    {
        MXS_ERROR("Router client session property is NULL. (%s:%d)", __FILE__, __LINE__);
        return -1;
    }
    rses_property_t *p;

    CHK_CLIENT_RSES(rses);
    CHK_RSES_PROP(prop);

    prop->rses_prop_rsession = rses;
    p = rses->rses_properties[prop->rses_prop_type];

    if (p == NULL)
    {
        rses->rses_properties[prop->rses_prop_type] = prop;
    }
    else
    {
        while (p->rses_prop_next != NULL)
        {
            p = p->rses_prop_next;
        }
        p->rses_prop_next = prop;
    }
    return 0;
}

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
static backend_ref_t *check_candidate_bref(backend_ref_t *cand,
                                           backend_ref_t *new,
                                           select_criteria_t sc)
{
    int (*p)(const void *, const void *);
    /** get compare function */
    p = criteria_cmpfun[sc];

    if (new == NULL)
    {
        return cand;
    }
    else if (cand == NULL || (p((void *)cand, (void *)new) > 0))
    {
        return new;
    }
    else
    {
        return cand;
    }
}

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param   rses pointer to router session
 * @return  pointer to backend reference of the root master or NULL
 *
 */
static backend_ref_t *get_root_master_bref(ROUTER_CLIENT_SES *rses)
{
    backend_ref_t *bref;
    backend_ref_t *candidate_bref = NULL;
    SERVER master = {};

    for (int i = 0; i < rses->rses_nbackends; i++)
    {
        bref = &rses->rses_backend_ref[i];
        if (bref && BREF_IS_IN_USE(bref))
        {
            ss_dassert(!BREF_IS_CLOSED(bref) && !BREF_HAS_FAILED(bref));
            if (bref == rses->rses_master_ref)
            {
                /** Store master state for better error reporting */
                master.status = bref->ref->server->status;
            }

            if (SERVER_IS_MASTER(bref->ref->server))
            {
                if (candidate_bref == NULL ||
                    (bref->ref->server->depth < candidate_bref->ref->server->depth))
                {
                    candidate_bref = bref;
                }
            }
        }
    }

    if (candidate_bref == NULL && rses->rses_config.master_failure_mode == RW_FAIL_INSTANTLY &&
        rses->rses_master_ref && BREF_IS_IN_USE(rses->rses_master_ref))
    {
        MXS_ERROR("Could not find master among the backend servers. "
                  "Previous master's state : %s", STRSRVSTATUS(&master));
    }

    return candidate_bref;
}
