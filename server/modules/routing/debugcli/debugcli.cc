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
 * @file debugcli.c - A "routing module" that in fact merely gives
 * access to debug commands within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date     Who             Description
 * 18/06/13 Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "debugcli"

#include <debugcli.hh>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxbase/atomic.h>
#include <maxscale/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/modinfo.h>
#include <maxscale/poll.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>

/* The router entry points */
static MXS_ROUTER*         createInstance(SERVICE* service, MXS_CONFIG_PARAMETER* params);
static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);
static void                freeSession(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);
static int                 execute(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session, GWBUF* queue);
static void                diagnostics(MXS_ROUTER* instance, DCB* dcb);
static json_t*             diagnostics_json(const MXS_ROUTER* instance);
static uint64_t            getCapabilities(MXS_ROUTER* instance);

extern int execute_cmd(CLI_SESSION* cli);

static pthread_mutex_t instlock;
static CLI_INSTANCE* instances;

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
    MXS_WARNING("THE 'debugcli' MODULE IS DEPRECATED");
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
        NULL,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "The debug user interface",
        "V1.1.1",
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
    CLI_INSTANCE* inst;

    if ((inst = static_cast<CLI_INSTANCE*>(MXS_MALLOC(sizeof(CLI_INSTANCE)))) == NULL)
    {
        return NULL;
    }

    inst->service = service;
    pthread_mutex_init(&inst->lock, NULL);
    inst->sessions = NULL;

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
    CLI_INSTANCE* inst = (CLI_INSTANCE*)instance;
    CLI_SESSION* client;

    if ((client = (CLI_SESSION*)MXS_MALLOC(sizeof(CLI_SESSION))) == NULL)
    {
        return NULL;
    }
    client->session = session;

    memset(client->cmdbuf, 0, 80);

    pthread_mutex_lock(&inst->lock);
    client->next = inst->sessions;
    inst->sessions = client;
    pthread_mutex_unlock(&inst->lock);

    session->state = SESSION_STATE_READY;

    dcb_printf(session->client_dcb, "Welcome to the MariaDB Corporation MaxScale Debug Interface.\n");
    dcb_printf(session->client_dcb, "Type help for a list of available commands.\n\n");

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
    CLI_INSTANCE* inst = (CLI_INSTANCE*)instance;
    CLI_SESSION* session = (CLI_SESSION*)router_session;


    pthread_mutex_lock(&inst->lock);
    if (inst->sessions == session)
    {
        inst->sessions = session->next;
    }
    else
    {
        CLI_SESSION* ptr = inst->sessions;
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
 * Free a debugcli session
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
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance      The router instance
 * @param router_session    The router session returned from the newSession call
 * @param queue         The queue of data buffers to route
 * @return The number of bytes sent
 */
static int execute(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session, GWBUF* queue)
{
    CLI_SESSION* session = (CLI_SESSION*)router_session;


    char* cmdbuf = session->cmdbuf;
    int cmdlen = 0;

    *cmdbuf = 0;

    /* Extract the characters */
    while (queue && (cmdlen < CMDBUFLEN - 1))
    {
        const char* data = (char*)GWBUF_DATA(queue);
        int len = GWBUF_LENGTH(queue);
        int n = MXS_MIN(len, CMDBUFLEN - cmdlen - 1);

        if (n != len)
        {
            MXS_WARNING("Too long user command truncated.");
        }

        strncat(cmdbuf, data, n);

        cmdlen += n;
        cmdbuf += n;

        queue = gwbuf_consume(queue, GWBUF_LENGTH(queue));
    }

    if (strrchr(session->cmdbuf, '\n'))
    {
        if (execute_cmd(session))
        {
            dcb_printf(session->session->client_dcb, "MaxScale> ");
        }
        else
        {
            dcb_close(session->session->client_dcb);
        }
    }
    return 1;
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

static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_NONE;
}
