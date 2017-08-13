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

#include <inttypes.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <new>


#include <maxscale/router.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/alloc.h>

#include "rwsplit_internal.hh"
#include "rwsplitsession.hh"

/**
 * The entry points for the read/write query splitting router module.
 *
 * This file contains the entry points that comprise the API to the read
 * write query splitting router. It also contains functions that are
 * directly called by the entry point functions. Some of these are used by
 * functions in other modules of the read write split router, others are
 * used only within this module.
 */

/** Maximum number of slaves */
#define MAX_SLAVE_COUNT "255"

/*
 * The functions that implement the router module API
 */

static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue);

static bool rwsplit_process_router_options(RWSplit *router,
                                           char **options);
static void handle_error_reply_client(MXS_SESSION *ses, RWSplitSession *rses,
                                      DCB *backend_dcb, GWBUF *errmsg);
static bool handle_error_new_connection(RWSplit *inst,
                                        RWSplitSession **rses,
                                        DCB *backend_dcb, GWBUF *errmsg);
static bool have_enough_servers(RWSplitSession *rses, const int min_nsrv,
                                int router_nsrv, RWSplit *router);
static bool route_stored_query(RWSplitSession *rses);

/**
 * Internal functions
 */

/**
 * @brief Get count of backend servers that are slaves.
 *
 * Find out the number of read backend servers.
 * Depending on the configuration value type, either copy direct count
 * of slave connections or calculate the count from percentage value.
 *
 * @param   rses Router client session
 * @param   router_nservers The number of backend servers in total
 */
int rses_get_max_slavecount(RWSplitSession *rses)
{
    int conf_max_nslaves;
    int router_nservers = rses->rses_nbackends;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_config.max_slave_connections > 0)
    {
        conf_max_nslaves = rses->rses_config.max_slave_connections;
    }
    else
    {
        conf_max_nslaves = (router_nservers * rses->rses_config.rw_max_slave_conn_percent) / 100;
    }

    return MXS_MIN(router_nservers - 1, MXS_MAX(1, conf_max_nslaves));
}

/*
 * @brief Get the maximum replication lag for this router
 *
 * @param   rses    Router client session
 * @return  Replication lag from configuration or very large number
 */
int rses_get_max_replication_lag(RWSplitSession *rses)
{
    int conf_max_rlag;

    CHK_CLIENT_RSES(rses);

    /** if there is no configured value, then longest possible int is used */
    if (rses->rses_config.max_slave_replication_lag > 0)
    {
        conf_max_rlag = rses->rses_config.max_slave_replication_lag;
    }
    else
    {
        conf_max_rlag = ~(1 << 31);
    }

    return conf_max_rlag;
}

/**
 * @brief Find a back end reference that matches the given DCB
 *
 * Finds out if there is a backend reference pointing at the DCB given as
 * parameter.
 *
 * @param rses  router client session
 * @param dcb   DCB
 *
 * @return backend reference pointer if succeed or NULL
 */
SRWBackend get_backend_from_dcb(RWSplitSession *rses, DCB *dcb)
{
    ss_dassert(dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    CHK_DCB(dcb);
    CHK_CLIENT_RSES(rses);

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend& backend = *it;

        if (backend->dcb() == dcb)
        {
            return backend;
        }
    }

    /** We should always have a valid backend reference */
    ss_dassert(false);
    return SRWBackend();
}

/**
 * @brief Process router options
 *
 * @param router Router instance
 * @param options Router options
 * @return True on success, false if a configuration error was found
 */
static bool rwsplit_process_router_options(Config& config,
                                           char **options)
{
    int i;
    char *value;
    select_criteria_t c;

    if (options == NULL)
    {
        return true;
    }

    MXS_WARNING("Router options for readwritesplit are deprecated.");

    bool success = true;

    for (i = 0; options[i]; i++)
    {
        if ((value = strchr(options[i], '=')) == NULL)
        {
            MXS_ERROR("Unsupported router option \"%s\" for readwritesplit router.", options[i]);
            success = false;
        }
        else
        {
            *value = 0;
            value++;
            if (strcmp(options[i], "slave_selection_criteria") == 0)
            {
                c = GET_SELECT_CRITERIA(value);
                ss_dassert(c == LEAST_GLOBAL_CONNECTIONS ||
                           c == LEAST_ROUTER_CONNECTIONS || c == LEAST_BEHIND_MASTER ||
                           c == LEAST_CURRENT_OPERATIONS || c == UNDEFINED_CRITERIA);

                if (c == UNDEFINED_CRITERIA)
                {
                    MXS_ERROR("Unknown slave selection criteria \"%s\". "
                              "Allowed values are LEAST_GLOBAL_CONNECTIONS, "
                              "LEAST_ROUTER_CONNECTIONS, LEAST_BEHIND_MASTER,"
                              "and LEAST_CURRENT_OPERATIONS.",
                              STRCRITERIA(config.slave_selection_criteria));
                    success = false;
                }
                else
                {
                    config.slave_selection_criteria = c;
                }
            }
            else if (strcmp(options[i], "max_sescmd_history") == 0)
            {
                config.max_sescmd_history = atoi(value);
            }
            else if (strcmp(options[i], "disable_sescmd_history") == 0)
            {
                config.disable_sescmd_history = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_accept_reads") == 0)
            {
                config.master_accept_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "strict_multi_stmt") == 0)
            {
                config.strict_multi_stmt = config_truth_value(value);
            }
            else if (strcmp(options[i], "retry_failed_reads") == 0)
            {
                config.retry_failed_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_failure_mode") == 0)
            {
                if (strcasecmp(value, "fail_instantly") == 0)
                {
                    config.master_failure_mode = RW_FAIL_INSTANTLY;
                }
                else if (strcasecmp(value, "fail_on_write") == 0)
                {
                    config.master_failure_mode = RW_FAIL_ON_WRITE;
                }
                else if (strcasecmp(value, "error_on_write") == 0)
                {
                    config.master_failure_mode = RW_ERROR_ON_WRITE;
                }
                else
                {
                    MXS_ERROR("Unknown value for 'master_failure_mode': %s", value);
                    success = false;
                }
            }
            else
            {
                MXS_ERROR("Unknown router option \"%s=%s\" for readwritesplit router.",
                          options[i], value);
                success = false;
            }
        }
    } /*< for */

    return success;
}

// TODO: Don't process parameters in readwritesplit
static bool handle_max_slaves(Config& config, const char *str)
{
    bool rval = true;
    char *endptr;
    int val = strtol(str, &endptr, 10);

    if (*endptr == '%' && *(endptr + 1) == '\0')
    {
        config.rw_max_slave_conn_percent = val;
        config.max_slave_connections = 0;
    }
    else if (*endptr == '\0')
    {
        config.max_slave_connections = val;
        config.rw_max_slave_conn_percent = 0;
    }
    else
    {
        MXS_ERROR("Invalid value for 'max_slave_connections': %s", str);
        rval = false;
    }

    return rval;
}

/**
 * @brief Handle an error reply for a client
 *
 * @param ses           Session
 * @param rses          Router session
 * @param backend_dcb   DCB for the backend server that has failed
 * @param errmsg        GWBUF containing the error message
 */
static void handle_error_reply_client(MXS_SESSION *ses, RWSplitSession *rses,
                                      DCB *backend_dcb, GWBUF *errmsg)
{

    mxs_session_state_t sesstate = ses->state;
    DCB *client_dcb = ses->client_dcb;

    SRWBackend backend = get_backend_from_dcb(rses, backend_dcb);

    backend->close();

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        CHK_DCB(client_dcb);
        client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
    }
}

static bool reroute_stored_statement(RWSplitSession *rses, const SRWBackend& old, GWBUF *stored)
{
    bool success = false;

    if (!session_trx_is_active(rses->client_dcb->session))
    {
        /**
         * Only try to retry the read if autocommit is enabled and we are
         * outside of a transaction
         */
        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            SRWBackend& backend = *it;

            if (backend->in_use() && backend != old &&
                !backend->is_master() &&
                backend->is_slave())
            {
                /** Found a valid candidate; a non-master slave that's in use */
                if (backend->write(stored))
                {
                    MXS_INFO("Retrying failed read at '%s'.", backend->name());
                    ss_dassert(backend->get_reply_state() == REPLY_STATE_DONE);
                    LOG_RS(backend, REPLY_STATE_START);
                    backend->set_reply_state(REPLY_STATE_START);
                    rses->expected_responses++;
                    success = true;
                    break;
                }
            }
        }

        if (!success && rses->current_master && rses->current_master->in_use())
        {
            /**
             * Either we failed to write to the slave or no valid slave was found.
             * Try to retry the read on the master.
             */
            if (rses->current_master->write(stored))
            {
                MXS_INFO("Retrying failed read at '%s'.", rses->current_master->name());
                LOG_RS(rses->current_master, REPLY_STATE_START);
                ss_dassert(rses->current_master->get_reply_state() == REPLY_STATE_DONE);
                rses->current_master->set_reply_state(REPLY_STATE_START);
                rses->expected_responses++;
                success = true;
            }
        }
    }

    return success;
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
static bool handle_error_new_connection(RWSplit *inst,
                                        RWSplitSession **rses,
                                        DCB *backend_dcb, GWBUF *errmsg)
{
    RWSplitSession *myrses = *rses;
    SRWBackend backend = get_backend_from_dcb(myrses, backend_dcb);

    MXS_SESSION* ses = backend_dcb->session;
    bool route_stored = false;
    CHK_SESSION(ses);

    if (backend->is_waiting_result())
    {
        /**
         * A query was sent through the backend and it is waiting for a reply.
         * Try to reroute the statement to a working server or send an error
         * to the client.
         */
        GWBUF *stored = NULL;
        const SERVER *target = NULL;
        if (!session_take_stmt(backend_dcb->session, &stored, &target) ||
            target != backend->backend()->server ||
            !reroute_stored_statement(*rses, backend, stored))
        {
            /**
             * We failed to route the stored statement or no statement was
             * stored for this server. Either way we can safely free the buffer
             * and decrement the expected response count.
             */
            gwbuf_free(stored);
            myrses->expected_responses--;

            if (backend->session_command_count())
            {
                /**
                 * The backend was executing a command that requires a reply.
                 * Send an error to the client to let it know the query has
                 * failed.
                 */
                DCB *client_dcb = ses->client_dcb;
                client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
            }

            if (myrses->expected_responses == 0)
            {
                /** The response from this server was the last one, try to
                 * route all queued queries */
                route_stored = true;
            }
        }
    }

    /** Close the current connection. This needs to be done before routing any
     * of the stored queries. If we route a stored query before the connection
     * is closed, it's possible that the routing logic will pick the failed
     * server as the target. */
    backend->close();

    if (route_stored)
    {
        route_stored_query(myrses);
    }

    int max_nslaves = rses_get_max_slavecount(myrses);
    bool succp;
    /**
     * Try to get replacement slave or at least the minimum
     * number of slave connections for router session.
     */
    if (inst->config().disable_sescmd_history)
    {
        succp = have_enough_servers(myrses, 1, myrses->rses_nbackends, inst) ? true : false;
    }
    else
    {
        succp = select_connect_backend_servers(myrses->rses_nbackends, max_nslaves,
                                               myrses->rses_config.slave_selection_criteria,
                                               ses, inst, myrses,
                                               connection_type::SLAVE);
    }

    return succp;
}

/**
 * @brief Calculate whether we have enough servers to route a query
 *
 * @param p_rses        Router session
 * @param min_nsrv      Minimum number of servers that is sufficient
 * @param nsrv          Actual number of servers
 * @param router        Router instance
 *
 * @return bool - whether enough, side effect is error logging
 */
static bool have_enough_servers(RWSplitSession *rses, const int min_nsrv,
                                int router_nsrv, RWSplit *router)
{
    bool succp = true;

    /** With too few servers session is not created */
    if (router_nsrv < min_nsrv ||
        MXS_MAX((rses)->rses_config.max_slave_connections,
                (router_nsrv * (rses)->rses_config.rw_max_slave_conn_percent) /
                100) < min_nsrv)
    {
        if (router_nsrv < min_nsrv)
        {
            MXS_ERROR("Unable to start %s service. There are "
                      "too few backend servers available. Found %d "
                      "when %d is required.",
                      router->service()->name, router_nsrv, min_nsrv);
        }
        else
        {
            int pct = (rses)->rses_config.rw_max_slave_conn_percent / 100;
            int nservers = router_nsrv * pct;

            if ((rses)->rses_config.max_slave_connections < min_nsrv)
            {
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d when %d is required.",
                          router->service()->name,
                          (rses)->rses_config.max_slave_connections, min_nsrv);
            }
            if (nservers < min_nsrv)
            {
                double dbgpct = ((double)min_nsrv / (double)router_nsrv) * 100.0;
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d%% when at least %.0f%% "
                          "would be required.",
                          router->service()->name,
                          (rses)->rses_config.rw_max_slave_conn_percent, dbgpct);
            }
        }
        succp = false;
    }

    return succp;
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
static bool route_stored_query(RWSplitSession *rses)
{
    bool rval = true;

    /** Loop over the stored statements as long as the routeQuery call doesn't
     * append more data to the queue. If it appends data to the queue, we need
     * to wait for a response before attempting another reroute */
    while (rses->query_queue)
    {
        GWBUF* query_queue = modutil_get_next_MySQL_packet(&rses->query_queue);
        query_queue = gwbuf_make_contiguous(query_queue);

        /** Store the query queue locally for the duration of the routeQuery call.
         * This prevents recursive calls into this function. */
        GWBUF *temp_storage = rses->query_queue;
        rses->query_queue = NULL;

        if (!routeQuery((MXS_ROUTER*)rses->router, (MXS_ROUTER_SESSION*)rses, query_queue))
        {
            rval = false;
            char* sql = modutil_get_SQL(query_queue);

            if (sql)
            {
                MXS_ERROR("Routing query \"%s\" failed.", sql);
                MXS_FREE(sql);
            }
            else
            {
                MXS_ERROR("Failed to route query.");
            }
            gwbuf_free(query_queue);
        }

        if (rses->query_queue == NULL)
        {
            /** Query successfully routed and no responses are expected */
            rses->query_queue = temp_storage;
        }
        else
        {
            /** Routing was stopped, we need to wait for a response before retrying */
            rses->query_queue = gwbuf_append(temp_storage, rses->query_queue);
            break;
        }
    }

    return rval;
}

/**
 * @brief Check if we have received a complete reply from the backend
 *
 * @param backend Backend reference
 * @param buffer  Buffer containing the response
 *
 * @return True if the complete response has been received
 */
bool reply_is_complete(SRWBackend backend, GWBUF *buffer)
{
    mysql_server_cmd_t cmd = mxs_mysql_current_command(backend->dcb()->session);

    if (backend->get_reply_state() == REPLY_STATE_START && !mxs_mysql_is_result_set(buffer))
    {
        if (cmd == MYSQL_COM_STMT_PREPARE || !mxs_mysql_more_results_after_ok(buffer))
        {
            /** Not a result set, we have the complete response */
            LOG_RS(backend, REPLY_STATE_DONE);
            backend->set_reply_state(REPLY_STATE_DONE);
        }
    }
    else
    {
        bool more = false;
        int old_eof = backend->get_reply_state() == REPLY_STATE_RSET_ROWS ? 1 : 0;
        int n_eof = modutil_count_signal_packets(buffer, old_eof, &more);

        if (n_eof == 0)
        {
            /** Waiting for the EOF packet after the column definitions */
            LOG_RS(backend, REPLY_STATE_RSET_COLDEF);
            backend->set_reply_state(REPLY_STATE_RSET_COLDEF);
        }
        else if (n_eof == 1 && cmd != MYSQL_COM_FIELD_LIST)
        {
            /** Waiting for the EOF packet after the rows */
            LOG_RS(backend, REPLY_STATE_RSET_ROWS);
            backend->set_reply_state(REPLY_STATE_RSET_ROWS);
        }
        else
        {
            /** We either have a complete result set or a response to
             * a COM_FIELD_LIST command */
            ss_dassert(n_eof == 2 || (n_eof == 1 && cmd == MYSQL_COM_FIELD_LIST));
            LOG_RS(backend, REPLY_STATE_DONE);
            backend->set_reply_state(REPLY_STATE_DONE);

            if (more)
            {
                /** The server will send more resultsets */
                LOG_RS(backend, REPLY_STATE_START);
                backend->set_reply_state(REPLY_STATE_START);
            }
        }
    }

    return backend->get_reply_state() == REPLY_STATE_DONE;
}

void close_all_connections(RWSplitSession* rses)
{
    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend& backend = *it;

        if (backend->in_use())
        {
            backend->close();
        }
    }
}

void check_and_log_backend_state(const SRWBackend& backend, DCB* problem_dcb)
{
    if (backend)
    {
        /** This is a valid DCB for a backend ref */
        if (backend->in_use() && backend->dcb() == problem_dcb)
        {
            ss_dassert(false);
            MXS_ERROR("Backend '%s' is still in use and points to the problem DCB.",
                      backend->name());
        }
    }
    else
    {
        const char *remote = problem_dcb->state == DCB_STATE_POLLING &&
                             problem_dcb->server ? problem_dcb->server->unique_name : "CLOSED";

        MXS_ERROR("DCB connected to '%s' is not in use by the router "
                  "session, not closing it. DCB is in state '%s'",
                  remote, STRDCBSTATE(problem_dcb->state));
    }
}

RWSplit::RWSplit(SERVICE* service, const Config& config):
    m_service(service),
    m_config(config)
{
}

RWSplit::~RWSplit()
{
}

SERVICE* RWSplit::service() const
{
    return m_service;
}

const Config& RWSplit::config() const
{
    return m_config;
}

Stats& RWSplit::stats()
{
    return m_stats;
}

RWSplitSession::RWSplitSession(const Config& config):
    rses_config(config)
{
}

/**
 * API function definitions
 */

/**
 * @brief Create a new readwritesplit router instance
 *
 * An instance of the router is required for each service that uses this router.
 * One instance of the router will handle multiple router sessions.
 *
 * @param service The service this router is being create for
 * @param options The options for this query router
 *
 * @return New router instance or NULL on error
 */
static MXS_ROUTER* createInstance(SERVICE *service, char **options)
{

    MXS_CONFIG_PARAMETER* params = service->svc_config_param;
    Config config(params);

    if (!handle_max_slaves(config, config_get_string(params, "max_slave_connections")) ||
        (options && !rwsplit_process_router_options(config, options)))
    {
        return NULL;
    }

    /** These options cancel each other out */
    if (config.disable_sescmd_history && config.max_sescmd_history > 0)
    {
        config.max_sescmd_history = 0;
    }

    return (MXS_ROUTER*)new (std::nothrow) RWSplit(service, config);
}

/**
 * @brief Create a new session for this router instance
 *
 * The session is used to store all the data required by the router for a
 * particular client connection. The instance of the router that relates to a
 * particular service is passed as the first parameter. The second parameter is
 * the session that has been created in response to the request from a client
 * for a connection. The passed session contains generic information; this
 * function creates the session structure that holds router specific data.
 * There is often a one to one relationship between sessions and router
 * sessions, although it is possible to create configurations where a
 * connection is handled by multiple routers, one after another.
 *
 * @param instance The router instance data
 * @param session  The MaxScale session (generic connection data)
 *
 * @return New router session or NULL on error
 */
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER *router_inst, MXS_SESSION *session)
{
    RWSplit* router = (RWSplit*)router_inst;
    RWSplitSession* client_rses = new (std::nothrow) RWSplitSession(router->config());

    if (client_rses == NULL)
    {
        return NULL;
    }

    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
    client_rses->rses_closed = false;
    client_rses->router = router;
    client_rses->client_dcb = session->client_dcb;
    client_rses->have_tmp_tables = false;
    client_rses->expected_responses = 0;
    client_rses->query_queue = NULL;
    client_rses->load_data_state = LOAD_DATA_INACTIVE;
    client_rses->sent_sescmd = 0;
    client_rses->recv_sescmd = 0;
    client_rses->sescmd_count = 1; // Needs to be a positive number to work

    int router_nservers = router->service()->n_dbref;
    const int min_nservers = 1;

    if (!have_enough_servers(client_rses, min_nservers, router_nservers, router))
    {
        delete client_rses;
        return NULL;
    }

    for (SERVER_REF *sref = router->service()->dbref; sref; sref = sref->next)
    {
        if (sref->active)
        {
            client_rses->backends.push_back(SRWBackend(new RWBackend(sref)));
        }
    }

    client_rses->rses_nbackends = router_nservers;

    int max_nslaves = rses_get_max_slavecount(client_rses);

    if (!select_connect_backend_servers(router_nservers, max_nslaves,
                                        client_rses->rses_config.slave_selection_criteria,
                                        session, router, client_rses,
                                        connection_type::ALL))
    {
        /**
         * At least the master must be found if the router is in the strict mode.
         * If sessions without master are allowed, only a slave must be found.
         */
        delete client_rses;
        return NULL;
    }

    if (client_rses->rses_config.rw_max_slave_conn_percent)
    {
        int n_conn = 0;
        double pct = (double)client_rses->rses_config.rw_max_slave_conn_percent / 100.0;
        n_conn = MXS_MAX(floor((double)client_rses->rses_nbackends * pct), 1);
        client_rses->rses_config.max_slave_connections = n_conn;
    }

    router->stats().n_sessions += 1;

    return (MXS_ROUTER_SESSION*)client_rses;
}

/**
 * @brief Close a router session
 *
 * Close a session with the router, this is the mechanism by which a router
 * may perform cleanup. The instance of the router that relates to
 * the relevant service is passed, along with the router session that is to
 * be closed. The freeSession will be called once the session has been closed.
 *
 * @param instance The router instance data
 * @param session  The router session being closed
 */
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session)
{
    RWSplitSession *router_cli_ses = (RWSplitSession *)router_session;
    CHK_CLIENT_RSES(router_cli_ses);

    if (!router_cli_ses->rses_closed)
    {
        router_cli_ses->rses_closed = true;
        close_all_connections(router_cli_ses);

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO) &&
            router_cli_ses->sescmd_list.size())
        {
            std::string sescmdstr;

            for (mxs::SessionCommandList::iterator it = router_cli_ses->sescmd_list.begin();
                 it != router_cli_ses->sescmd_list.end(); it++)
            {
                mxs::SSessionCommand& scmd = *it;
                sescmdstr += scmd->to_string();
                sescmdstr += "\n";
            }

            MXS_INFO("Executed session commands:\n%s", sescmdstr.c_str());
        }
    }
}

/**
 * @brief Free a router session
 *
 * When a router session has been closed, freeSession can be called to free
 * allocated resources.
 *
 * @param instance The router instance
 * @param session  The router session
 *
 */
static void freeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* session)
{
    RWSplitSession* rses = reinterpret_cast<RWSplitSession*>(session);
    delete rses;
}

/**
 * @brief The main routing entry point
 *
 * The routeQuery function will make the routing decision based on the contents
 * of the instance, session and the query itself. The query always represents
 * a complete MariaDB/MySQL packet because we define the RCAP_TYPE_STMT_INPUT in
 * getCapabilities().
 *
 * @param instance       Router instance
 * @param router_session Router session associated with the client
 * @param querybuf       Buffer containing the query
 *
 * @return 1 on success, 0 on error
 */
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *querybuf)
{
    RWSplit *inst = (RWSplit *) instance;
    RWSplitSession *rses = (RWSplitSession *) router_session;
    int rval = 0;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        closed_session_reply(querybuf);
    }
    else
    {
        /** Gather the information required to make routing decisions */
        RouteInfo info(rses, querybuf);

        if (rses->query_queue == NULL &&
            (rses->expected_responses == 0 ||
             info.command == MYSQL_COM_STMT_FETCH ||
             rses->load_data_state == LOAD_DATA_ACTIVE))
        {
            /** No active or pending queries */
            if (route_single_stmt(inst, rses, querybuf, info))
            {
                rval = 1;
            }
        }
        else
        {
            /**
             * We are already processing a request from the client. Store the
             * new query and wait for the previous one to complete.
             */
            ss_dassert(rses->expected_responses || rses->query_queue);
            MXS_INFO("Storing query (len: %d cmd: %0x), expecting %d replies to current command",
                     gwbuf_length(querybuf), GWBUF_DATA(querybuf)[4],
                     rses->expected_responses);
            rses->query_queue = gwbuf_append(rses->query_queue, querybuf);
            querybuf = NULL;
            rval = 1;

            if (rses->expected_responses == 0 && !route_stored_query(rses))
            {
                rval = 0;
            }
        }
    }

    if (querybuf != NULL)
    {
        gwbuf_free(querybuf);
    }

    return rval;
}

/**
 * @brief Diagnostics routine
 *
 * Print query router statistics to the DCB passed in
 *
 * @param instance The router instance
 * @param dcb      The DCB for diagnostic output
 */
static void diagnostics(MXS_ROUTER *instance, DCB *dcb)
{
    RWSplit *router = (RWSplit *)instance;
    const char *weightby = serviceGetWeightingParameter(router->service());
    double master_pct = 0.0, slave_pct = 0.0, all_pct = 0.0;

    dcb_printf(dcb, "\n");
    dcb_printf(dcb, "\tuse_sql_variables_in:      %s\n",
               mxs_target_to_str(router->config().use_sql_variables_in));
    dcb_printf(dcb, "\tslave_selection_criteria:  %s\n",
               select_criteria_to_str(router->config().slave_selection_criteria));
    dcb_printf(dcb, "\tmaster_failure_mode:       %s\n",
               failure_mode_to_str(router->config().master_failure_mode));
    dcb_printf(dcb, "\tmax_slave_replication_lag: %d\n",
               router->config().max_slave_replication_lag);
    dcb_printf(dcb, "\tretry_failed_reads:        %s\n",
               router->config().retry_failed_reads ? "true" : "false");
    dcb_printf(dcb, "\tstrict_multi_stmt:         %s\n",
               router->config().strict_multi_stmt ? "true" : "false");
    dcb_printf(dcb, "\tdisable_sescmd_history:    %s\n",
               router->config().disable_sescmd_history ? "true" : "false");
    dcb_printf(dcb, "\tmax_sescmd_history:        %lu\n",
               router->config().max_sescmd_history);
    dcb_printf(dcb, "\tmaster_accept_reads:       %s\n",
               router->config().master_accept_reads ? "true" : "false");
    dcb_printf(dcb, "\n");

    if (router->stats().n_queries > 0)
    {
        master_pct = ((double)router->stats().n_master / (double)router->stats().n_queries) * 100.0;
        slave_pct = ((double)router->stats().n_slave / (double)router->stats().n_queries) * 100.0;
        all_pct = ((double)router->stats().n_all / (double)router->stats().n_queries) * 100.0;
    }

    dcb_printf(dcb, "\tNumber of router sessions:           	%" PRIu64 "\n",
               router->stats().n_sessions);
    dcb_printf(dcb, "\tCurrent no. of router sessions:      	%d\n",
               router->service()->stats.n_current);
    dcb_printf(dcb, "\tNumber of queries forwarded:          	%" PRIu64 "\n",
               router->stats().n_queries);
    dcb_printf(dcb, "\tNumber of queries forwarded to master:	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_master, master_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to slave: 	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_slave, slave_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to all:   	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_all, all_pct);

    if (*weightby)
    {
        dcb_printf(dcb, "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb, "\t\tServer               Target %%    Connections  "
                   "Operations\n");
        dcb_printf(dcb, "\t\t                               Global  Router\n");
        for (SERVER_REF *ref = router->service()->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb, "\t\t%-20s %3.1f%%     %-6d  %-6d  %d\n",
                       ref->server->unique_name, (float)ref->weight / 10,
                       ref->server->stats.n_current, ref->connections,
                       ref->server->stats.n_current_ops);
        }
    }
}

/**
 * @brief JSON diagnostics routine
 *
 * @param instance The router instance
 * @param dcb      The DCB for diagnostic output
 */
static json_t* diagnostics_json(const MXS_ROUTER *instance)
{
    RWSplit *router = (RWSplit *)instance;

    json_t* rval = json_object();

    json_object_set_new(rval, "use_sql_variables_in",
                        json_string(mxs_target_to_str(router->config().use_sql_variables_in)));
    json_object_set_new(rval, "slave_selection_criteria",
                        json_string(select_criteria_to_str(router->config().slave_selection_criteria)));
    json_object_set_new(rval, "master_failure_mode",
                        json_string(failure_mode_to_str(router->config().master_failure_mode)));
    json_object_set_new(rval, "max_slave_replication_lag",
                        json_integer(router->config().max_slave_replication_lag));
    json_object_set_new(rval, "retry_failed_reads",
                        json_boolean(router->config().retry_failed_reads));
    json_object_set_new(rval, "strict_multi_stmt",
                        json_boolean(router->config().strict_multi_stmt));
    json_object_set_new(rval, "disable_sescmd_history",
                        json_boolean(router->config().disable_sescmd_history));
    json_object_set_new(rval, "max_sescmd_history",
                        json_integer(router->config().max_sescmd_history));
    json_object_set_new(rval, "master_accept_reads",
                        json_boolean(router->config().master_accept_reads));


    json_object_set_new(rval, "connections", json_integer(router->stats().n_sessions));
    json_object_set_new(rval, "current_connections", json_integer(router->service()->stats.n_current));
    json_object_set_new(rval, "queries", json_integer(router->stats().n_queries));
    json_object_set_new(rval, "route_master", json_integer(router->stats().n_master));
    json_object_set_new(rval, "route_slave", json_integer(router->stats().n_slave));
    json_object_set_new(rval, "route_all", json_integer(router->stats().n_all));

    const char *weightby = serviceGetWeightingParameter(router->service());

    if (*weightby)
    {
        json_object_set_new(rval, "weightby", json_string(weightby));
    }

    return rval;
}

static void log_unexpected_response(DCB* dcb, GWBUF* buffer)
{
    if (mxs_mysql_is_err_packet(buffer))
    {
        /** This should be the only valid case where the server sends a response
         * without the client sending one first. MaxScale does not yet advertise
         * the progress reporting flag so we don't need to handle it. */
        uint8_t* data = GWBUF_DATA(buffer);
        size_t len = MYSQL_GET_PAYLOAD_LEN(data);
        uint16_t errcode = MYSQL_GET_ERRCODE(data);
        std::string errstr((char*)data + 7, (char*)data + 7 + len - 3);

        if (errcode == ER_CONNECTION_KILLED)
        {
            MXS_INFO("Connection from '%s'@'%s' to '%s' was killed",
                     dcb->session->client_dcb->user,
                     dcb->session->client_dcb->remote,
                     dcb->server->unique_name);
        }
        else
        {
            MXS_WARNING("Server '%s' sent an unexpected error: %hu, %s",
                        dcb->server->unique_name, errcode, errstr.c_str());
        }
    }
    else
    {
        MXS_ERROR("Unexpected internal state: received response 0x%02hhx from "
                  "server '%s' when no response was expected",
                  mxs_mysql_get_command(buffer), dcb->server->unique_name);
        ss_dassert(false);
    }
}

/**
 * @brief Client Reply routine
 *
 * @param   instance       The router instance
 * @param   router_session The router session
 * @param   backend_dcb    The backend DCB
 * @param   queue          The Buffer containing the reply
 */
static void clientReply(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *writebuf,
                        DCB *backend_dcb)
{
    RWSplitSession *rses = (RWSplitSession *)router_session;
    DCB *client_dcb = backend_dcb->session->client_dcb;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        gwbuf_free(writebuf);
        return;
    }

    SRWBackend backend = get_backend_from_dcb(rses, backend_dcb);

    if (backend->get_reply_state() == REPLY_STATE_DONE)
    {
        /** If we receive an unexpected response from the server, the internal
         * logic cannot handle this situation. Routing the reply straight to
         * the client should be the safest thing to do at this point. */
        log_unexpected_response(backend_dcb, writebuf);
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
        return;
    }

    /** Statement was successfully executed, free the stored statement */
    session_clear_stmt(backend_dcb->session);

    if (reply_is_complete(backend, writebuf))
    {
        /** Got a complete reply, acknowledge the write and decrement expected response count */
        backend->ack_write();
        rses->expected_responses--;
        ss_dassert(rses->expected_responses >= 0);
        ss_dassert(backend->get_reply_state() == REPLY_STATE_DONE);
    }
    else
    {
        MXS_DEBUG("Reply not yet complete, waiting for %d replies", rses->expected_responses);
    }

    /**
     * Active cursor means that reply is from session command
     * execution.
     */
    if (backend->session_command_count())
    {
        check_session_command_reply(writebuf, backend);

        /** This discards all responses that have already been sent to the client */
        bool rconn = false;
        process_sescmd_response(rses, backend, &writebuf, &rconn);

        if (rconn && !rses->router->config().disable_sescmd_history)
        {
            select_connect_backend_servers(
                rses->rses_nbackends,
                rses->rses_config.max_slave_connections,
                rses->rses_config.slave_selection_criteria,
                rses->client_dcb->session,
                rses->router,
                rses,
                connection_type::SLAVE);
        }
    }

    bool queue_routed = false;

    if (rses->expected_responses == 0)
    {
        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            ss_dassert((*it)->get_reply_state() == REPLY_STATE_DONE || (*it)->is_closed());
        }

        queue_routed = rses->query_queue != NULL;
        route_stored_query(rses);
    }
    else
    {
        ss_dassert(rses->expected_responses > 0);
    }

    if (writebuf && client_dcb)
    {
        /** Write reply to client DCB */
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    }
    /** Check pending session commands */
    else if (!queue_routed && backend->session_command_count())
    {
        MXS_INFO("Backend %s processed reply and starts to execute active cursor.",
                 backend->uri());

        if (backend->execute_session_command())
        {
            rses->expected_responses++;
        }
    }
}


/**
 * @brief Get router capabilities
 */
static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_STMT_OUTPUT;
}

/**
 * @brief Router error handling routine
 *
 * Error Handler routine to resolve backend failures. If it succeeds then
 * there are enough operative backends available and connected. Otherwise it
 * fails, and session is terminated.
 *
 * @param instance       The router instance
 * @param router_session The router session
 * @param errmsgbuf      The error message to reply
 * @param backend_dcb    The backend DCB
 * @param action         The action: ERRACT_NEW_CONNECTION or
 *                       ERRACT_REPLY_CLIENT
 * @param succp          Result of action: true if router can continue
 */
static void handleError(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *errmsgbuf,
                        DCB *problem_dcb,
                        mxs_error_action_t action,
                        bool *succp)
{
    ss_dassert(problem_dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    RWSplit *inst = (RWSplit *)instance;
    RWSplitSession *rses = (RWSplitSession *)router_session;
    CHK_CLIENT_RSES(rses);
    CHK_DCB(problem_dcb);

    if (rses->rses_closed)
    {
        *succp = false;
        return;
    }

    MXS_SESSION *session = problem_dcb->session;
    ss_dassert(session);

    SRWBackend backend = get_backend_from_dcb(rses, problem_dcb);

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        {
            if (rses->current_master && rses->current_master->dcb() == problem_dcb)
            {
                /** The connection to the master has failed */
                SERVER *srv = rses->current_master->server();
                bool can_continue = false;

                if (rses->rses_config.master_failure_mode != RW_FAIL_INSTANTLY &&
                    (!backend || !backend->is_waiting_result()))
                {
                    /** The failure of a master is not considered a critical
                     * failure as partial functionality still remains. Reads
                     * are allowed as long as slave servers are available
                     * and writes will cause an error to be returned.
                     *
                     * If we were waiting for a response from the master, we
                     * can't be sure whether it was executed or not. In this
                     * case the safest thing to do is to close the client
                     * connection. */
                    can_continue = true;
                }
                else if (!SERVER_IS_MASTER(srv) && !srv->master_err_is_logged)
                {
                    MXS_ERROR("Server %s:%d lost the master status. Readwritesplit "
                              "service can't locate the master. Client sessions "
                              "will be closed.", srv->name, srv->port);
                    srv->master_err_is_logged = true;
                }

                *succp = can_continue;

                if (backend)
                {
                    backend->close(mxs::Backend::CLOSE_FATAL);
                }
                else
                {
                    MXS_ERROR("Server %s:%d lost the master status but could not locate the "
                              "corresponding backend ref.", srv->name, srv->port);
                }
            }
            else if (backend)
            {
                if (rses->target_node &&
                    (rses->target_node->dcb() == problem_dcb &&
                     session_trx_is_read_only(problem_dcb->session)))
                {
                    /** The problem DCB is the current target of a READ ONLY transaction.
                     * Reset the target and close the session. */
                    rses->target_node.reset();
                    *succp = false;
                }
                else
                {
                    /** Try to replace the failed connection with a new one */
                    *succp = handle_error_new_connection(inst, &rses, problem_dcb, errmsgbuf);
                }
            }

            check_and_log_backend_state(backend, problem_dcb);
            break;
        }

    case ERRACT_REPLY_CLIENT:
        {
            handle_error_reply_client(session, rses, problem_dcb, errmsgbuf);
            *succp = false; /*< no new backend servers were made available */
            break;
        }

    default:
        ss_dassert(!true);
        *succp = false;
        break;
    }
}

MXS_BEGIN_DECLS

/**
 * The module entry point routine. It is this routine that must return
 * the structure that is referred to as the "module object". This is a
 * structure with the set of external entry points for this module.
 */
MXS_MODULE *MXS_CREATE_MODULE()
{
    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostics,
        diagnostics_json,
        clientReply,
        handleError,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER, MXS_MODULE_GA, MXS_ROUTER_VERSION,
        "A Read/Write splitting router for enhancement read scalability",
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_STMT_OUTPUT,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "use_sql_variables_in",
                MXS_MODULE_PARAM_ENUM,
                "all",
                MXS_MODULE_OPT_NONE,
                use_sql_variables_in_values
            },
            {
                "slave_selection_criteria",
                MXS_MODULE_PARAM_ENUM,
                "LEAST_CURRENT_OPERATIONS",
                MXS_MODULE_OPT_NONE,
                slave_selection_criteria_values
            },
            {
                "master_failure_mode",
                MXS_MODULE_PARAM_ENUM,
                "fail_instantly",
                MXS_MODULE_OPT_NONE,
                master_failure_mode_values
            },
            {"max_slave_replication_lag", MXS_MODULE_PARAM_INT, "-1"},
            {"max_slave_connections", MXS_MODULE_PARAM_STRING, MAX_SLAVE_COUNT},
            {"retry_failed_reads", MXS_MODULE_PARAM_BOOL, "true"},
            {"disable_sescmd_history", MXS_MODULE_PARAM_BOOL, "true"},
            {"max_sescmd_history", MXS_MODULE_PARAM_COUNT, "0"},
            {"strict_multi_stmt",  MXS_MODULE_PARAM_BOOL, "true"},
            {"master_accept_reads", MXS_MODULE_PARAM_BOOL, "false"},
            {"connection_keepalive", MXS_MODULE_PARAM_COUNT, "0"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}

MXS_END_DECLS
