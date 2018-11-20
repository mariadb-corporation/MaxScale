/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxinfo.c - A "routing module" that in fact merely gives access
 * to a MaxScale information schema usign the MySQL protocol
 *
 * @verbatim
 * Revision History
 *
 * Date       Who                 Description
 * 16/02/15   Mark Riddoch        Initial implementation
 * 27/02/15   Massimiliano Pinto  Added maxinfo_add_mysql_user
 * 09/09/2015 Martin Brampton     Modify error handler
 *
 * @endverbatim
 */

#include "maxinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <maxscale/alloc.h>
#include <maxscale/service.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/monitor.h>
#include <maxbase/atomic.h>
#include <maxscale/dcb.h>
#include <maxscale/maxscale.h>
#include <maxscale/log.h>
#include <maxscale/resultset.hh>
#include <maxscale/version.h>
#include <maxscale/resultset.hh>
#include <maxscale/secrets.h>
#include <maxscale/users.h>
#include <maxscale/protocol/mysql.h>

#include "../../../core/internal/modules.h"
#include "../../../core/internal/monitor.h"
#include "../../../core/internal/session.h"
#include "../../../core/internal/session.hh"
#include "../../../core/internal/poll.hh"

extern char* create_hex_sha1_sha1_passwd(char* passwd);

static int maxinfo_statistics(INFO_INSTANCE*, INFO_SESSION*, GWBUF*);
static int maxinfo_ping(INFO_INSTANCE*, INFO_SESSION*, GWBUF*);
static int maxinfo_execute_query(INFO_INSTANCE*, INFO_SESSION*, char*);
static int maxinfo_send_ok(DCB* dcb);

/* The router entry points */
static MXS_ROUTER*         createInstance(SERVICE* service, MXS_CONFIG_PARAMETER* params);
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);
static void                freeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);
static int                 execute(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session, GWBUF* queue);
static void                diagnostics(MXS_ROUTER* instance, DCB* dcb);
static json_t*             diagnostics_json(const MXS_ROUTER* instance);
static uint64_t            getCapabilities(MXS_ROUTER* instance);
static void                handleError(MXS_ROUTER* instance,
                                       MXS_ROUTER_SESSION* router_session,
                                       GWBUF* errbuf,
                                       DCB*   backend_dcb,
                                       mxs_error_action_t action,
                                       bool* succp);

static pthread_mutex_t instlock;
static INFO_INSTANCE* instances;

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_WARNING("THE 'maxinfo' MODULE IS DEPRECATED");
    pthread_mutex_init(&instlock, NULL);
    instances = NULL;

    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        execute,
        diagnostics,
        diagnostics_json,
        NULL,
        handleError,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        "The MaxScale Information Schema",
        "V1.0.0",
        RCAP_TYPE_NO_AUTH,
        &MyObject,
        NULL,   /* Process init. */
        NULL,   /* Process finish. */
        NULL,   /* Thread init. */
        NULL,   /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 *
 * @param service   The service this router is being create for
 * @param options   Any array of options for the query router
 *
 * @return The instance data for this new instance
 */
static MXS_ROUTER* createInstance(SERVICE* service, MXS_CONFIG_PARAMETER* params)
{
    INFO_INSTANCE* inst;
    int i;

    if ((inst = static_cast<INFO_INSTANCE*>(MXS_MALLOC(sizeof(INFO_INSTANCE)))) == NULL)
    {
        return NULL;
    }

    inst->sessions = NULL;
    inst->service = service;
    pthread_mutex_init(&inst->lock, NULL);

    /*
     * We have completed the creation of the instance data, so now
     * insert this router instance into the linked list of routers
     * that have been created with this module.
     */
    pthread_mutex_lock(&instlock);
    inst->next = instances;
    instances = inst;
    pthread_mutex_unlock(&instlock);

    return (MXS_ROUTER*)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance  The router instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* instance, MXS_SESSION* session)
{
    INFO_INSTANCE* inst = (INFO_INSTANCE*)instance;
    INFO_SESSION* client;

    if ((client = (INFO_SESSION*)MXS_MALLOC(sizeof(INFO_SESSION))) == NULL)
    {
        return NULL;
    }
    client->session = session;
    client->dcb = session->client_dcb;
    client->queue = NULL;

    pthread_mutex_lock(&inst->lock);
    client->next = inst->sessions;
    inst->sessions = client;
    pthread_mutex_unlock(&inst->lock);

    session->state = SESSION_STATE_READY;

    return reinterpret_cast<MXS_ROUTER_SESSION*>(client);
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance      The router instance data
 * @param router_session    The session being closed
 */
static void closeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session)
{
    INFO_INSTANCE* inst = (INFO_INSTANCE*)instance;
    INFO_SESSION* session = (INFO_SESSION*)router_session;


    pthread_mutex_lock(&inst->lock);
    if (inst->sessions == session)
    {
        inst->sessions = session->next;
    }
    else
    {
        INFO_SESSION* ptr = inst->sessions;
        while (ptr && ptr->next != session)
        {
            ptr = ptr->next;
        }
        if (ptr)
        {
            ptr->next = session->next;
        }
    }
    pthread_mutex_unlock(&inst->lock);
    /**
     * Router session is freed in session.c:session_close, when session who
     * owns it, is freed.
     */
}

/**
 * Free a maxinfo session
 *
 * @param router_instance   The router session
 * @param router_client_session The router session as returned from newSession
 */
static void freeSession(MXS_ROUTER* router_instance,
                        MXS_ROUTER_SESSION* router_client_session)
{
    MXS_FREE(router_client_session);
    return;
}

/**
 * Error Handler routine
 *
 * The routine will handle errors that occurred in backend writes.
 *
 * @param instance        The router instance
 * @param router_session  The router session
 * @param message         The error message to reply
 * @param backend_dcb     The backend DCB
 * @param action          The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param succp           Result of action: true iff router can continue
 *
 */
static void handleError(MXS_ROUTER* instance,
                        MXS_ROUTER_SESSION* router_session,
                        GWBUF* errbuf,
                        DCB*   backend_dcb,
                        mxs_error_action_t action,
                        bool* succp)

{
    mxb_assert(backend_dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    DCB* client_dcb;
    MXS_SESSION* session = backend_dcb->session;

    client_dcb = session->client_dcb;

    if (session->state == SESSION_STATE_ROUTER_READY)
    {
        client_dcb->func.write(client_dcb, gwbuf_clone(errbuf));
    }

    /** false because connection is not available anymore */
    dcb_close(backend_dcb);
    *succp = false;
}

/**
 * We have data from the client, this is a SQL command, or other MySQL
 * packet type.
 *
 * @param instance       The router instance
 * @param router_session The router session returned from the newSession call
 * @param queue          The queue of data buffers to route
 * @return The number of bytes sent
 */
static int execute(MXS_ROUTER* rinstance, MXS_ROUTER_SESSION* router_session, GWBUF* queue)
{
    INFO_INSTANCE* instance = (INFO_INSTANCE*)rinstance;
    INFO_SESSION* session = (INFO_SESSION*)router_session;
    uint8_t* data;
    int length, len, residual;
    char* sql;

    if (GWBUF_TYPE(queue) == GWBUF_TYPE_HTTP)
    {
        gwbuf_free(queue);
        return 0;
    }
    if (session->queue)
    {
        queue = gwbuf_append(session->queue, queue);
        session->queue = NULL;
        queue = gwbuf_make_contiguous(queue);
    }
    data = (uint8_t*)GWBUF_DATA(queue);
    length = data[0] + (data[1] << 8) + (data[2] << 16);
    if (length + 4 > static_cast<int>(GWBUF_LENGTH(queue)))
    {
        // Incomplete packet, must be buffered
        session->queue = queue;
        return 1;
    }

    int rc = 1;
    // We have a complete request in a single buffer
    if (modutil_MySQL_Query(queue, &sql, &len, &residual))
    {
        sql = strndup(sql, len);
        rc = maxinfo_execute_query(instance, session, sql);
        MXS_FREE(sql);
    }
    else
    {
        switch (MYSQL_COMMAND(queue))
        {
        case MXS_COM_PING:
            rc = maxinfo_send_ok(session->dcb);
            break;

        case MXS_COM_STATISTICS:
            rc = maxinfo_statistics(instance, session, queue);
            break;

        case MXS_COM_QUIT:
            break;

        default:
            MXS_ERROR("Unexpected MySQL command 0x%x",
                      MYSQL_COMMAND(queue));
            break;
        }
    }
    // MaxInfo doesn't route the data forward so it should be freed.
    gwbuf_free(queue);
    return rc;
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 * @param dcb       DCB to send diagnostics to
 */
static void diagnostics(MXS_ROUTER* instance, DCB* dcb)
{
    return;     /* Nothing to do currently */
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 * @param dcb       DCB to send diagnostics to
 */
static json_t* diagnostics_json(const MXS_ROUTER* instance)
{
    return NULL;
}

/**
 * Capabilities interface for the rotuer
 *
 * Not used for the maxinfo router
 */
static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_NONE;
}



/**
 * Return some basic statistics from the router in response to a COM_STATISTICS
 * request.
 *
 * @param router    The router instance
 * @param session   The connection that requested the statistics
 * @param queue     The statistics request
 *
 * @return non-zero on sucessful send
 */
static int maxinfo_statistics(INFO_INSTANCE* router, INFO_SESSION* session, GWBUF* queue)
{
    char result[1000];
    uint8_t* ptr;
    GWBUF* ret;
    int len;

    snprintf(result,
             1000,
             "Uptime: %u  Threads: %u  Sessions: %u ",
             maxscale_uptime(),
             config_threadcount(),
             serviceSessionCountAll());
    if ((ret = gwbuf_alloc(4 + strlen(result))) == NULL)
    {
        return 0;
    }
    len = strlen(result);
    ptr = GWBUF_DATA(ret);
    *ptr++ = len & 0xff;
    *ptr++ = (len & 0xff00) >> 8;
    *ptr++ = (len & 0xff0000) >> 16;
    *ptr++ = 1;
    memcpy(ptr, result, len);

    return session->dcb->func.write(session->dcb, ret);
}

/**
 * Respond to a COM_PING command
 *
 * @param router    The router instance
 * @param session   The connection that requested the ping
 * @param queue     The ping request
 */
static int maxinfo_ping(INFO_INSTANCE* router, INFO_SESSION* session, GWBUF* queue)
{
    uint8_t* ptr;
    GWBUF* ret;
    int len;

    if ((ret = gwbuf_alloc(5)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(ret);
    *ptr++ = 0x01;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 1;
    *ptr = 0;       // OK

    return session->dcb->func.write(session->dcb, ret);
}

/**
 * The hardwired select @@vercom response
 *
 * @param dcb   The DCB of the client
 */
static void respond_vercom(DCB* dcb)
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"@@version_comment"});
    set->add_row({MAXSCALE_VERSION});
    set->write(dcb);
}

/**
 * The hardwired select ... as starttime response
 *
 * @param dcb   The DCB of the client
 */
static void respond_starttime(DCB* dcb)
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"starttime"});
    set->add_row({std::to_string(maxscale_started())});
    set->write(dcb);
}

/**
 * Send a MySQL OK packet to the DCB
 *
 * @param dcb   The DCB to send the OK packet to
 * @return result of a write call, non-zero if write was successful
 */
static int maxinfo_send_ok(DCB* dcb)
{
    GWBUF* buf;
    uint8_t* ptr;

    if ((buf = gwbuf_alloc(11)) == NULL)
    {
        return 0;
    }
    ptr = GWBUF_DATA(buf);
    *ptr++ = 7;     // Payload length
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 1;     // Seqno
    *ptr++ = 0;     // ok
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 2;
    *ptr++ = 0;
    *ptr++ = 0;
    *ptr++ = 0;
    return dcb->func.write(dcb, buf);
}

/**
 * Execute a SQL query against the MaxScale Information Schema
 *
 * @param instance  The instance strcture
 * @param session   The session pointer
 * @param sql       The SQL to execute
 */
static int maxinfo_execute_query(INFO_INSTANCE* instance, INFO_SESSION* session, char* sql)
{
    MAXINFO_TREE* tree;
    PARSE_ERROR err;

    MXS_INFO("SQL statement: '%s' for 0x%p.",
             sql,
             session->dcb);
    if (strcmp(sql, "select @@version_comment limit 1") == 0)
    {
        respond_vercom(session->dcb);
        return 1;
    }
    /* Below is a kludge for MonYog, if we see
     *  select unix_timestamp... as starttime
     * just return the starttime of MaxScale
     */
    if (strncasecmp(sql,
                    "select UNIX_TIMESTAMP",
                    strlen("select UNIX_TIMESTAMP")) == 0
        && (strstr(sql, "as starttime") != NULL || strstr(sql, "AS starttime") != NULL))
    {
        respond_starttime(session->dcb);
        return 1;
    }
    if (strncasecmp(sql, "set names", 9) == 0)
    {
        return maxinfo_send_ok(session->dcb);
    }
    if (strncasecmp(sql, "set session", 11) == 0)
    {
        return maxinfo_send_ok(session->dcb);
    }
    if (strncasecmp(sql, "set @@session", 13) == 0)
    {
        return maxinfo_send_ok(session->dcb);
    }
    if (strncasecmp(sql, "set autocommit", 14) == 0)
    {
        return maxinfo_send_ok(session->dcb);
    }
    if (strncasecmp(sql, "SELECT `ENGINES`.`SUPPORT`", 26) == 0)
    {
        return maxinfo_send_ok(session->dcb);
    }
    if ((tree = maxinfo_parse(sql, &err)) == NULL)
    {
        maxinfo_send_parse_error(session->dcb, sql, err);
        MXS_NOTICE("Failed to parse SQL statement: '%s'.", sql);
    }
    else
    {
        maxinfo_execute(session->dcb, tree);
        maxinfo_free_tree(tree);
    }
    return 1;
}

/**
 * Session all result set
 * @return A resultset for all sessions
 */
static std::unique_ptr<ResultSet> maxinfoSessionsAll()
{
    return sessionGetList();
}

/**
 * Client session result set
 * @return A resultset for all sessions
 */
static std::unique_ptr<ResultSet> maxinfoClientSessions()
{
    return sessionGetList();
}
