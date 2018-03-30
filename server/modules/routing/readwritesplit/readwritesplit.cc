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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cmath>
#include <new>

#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>
#include <maxscale/router.h>
#include <maxscale/spinlock.h>
#include <maxscale/mysql_utils.h>

#include "rwsplit_internal.hh"
#include "rwsplitsession.hh"
#include "routeinfo.hh"

using namespace maxscale;

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
static bool route_stored_query(RWSplitSession *rses);

/**
 * Internal functions
 */

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

static inline SRWBackend& get_backend_from_dcb(RWSplitSession *rses, DCB *dcb)
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

    /** We should always have a valid backend reference and in case we don't,
     * something is terribly wrong. */
    MXS_ALERT("No reference to DCB %p found, aborting.", dcb);
    raise(SIGABRT);

    // To make the compiler happy, we return a reference to a static value.
    static SRWBackend this_should_not_happen;
    return this_should_not_happen;
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
            else if (strcmp(options[i], "strict_sp_calls") == 0)
            {
                config.strict_sp_calls = config_truth_value(value);
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

    SRWBackend& backend = get_backend_from_dcb(rses, backend_dcb);

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
    SRWBackend& backend = get_backend_from_dcb(myrses, backend_dcb);

    MXS_SESSION* ses = backend_dcb->session;
    bool route_stored = false;
    CHK_SESSION(ses);

    if (backend->is_waiting_result())
    {
        ss_dassert(myrses->expected_responses > 0);
        myrses->expected_responses--;

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

            if (!backend->have_session_commands())
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

    int max_nslaves = inst->max_slave_count();
    bool succp;
    /**
     * Try to get replacement slave or at least the minimum
     * number of slave connections for router session.
     */
    if (myrses->recv_sescmd > 0 && inst->config().disable_sescmd_history)
    {
        succp = inst->have_enough_servers();
    }
    else
    {
        succp = select_connect_backend_servers(inst, ses, myrses->backends,
                                               myrses->current_master, &myrses->sescmd_list,
                                               &myrses->expected_responses,
                                               connection_type::SLAVE);
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

        // TODO: Move the handling of queued queries to the client protocol
        // TODO: module where the command tracking is done automatically.
        uint8_t cmd = mxs_mysql_get_command(query_queue);
        mysql_protocol_set_current_command(rses->client_dcb, (mxs_mysql_cmd_t)cmd);

        if (!routeQuery((MXS_ROUTER*)rses->router, (MXS_ROUTER_SESSION*)rses, query_queue))
        {
            rval = false;
            MXS_ERROR("Failed to route queued query.");
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

void close_all_connections(SRWBackendList& backends)
{
    for (SRWBackendList::iterator it = backends.begin(); it != backends.end(); it++)
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
            MXS_ERROR("Backend '%s' is still in use and points to the problem DCB.",
                      backend->name());
            ss_dassert(false);
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

int RWSplit::max_slave_count() const
{
    int router_nservers = m_service->n_dbref;
    int conf_max_nslaves = m_config.max_slave_connections > 0 ?
                           m_config.max_slave_connections :
                           (router_nservers * m_config.rw_max_slave_conn_percent) / 100;
    return MXS_MIN(router_nservers - 1, MXS_MAX(1, conf_max_nslaves));
}

bool RWSplit::have_enough_servers() const
{
    bool succp = true;
    const int min_nsrv = 1;
    const int router_nsrv = m_service->n_dbref;

    int n_serv = MXS_MAX(m_config.max_slave_connections,
                         (router_nsrv * m_config.rw_max_slave_conn_percent) / 100);

    /** With too few servers session is not created */
    if (router_nsrv < min_nsrv || n_serv < min_nsrv)
    {
        if (router_nsrv < min_nsrv)
        {
            MXS_ERROR("Unable to start %s service. There are "
                      "too few backend servers available. Found %d "
                      "when %d is required.", m_service->name, router_nsrv, min_nsrv);
        }
        else
        {
            int pct = m_config.rw_max_slave_conn_percent / 100;
            int nservers = router_nsrv * pct;

            if (m_config.max_slave_connections < min_nsrv)
            {
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d when %d is required.",
                          m_service->name, m_config.max_slave_connections, min_nsrv);
            }
            if (nservers < min_nsrv)
            {
                double dbgpct = ((double)min_nsrv / (double)router_nsrv) * 100.0;
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d%% when at least %.0f%% "
                          "would be required.", m_service->name,
                          m_config.rw_max_slave_conn_percent, dbgpct);
            }
        }
        succp = false;
    }

    return succp;
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
    RWSplit* router = reinterpret_cast<RWSplit*>(router_inst);
    RWSplitSession* rses = NULL;
    MXS_EXCEPTION_GUARD(rses = RWSplitSession::create(router, session));
    return reinterpret_cast<MXS_ROUTER_SESSION*>(rses);
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
        close_all_connections(router_cli_ses->backends);

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
        if (rses->query_queue == NULL &&
            (rses->expected_responses == 0 ||
             mxs_mysql_get_command(querybuf) == MXS_COM_STMT_FETCH ||
             rses->load_data_state == LOAD_DATA_ACTIVE ||
             rses->large_query))
        {
            /** Gather the information required to make routing decisions */
            RouteInfo info(rses, querybuf);

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
            ss_dassert(rses->expected_responses > 0);

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
    dcb_printf(dcb, "\tstrict_sp_calls:           %s\n",
               router->config().strict_sp_calls ? "true" : "false");
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
    json_object_set_new(rval, "strict_sp_calls",
                        json_boolean(router->config().strict_sp_calls));
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
 * @bref discard the result of wait gtid statment, the result will be an error
 * packet or an error packet.
 * @param buffer origin reply buffer
 * @param proto  MySQLProtocol
 * @return reset buffer
 */
GWBUF *discard_master_wait_gtid_result(GWBUF *buffer, RWSplitSession *rses)
{
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    uint8_t packet_len = 0;
    uint8_t offset = 0;
    mxs_mysql_cmd_t com;

    gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN + 1, header_and_command);
    /* ignore error packet */
    if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_ERR)
    {
        rses->wait_gtid_state = EXPECTING_NOTHING;
        return buffer;
    }

    /* this packet must be an ok packet now */
    ss_dassert(MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_OK);
    packet_len = MYSQL_GET_PAYLOAD_LEN(header_and_command) + MYSQL_HEADER_LEN;
    rses->wait_gtid_state = EXPECTING_REAL_RESULT;
    rses->next_seq = 1;

    return gwbuf_consume(buffer, packet_len);
}

/**
 * @bref After discarded the wait result, we need correct the seqence number of every packet
 *
 * @param buffer origin reply buffer
 * @param proto  MySQLProtocol
 *
 */
void correct_packet_sequence(GWBUF *buffer, RWSplitSession *rses)
{
    uint8_t header[3];
    uint32_t offset = 0;
    uint32_t packet_len = 0;
    if (rses->wait_gtid_state == EXPECTING_REAL_RESULT)
    {
        while (gwbuf_copy_data(buffer, offset, 3, header) == 3)
        {
           packet_len = MYSQL_GET_PAYLOAD_LEN(header) + MYSQL_HEADER_LEN;
           uint8_t *seq = gwbuf_byte_pointer(buffer, offset + MYSQL_SEQ_OFFSET);
           *seq = rses->next_seq;
           rses->next_seq++;
           offset += packet_len;
        }
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
    RWSplit *inst = (RWSplit *)instance;
    DCB *client_dcb = backend_dcb->session->client_dcb;
    CHK_CLIENT_RSES(rses);
    ss_dassert(!rses->rses_closed);

    SRWBackend& backend = get_backend_from_dcb(rses, backend_dcb);

    if (inst->config().enable_causal_read &&
        GWBUF_IS_REPLY_OK(writebuf) &&
        backend == rses->current_master)
    {
        /** Save gtid position */
        char *tmp = gwbuf_get_property(writebuf, (char *)"gtid");
        if (tmp)
        {
            rses->gtid_pos = std::string(tmp);
        }
    }

    if (rses->wait_gtid_state == EXPECTING_WAIT_GTID_RESULT)
    {
        ss_dassert(rses->rses_config.enable_causal_read);
        if ((writebuf = discard_master_wait_gtid_result(writebuf, rses)) == NULL)
        {
            // Nothing to route, return
            return;
        }
    }
    if (rses->wait_gtid_state == EXPECTING_REAL_RESULT)
    {
        correct_packet_sequence(writebuf, rses);
    }

    if (backend->get_reply_state() == REPLY_STATE_DONE)
    {
        /** If we receive an unexpected response from the server, the internal
         * logic cannot handle this situation. Routing the reply straight to
         * the client should be the safest thing to do at this point. */
        log_unexpected_response(backend_dcb, writebuf);
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
        return;
    }

    if (session_have_stmt(backend_dcb->session))
    {
        /** Statement was successfully executed, free the stored statement */
        session_clear_stmt(backend_dcb->session);
    }

    if (backend->reply_is_complete(writebuf))
    {
        /** Got a complete reply, acknowledge the write and decrement expected response count */
        backend->ack_write();
        rses->expected_responses--;
        ss_dassert(rses->expected_responses >= 0);
        ss_dassert(backend->get_reply_state() == REPLY_STATE_DONE);
        MXS_INFO("Reply complete, last reply from %s", backend->name());
    }
    else
    {
        MXS_INFO("Reply not yet complete. Waiting for %d replies, got one from %s",
                 rses->expected_responses, backend->name());
    }

    if (backend->have_session_commands())
    {
        /** Reply to an executed session command */
        process_sescmd_response(rses, backend, &writebuf);
    }

    if (backend->have_session_commands())
    {
        if (backend->execute_session_command())
        {
            rses->expected_responses++;
        }
    }
    else if (rses->expected_responses == 0 && rses->query_queue)
    {
        route_stored_query(rses);
    }

    if (writebuf)
    {
        ss_dassert(client_dcb);
        /** Write reply to client DCB */
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    }
}


/**
 * @brief Get router capabilities
 */
static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING |
        RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_SESSION_STATE_TRACKING;
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

    SRWBackend& backend = get_backend_from_dcb(rses, problem_dcb);

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        {
            if (rses->current_master && rses->current_master->in_use() &&
                rses->current_master == backend)
            {
                /** The connection to the master has failed */
                SERVER *srv = rses->current_master->server();
                bool can_continue = false;

                if (!backend->is_waiting_result())
                {
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
                    if (rses->rses_config.master_failure_mode != RW_FAIL_INSTANTLY)
                    {
                        can_continue = true;
                    }
                }
                else
                {
                    // We were expecting a response but we aren't going to get one
                    rses->expected_responses--;

                    if (rses->rses_config.master_failure_mode == RW_ERROR_ON_WRITE)
                    {
                        /** In error_on_write mode, the session can continue even
                         * if the master is lost. Send a read-only error to
                         * the client to let it know that the query failed. */
                        can_continue = true;
                        send_readonly_error(rses->client_dcb);
                    }

                    if (!SERVER_IS_MASTER(srv) && !srv->master_err_is_logged)
                    {
                        ss_dassert(backend);
                        MXS_ERROR("Server %s (%s) lost the master status while waiting"
                            " for a result. Client sessions will be closed.",
                                  backend->name(), backend->uri());
                        backend->server()->master_err_is_logged = true;
                    }
                }

                if (session_trx_is_active(session))
                {
                    // We have an open transaction, we can't continue
                    can_continue = false;
                }

                *succp = can_continue;
                backend->close();
            }
            else
            {
                if (rses->target_node && rses->target_node == backend &&
                     session_trx_is_read_only(problem_dcb->session))
                {
                    /**
                     * We were locked to a single node but the node died. Currently
                     * this only happens with read-only transactions so the only
                     * thing we can do is to close the connection.
                     */
                    *succp = false;
                    backend->close(mxs::Backend::CLOSE_FATAL);
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
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING |
        RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_SESSION_STATE_TRACKING,
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
            {"disable_sescmd_history", MXS_MODULE_PARAM_BOOL, "false"},
            {"max_sescmd_history", MXS_MODULE_PARAM_COUNT, "50"},
            {"strict_multi_stmt",  MXS_MODULE_PARAM_BOOL, "false"},
            {"strict_sp_calls",  MXS_MODULE_PARAM_BOOL, "false"},
            {"master_accept_reads", MXS_MODULE_PARAM_BOOL, "false"},
            {"connection_keepalive", MXS_MODULE_PARAM_COUNT, "0"},
            {"enable_causal_read", MXS_MODULE_PARAM_BOOL, "false"},
            {"causal_read_timeout", MXS_MODULE_PARAM_STRING, "0"},
            {"master_reconnection", MXS_MODULE_PARAM_BOOL, "false"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}

MXS_END_DECLS
