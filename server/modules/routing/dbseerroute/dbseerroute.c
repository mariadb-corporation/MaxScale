/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file dbseerroute.c - DBSeer router
 *
 * This is the implementation of a simple query router that balances
 * read connections + produces performance logs for DBSeer.
 * It assumes the service is configured with a set
 * of slaves and that the application clients already split read and write
 * queries. It offers a service to balance the client read connections
 * over this set of slave servers. It does this once only, at the time
 * the connection is made. It chooses the server that currently has the least
 * number of connections by keeping a count for each server of how
 * many connections the query router has made to the server.
 *
 * When two servers have the same number of current connections the one with
 * the least number of connections since startup will be used.
 *
 * The router may also have options associated to it that will limit the
 * choice of backend server. Currently two options are supported, the "master"
 * option will cause the router to only connect to servers marked as masters
 * and the "slave" option will limit connections to routers that are marked
 * as slaves. If neither option is specified the router will connect to either
 * masters or slaves.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 01/12/2016   Dong Young Yoon         Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <maxscale/alloc.h>
#include <maxscale/service.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/atomic.h>
#include <maxscale/spinlock.h>
#include "readconnection.h"
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/modutil.h>

/*
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <readconnection.h>
#include <mysql_client_server_protocol.h>
#include "modutil.h"
*/

MODULE_INFO info =
{
    MODULE_API_ROUTER,
    MODULE_GA,
    ROUTER_VERSION,
    "A connection based router to load balance based on connections + transaction performance logging"
};

static char *version_str = "V1.0.0";
static int buf_size = 10;
static int sql_size_limit = 64 * 1024 * 1024;

/* The router entry points */
static ROUTER *createInstance(SERVICE *service, char **options);
static void *newSession(ROUTER *instance, SESSION *session);
static void closeSession(ROUTER *instance, void *router_session);
static void freeSession(ROUTER *instance, void *router_session);
static int routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static void diagnostics(ROUTER *instance, DCB *dcb);
static void clientReply(ROUTER *instance, void *router_session, GWBUF *queue,
                        DCB *backend_dcb);
static void handleError(ROUTER *instance, void *router_session, GWBUF *errbuf,
                        DCB *problem_dcb, error_action_t action, bool *succp);
static uint64_t getCapabilities();
static void *checkNamedPipe(void *args);


/** The module object definition */
static ROUTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostics,
    clientReply,
    handleError,
    getCapabilities,
    NULL
};

static bool rses_begin_locked_router_action(ROUTER_CLIENT_SES* rses);

static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses);

static SERVER_REF *get_root_master(SERVER_REF *servers);
static int handle_state_switch(DCB* dcb, DCB_REASON reason, void * routersession);
static SPINLOCK instlock;
static ROUTER_INSTANCE *instances;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
    MXS_NOTICE("Initialise performancelogroute router module %s.", version_str);
    spinlock_init(&instlock);
    instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
    return &MyObject;
}

static inline void free_readconn_instance(ROUTER_INSTANCE *router)
{
    if (router)
    {
        MXS_FREE(router);
    }
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 *
 * @param service	The service this router is being create for
 * @param options	An array of options for this query router
 *
 * @return The instance data for this new instance
 */
static ROUTER *
createInstance(SERVICE *service, char **options)
{
    char buffer[4096];
    ROUTER_INSTANCE *inst;
    SERVER *server;
    SERVER_REF *sref;
    int i, n, ret;
    char *weightby;
    char *log_filename;
    char *log_delimiter;
    char *query_delimiter;
    char *named_pipe;
    int query_delimiter_size;

    if ((inst = MXS_CALLOC(1, sizeof(ROUTER_INSTANCE))) == NULL)
    {
        return NULL;
    }

    inst->service = service;
    spinlock_init(&inst->lock);

    /*
     * Process the options
     */
    bool error = false;
    inst->bitmask = 0;
    inst->bitvalue = 0;
    if (options)
    {
        for (i = 0; options[i]; i++)
        {
            if (!strcasecmp(options[i], "master"))
            {
                inst->bitmask |= (SERVER_MASTER | SERVER_SLAVE);
                inst->bitvalue |= SERVER_MASTER;
            }
            else if (!strcasecmp(options[i], "slave"))
            {
                inst->bitmask |= (SERVER_MASTER | SERVER_SLAVE);
                inst->bitvalue |= SERVER_SLAVE;
            }
            else if (!strcasecmp(options[i], "running"))
            {
                inst->bitmask |= (SERVER_RUNNING);
                inst->bitvalue |= SERVER_RUNNING;
            }
            else if (!strcasecmp(options[i], "synced"))
            {
                inst->bitmask |= (SERVER_JOINED);
                inst->bitvalue |= SERVER_JOINED;
            }
            else if (!strcasecmp(options[i], "ndb"))
            {
                inst->bitmask |= (SERVER_NDB);
                inst->bitvalue |= SERVER_NDB;
            }
            else
            {
                MXS_WARNING("Unsupported router "
                            "option \'%s\' for readconnroute. "
                            "Expected router options are "
                            "[slave|master|synced|ndb]",
                            options[i]);
                error = true;
            }
        }
    }

    if (error)
    {
        free_readconn_instance(inst);
        return NULL;
    }

    if (inst->bitmask == 0 && inst->bitvalue == 0)
    {
        /** No parameters given, use RUNNING as a valid server */
        inst->bitmask |= (SERVER_RUNNING);
        inst->bitvalue |= SERVER_RUNNING;
    }

    /*
     * check named pipe.
     */
    inst->log_enabled = false;
    if ((named_pipe = serviceGetNamedPipe(service)) != NULL)
    {
        inst->named_pipe = strdup(named_pipe);
        // check if the file exists first.
        if (access(inst->named_pipe, F_OK) == 0)
        {
            // if exists, check if it is a named pipe.
            struct stat st;
            ret = stat(inst->named_pipe, &st);

            // check whether the file is named pipe.
            if (ret == -1 && errno != ENOENT)
            {
                MXS_ERROR("stat() failed on named pipe: %s", strerror(errno));
                free(inst);
                return NULL;
            }
            if (ret == 0 && S_ISFIFO(st.st_mode))
            {
                // if it is a named pipe, we delete it and recreate it.
                unlink(inst->named_pipe);
            }
            else
            {
                MXS_ERROR("The file '%s' already exists and it is not a named pipe.", inst->named_pipe);
                free(inst);
                return NULL;
            }
        }

        // now create the named pipe.
        ret = mkfifo(inst->named_pipe, 0660);
        if (ret == -1)
        {
            MXS_ERROR("mkfifo() failed on named pipe: %s", strerror(errno));
            free(inst);
            return NULL;
        }
    }
    else
    {
        MXS_ERROR("You need to specify a named pipe for dbseerroute router.");
        free(inst);
        return NULL;
    }

    /*
     * process logging options.
     */
    if ((log_filename = serviceGetLogFilename(service)) != NULL)
    {
        MXS_FREE(inst->log_filename);
        inst->log_filename = strdup(log_filename);
        /* set default log and query delimiters */
        inst->log_delimiter = strdup(":::");
        inst->query_delimiter = strdup("@@@");
        inst->query_delimiter_size = 3;

        if ((log_delimiter = serviceGetLogDelimiter(service)) != NULL)
        {
            MXS_FREE(inst->log_delimiter);
            inst->log_delimiter = strdup(log_delimiter);
        }
        if ((query_delimiter = serviceGetQueryDelimiter(service)) != NULL)
        {
            MXS_FREE(inst->query_delimiter);
            inst->query_delimiter = strdup(query_delimiter);
            inst->query_delimiter_size = strlen(inst->query_delimiter);
        }

        inst->log_file = fopen(inst->log_filename, "w");
        if (inst->log_file == NULL)
        {
            MXS_ERROR("Failed to open a log file for dbseerroute router.");
            free(inst);
            return NULL;
        }
    }

    /*
     * Launch a thread that checks the named pipe.
     */
    pthread_t tid;
    ret = pthread_create(&tid, NULL, checkNamedPipe, (void*) inst);
    if (ret == -1)
    {
        MXS_ERROR("Couldn't create a thread to check the named pipe: %s", strerror(errno));
        MXS_FREE(inst);
        return NULL;
    }

    /*
     * We have completed the creation of the instance data, so now
     * insert this router instance into the linked list of routers
     * that have been created with this module.
     */
    spinlock_acquire(&instlock);
    inst->next = instances;
    instances = inst;
    spinlock_release(&instlock);

    return(ROUTER *) inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static void *
newSession(ROUTER *instance, SESSION *session)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *client_rses;
    SERVER_REF *candidate = NULL;
    int i;
    SERVER_REF *master_host = NULL;

    MXS_DEBUG("%lu [newSession] new router session with session "
              "%p, and inst %p.",
              pthread_self(),
              session,
              inst);


    client_rses = (ROUTER_CLIENT_SES *) MXS_CALLOC(1, sizeof(ROUTER_CLIENT_SES));

    if (client_rses == NULL)
    {
        return NULL;
    }

#if defined(SS_DEBUG)
    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif
    client_rses->client_dcb = session->client_dcb;

    /**
     * Find the Master host from available servers
     */
    master_host = get_root_master(inst->service->dbref);

    /**
     * Find a backend server to connect to. This is the extent of the
     * load balancing algorithm we need to implement for this simple
     * connection router.
     */

    /*
     * Loop over all the servers and find any that have fewer connections
     * than the candidate server.
     *
     * If a server has less connections than the current candidate we mark this
     * as the new candidate to connect to.
     *
     * If a server has the same number of connections currently as the candidate
     * and has had less connections over time than the candidate it will also
     * become the new candidate. This has the effect of spreading the
     * connections over different servers during periods of very low load.
     */
    for (SERVER_REF *ref = inst->service->dbref; ref; ref = ref->next)
    {
        if (!SERVER_REF_IS_ACTIVE(ref) || SERVER_IN_MAINT(ref->server) || ref->weight == 0)
        {
            continue;
        }
        else
        {
            MXS_DEBUG("%lu [newSession] Examine server in port %d with "
                      "%d connections. Status is %s, "
                      "inst->bitvalue is %d",
                      pthread_self(),
                      ref->server->port,
                      ref->connections,
                      STRSRVSTATUS(ref->server),
                      inst->bitmask);
        }

        /* Check server status bits against bitvalue from router_options */
        if (ref && SERVER_IS_RUNNING(ref->server) &&
            (ref->server->status & inst->bitmask & inst->bitvalue))
        {
            if (master_host)
            {
                if (ref == master_host && (inst->bitvalue & SERVER_SLAVE))
                {
                    /* Skip root master here, as it could also be slave of an external server that
                     * is not in the configuration.  Intermediate masters (Relay Servers) are also
                     * slave and will be selected as Slave(s)
                     */

                    continue;
                }
                if (ref == master_host && (inst->bitvalue & SERVER_MASTER))
                {
                    /* If option is "master" return only the root Master as there could be
                     * intermediate masters (Relay Servers) and they must not be selected.
                     */

                    candidate = master_host;
                    break;
                }
            }
            else
            {
                /* Master_host is NULL, no master server.  If requested router_option is 'master'
                 * candidate wll be NULL.
                 */
                if (inst->bitvalue & SERVER_MASTER)
                {
                    candidate = NULL;
                    break;
                }
            }

            /* If no candidate set, set first running server as our initial candidate server */
            if (candidate == NULL)
            {
                candidate = ref;
            }
            else if (((ref->connections + 1) * 1000) / ref->weight <
                     ((candidate->connections + 1) * 1000) / candidate->weight)
            {
                /* This running server has fewer connections, set it as a new candidate */
                candidate = ref;
            }
            else if (((ref->connections + 1) * 1000) / ref->weight ==
                     ((candidate->connections + 1) * 1000) / candidate->weight &&
                     ref->server->stats.n_connections < candidate->server->stats.n_connections)
            {
                /* This running server has the same number of connections currently as the candidate
                but has had fewer connections over time than candidate, set this server to
                candidate*/
                candidate = ref;
            }
        }
    }

    /* There is no candidate server here!
     * With router_option=slave a master_host could be set, so route traffic there.
     * Otherwise, just clean up and return NULL
     */
    if (!candidate)
    {
        if (master_host)
        {
            candidate = master_host;
        }
        else
        {
            MXS_ERROR("Failed to create new routing session. "
                      "Couldn't find eligible candidate server. Freeing "
                      "allocated resources.");
            free(client_rses);
            return NULL;
        }
    }

    /*
     * Set up for logging if log filename is specified.
     */
    client_rses->max_sql_size = 4 * 1024;
    client_rses->sql = (char*)malloc(client_rses->max_sql_size);
    memset(client_rses->sql, 0x00, client_rses->max_sql_size);
    client_rses->buf = (char*)malloc(buf_size);
    client_rses->sql_index = 0;

    /*
     * We now have the server with the least connections.
     * Bump the connection count for this server
     */
    client_rses->backend = candidate;

    /** Open the backend connection */
    client_rses->backend_dcb = dcb_connect(candidate->server, session,
                                           candidate->server->protocol);

    if (client_rses->backend_dcb == NULL)
    {
        /** The failure is reported in dcb_connect() */
        MXS_FREE(client_rses);
        return NULL;
    }

    atomic_add(&candidate->connections, 1);

    // TODO: Remove this as it is never called
    dcb_add_callback(client_rses->backend_dcb,
                     DCB_REASON_NOT_RESPONDING,
                     &handle_state_switch,
                     client_rses);
    inst->stats.n_sessions++;

    CHK_CLIENT_RSES(client_rses);

    MXS_INFO("Dbseerroute: New session for server %s. Connections : %d",
             candidate->server->unique_name, candidate->connections);

    return(void *) client_rses;
}

/**
 * @node Unlink from backend server, unlink from router's connection list,
 * and free memory of a router client session.
 *
 * Parameters:
 * @param router - <usage>
 *          <description>
 *
 * @param router_cli_ses - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void freeSession(ROUTER* router_instance, void* router_client_ses)
{
    ROUTER_INSTANCE* router = (ROUTER_INSTANCE *) router_instance;
    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *) router_client_ses;

    ss_debug(int prev_val = ) atomic_add(&router_cli_ses->backend->connections, -1);
    ss_dassert(prev_val > 0);

    MXS_FREE(router_cli_ses);
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static void
closeSession(ROUTER *instance, void *router_session)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    DCB* backend_dcb;

    CHK_CLIENT_RSES(router_cli_ses);
    /**
     * Lock router client session for secure read and update.
     */
    if (rses_begin_locked_router_action(router_cli_ses))
    {
        /* decrease server current connection counter */

        backend_dcb = router_cli_ses->backend_dcb;
        router_cli_ses->backend_dcb = NULL;
        router_cli_ses->rses_closed = true;
        /** Unlock */
        rses_end_locked_router_action(router_cli_ses);

        /**
         * Close the backend server connection
         */
        if (backend_dcb != NULL)
        {
            CHK_DCB(backend_dcb);
            dcb_close(backend_dcb);
        }
    }
    if (inst->log_file != NULL)
    {
         fflush(inst->log_file);
    }
}

/** Log routing failure due to closed session */
static void log_closed_session(mysql_server_cmd_t mysql_command, bool is_closed,
                               SERVER_REF *ref)
{
    char msg[MAX_SERVER_NAME_LEN + 200] = ""; // Extra space for message

    if (is_closed)
    {
        sprintf(msg, "Session is closed.");
    }
    else if (SERVER_IS_DOWN(ref->server))
    {
        sprintf(msg, "Server '%s' is down.", ref->server->unique_name);
    }
    else if (!SERVER_REF_IS_ACTIVE(ref))
    {
        sprintf(msg, "Server '%s' was removed from the service.", ref->server->unique_name);
    }

    MXS_ERROR("Failed to route MySQL command %d to backend server. %s",
              mysql_command, msg);
}

/**
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance		The router instance
 * @param router_session	The router session returned from the newSession call
 * @param queue			The queue of data buffers to route
 * @return if succeed 1, otherwise 0
 */
static int
routeQuery(ROUTER *instance, void *router_session, GWBUF *queue)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    int rc = 0;
    DCB* backend_dcb;
    MySQLProtocol *proto = (MySQLProtocol*)router_cli_ses->client_dcb->protocol;
    mysql_server_cmd_t mysql_command = proto->current_command;
    bool rses_is_closed;

    inst->stats.n_queries++;

    /** Dirty read for quick check if router is closed. */
    if (router_cli_ses->rses_closed)
    {
        rses_is_closed = true;
    }
    else
    {
        /**
         * Lock router client session for secure read of DCBs
         */
        rses_is_closed = !(rses_begin_locked_router_action(router_cli_ses));
    }

    if (!rses_is_closed)
    {
        backend_dcb = router_cli_ses->backend_dcb;
        /** unlock */
        rses_end_locked_router_action(router_cli_ses);
    }

    if (rses_is_closed || backend_dcb == NULL ||
        !SERVER_REF_IS_ACTIVE(router_cli_ses->backend) ||
        SERVER_IS_DOWN(router_cli_ses->backend->server))
    {
        log_closed_session(mysql_command, rses_is_closed, router_cli_ses->backend);
        gwbuf_free(queue);
        goto return_rc;

    }

    char *ptr = NULL;
    ptr = modutil_get_SQL(queue);

    /* do performance logging. */
    if (ptr && inst->log_filename)
    {
        router_cli_ses->sql_end = false;
        int query_len = strlen(ptr);
        /* check for commit and rollback */
        if (query_len > 5)
        {
             int query_size = strlen(ptr)+1;
             char *buf = router_cli_ses->buf;
             for (int i = 0; i < query_size && i < buf_size; ++i)
             {
                 buf[i] = tolower(ptr[i]);
             }
             if (strncmp(buf, "commit", 6) == 0)
             {
                  router_cli_ses->sql_end = true;
             }
             else if (strncmp(buf, "rollback", 8) == 0)
             {
                 router_cli_ses->sql_end = true;
                 router_cli_ses->sql_index = 0;
             }
        }

        /* for normal sql statements. */
        if (!router_cli_ses->sql_end)
        {
            /* check and expand buffer size first. */
            int new_sql_size = router_cli_ses->max_sql_size;
            int len = router_cli_ses->sql_index + query_len + inst->query_delimiter_size + 1;

            /* if the total length of query statements exceeds the maximum limit of sql size, print an error and return.*/
            if (len > sql_size_limit)
            {
                 MXS_ERROR("The size fof query statements exceeds the maximum sql size of 64MB for logging.");
                 goto return_rc;
            }
            /* double buffer size until the buffer fits the query.*/
            while (len > new_sql_size)
            {
                new_sql_size *= 2;
            }
            if (new_sql_size > router_cli_ses->max_sql_size)
            {
                char* new_sql = (char*)malloc(new_sql_size);
                if (new_sql == NULL)
                {
                    MXS_ERROR("Memory allocation failure.");
                    goto return_rc;
                }
                memcpy(new_sql, router_cli_ses->sql, router_cli_ses->sql_index);
                free(router_cli_ses->sql);
                router_cli_ses->sql = new_sql;
                router_cli_ses->max_sql_size = new_sql_size;
            }

            /* if first sql statement.*/
            if (router_cli_ses->sql_index == 0)
            {
                memcpy(router_cli_ses->sql, ptr, query_len);
                router_cli_ses->sql_index += query_len;
                gettimeofday(&router_cli_ses->current_start, NULL);
            }
            else
            {
                memcpy(router_cli_ses->sql + router_cli_ses->sql_index, inst->query_delimiter, inst->query_delimiter_size);
                memcpy(router_cli_ses->sql + router_cli_ses->sql_index + inst->query_delimiter_size, ptr, query_len);
                router_cli_ses->sql_index += (inst->query_delimiter_size + query_len);
            }
        }
    }

    free(ptr);

    char* trc = NULL;

    switch (mysql_command)
    {
        case MYSQL_COM_CHANGE_USER:
            rc = backend_dcb->func.auth(backend_dcb, NULL, backend_dcb->session,
                                        queue);
            break;
        case MYSQL_COM_QUERY:
            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                trc = modutil_get_SQL(queue);
            }
        default:
            rc = backend_dcb->func.write(backend_dcb, queue);
            break;
    }

    MXS_INFO("Routed [%s] to '%s'%s%s",
             STRPACKETTYPE(mysql_command),
             backend_dcb->server->unique_name,
             trc ? ": " : ".",
             trc ? trc : "");
    free(trc);

return_rc:

    return rc;
}

/**
 * Display router diagnostics
 *
 * @param instance	Instance of the router
 * @param dcb		DCB to send diagnostics to
 */
static void
diagnostics(ROUTER *router, DCB *dcb)
{
    ROUTER_INSTANCE *router_inst = (ROUTER_INSTANCE *) router;
    char *weightby;

    dcb_printf(dcb, "\tNumber of router sessions:   	%d\n",
               router_inst->stats.n_sessions);
    dcb_printf(dcb, "\tCurrent no. of router sessions:	%d\n",
               router_inst->service->stats.n_current);
    dcb_printf(dcb, "\tNumber of queries forwarded:   	%d\n",
               router_inst->stats.n_queries);
    if ((weightby = serviceGetWeightingParameter(router_inst->service))
        != NULL)
    {
        dcb_printf(dcb, "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb,
                   "\t\tServer               Target %% Connections\n");
        for (SERVER_REF *ref = router_inst->service->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb, "\t\t%-20s %3.1f%%     %d\n",
                       ref->server->unique_name,
                       (float) ref->weight / 10,
                       ref->connections);
        }
    }
}

/**
 * Client Reply routine
 *
 * The routine will reply to client data from backend server
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       backend_dcb     The backend DCB
 * @param       queue           The GWBUF with reply data
 */
static void
clientReply(ROUTER *instance, void *router_session, GWBUF *queue, DCB *backend_dcb)
{
    /* perform logging if log file has been specified.*/
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    if (inst->log_filename)
    {
        struct timeval tv, diff;
        int i;

        /* found 'commit' and sql statements exists.*/
        if (router_cli_ses->sql_end && router_cli_ses->sql_index > 0)
        {
            gettimeofday(&tv, NULL);
            timersub(&tv, &(router_cli_ses->current_start), &diff);

            /* get latency.*/
            uint64_t millis = (diff.tv_sec * (uint64_t)1000 + diff.tv_usec / 1000);
            /* get timestamp.*/
            uint64_t timestamp = (tv.tv_sec + (tv.tv_usec / (1000*1000)));
            *(router_cli_ses->sql + router_cli_ses->sql_index) = '\0';

            char *delimiter = inst->log_delimiter;
            char* server_uniquename = router_cli_ses->backend->server->unique_name;
            char* server_hostname = router_cli_ses->backend->server->name;
            char *user = backend_dcb->user;

            /* log strucure:
             * timestamp | backend_server_unique_name | backend_server_hostname | latency | sql
             */
            if (inst->log_enabled)
            {
                fprintf(inst->log_file, "%ld%s%s%s%s%s%ld%s%s\n",
                        timestamp,
                        delimiter,
                        server_uniquename,
                        delimiter,
                        server_hostname,
                        delimiter,
                        millis,
                        delimiter,
                        router_cli_ses->sql
                       );
            }
            router_cli_ses->sql_index = 0;
        }
    }

    ss_dassert(backend_dcb->session->client_dcb != NULL);
    SESSION_ROUTE_REPLY(backend_dcb->session, queue);
}

/**
 * Error Handler routine
 *
 * The routine will handle errors that occurred in writes.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       problem_dcb     The DCB related to the error
 * @param       action     	The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param	succp		Result of action: true if router can continue
 *
 */
static void handleError(ROUTER *instance, void *router_session, GWBUF *errbuf,
                        DCB *problem_dcb, error_action_t action, bool *succp)

{
    DCB *client_dcb;
    SESSION *session = problem_dcb->session;
    session_state_t sesstate;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;

    /** Don't handle same error twice on same DCB */
    if (problem_dcb->dcb_errhandle_called)
    {
        /** we optimistically assume that previous call succeed */
        *succp = true;
        return;
    }
    else
    {
        problem_dcb->dcb_errhandle_called = true;
    }
    spinlock_acquire(&session->ses_lock);
    sesstate = session->state;
    client_dcb = session->client_dcb;

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        CHK_DCB(client_dcb);
        spinlock_release(&session->ses_lock);
        client_dcb->func.write(client_dcb, gwbuf_clone(errbuf));
    }
    else
    {
        spinlock_release(&session->ses_lock);
    }

    if (DCB_ROLE_CLIENT_HANDLER == problem_dcb->dcb_role)
    {
        dcb_close(problem_dcb);
    }
    else if (router_cli_ses && problem_dcb == router_cli_ses->backend_dcb)
    {
        router_cli_ses->backend_dcb = NULL;
        dcb_close(problem_dcb);
    }

    /** false because connection is not available anymore */
    *succp = false;
}

/** to be inline'd */

/**
 * @node Acquires lock to router client session if it is not closed.
 *
 * Parameters:
 * @param rses - in, use
 *
 *
 * @return true if router session was not closed. If return value is true
 * it means that router is locked, and must be unlocked later. False, if
 * router was closed before lock was acquired.
 *
 *
 * @details (write detailed description here)
 *
 */
static bool rses_begin_locked_router_action(ROUTER_CLIENT_SES* rses)
{
    bool succp = false;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        goto return_succp;
    }
    spinlock_acquire(&rses->rses_lock);
    if (rses->rses_closed)
    {
        spinlock_release(&rses->rses_lock);
        goto return_succp;
    }
    succp = true;

return_succp:
    return succp;
}

/** to be inline'd */

/**
 * @node Releases router client session lock.
 *
 * Parameters:
 * @param rses - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses)
{
    CHK_CLIENT_RSES(rses);
    spinlock_release(&rses->rses_lock);
}

static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_NONE;
}

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param servers	The list of servers
 * @return		The Master found
 *
 */

static SERVER_REF *get_root_master(SERVER_REF *servers)
{
    int i = 0;
    SERVER_REF *master_host = NULL;

    for (SERVER_REF *ref = servers; ref; ref = ref->next)
    {
        if (ref->active && SERVER_IS_MASTER(ref->server))
        {
            if (master_host == NULL)
            {
                master_host = ref;
            }
            else if (ref->server->depth < master_host->server->depth ||
                    (ref->server->depth == master_host->server->depth &&
                     ref->weight > master_host->weight))
            {
                /**
                 * This master has a lower depth than the candidate master or
                 * the depths are equal but this master has a higher weight
                 */
                master_host = ref;
            }
        }
    }
    return master_host;
}

static int handle_state_switch(DCB* dcb, DCB_REASON reason, void * routersession)
{
    ss_dassert(dcb != NULL);
    SESSION* session = dcb->session;
    ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES*) routersession;
    SERVICE* service = session->service;
    ROUTER* router = (ROUTER *) service->router;

    if (NULL == dcb->session->router_session && DCB_REASON_ERROR != reason)
    {
        /*
         * We cannot handle a DCB that does not have a router session,
         * except in the case where error processing is invoked.
         */
        return 0;
    }
    switch (reason)
    {
        case DCB_REASON_CLOSE:
            dcb->func.close(dcb);
            break;
        case DCB_REASON_DRAINED:
            /** Do we need to do anything? */
            break;
        case DCB_REASON_HIGH_WATER:
            /** Do we need to do anything? */
            break;
        case DCB_REASON_LOW_WATER:
            /** Do we need to do anything? */
            break;
        case DCB_REASON_ERROR:
            dcb->func.error(dcb);
            break;
        case DCB_REASON_HUP:
            dcb->func.hangup(dcb);
            break;
        case DCB_REASON_NOT_RESPONDING:
            dcb->func.hangup(dcb);
            break;
        default:
            break;
    }

    return 0;
}

static void* checkNamedPipe(void *args)
{
    int ret;
    char buffer[2];
    char buf[4096];
    ROUTER_INSTANCE* inst = (ROUTER_INSTANCE*) args;
    char* named_pipe = inst->named_pipe;

    // open named pipe and this will block until middleware opens it.
    while ((inst->named_pipe_fd = open(named_pipe, O_RDONLY)) > 0)
    {
        // 1 for start logging, 0 for stopping.
        while ((ret = read(inst->named_pipe_fd, buffer, 1)) > 0)
        {
            if (buffer[0] == '1')
            {
                inst->log_enabled = true;
            }
            else if (buffer[0] == '0')
            {
                inst->log_enabled = false;
            }
        }
        if (ret == 0)
        {
            close(inst->named_pipe_fd);
        }
    }
    if (inst->named_pipe_fd == -1)
    {
        MXS_ERROR("Failed to open the named pipe '%s': %s", named_pipe, strerror(errno));
        return NULL;
    }

    return NULL;
}
