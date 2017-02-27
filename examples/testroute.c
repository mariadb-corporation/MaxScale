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
#include <stdio.h>
#include <maxscale/alloc.h>
#include <maxscale/router.h>
#include <maxscale/modinfo.h>

static MXS_ROUTER *createInstance(SERVICE *service, char **options);
static MXS_ROUTER_SESSION *newSession(MXS_ROUTER *instance, MXS_SESSION *session);
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static void freeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue);
static void clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue, DCB*);
static void diagnostic(MXS_ROUTER *instance, DCB *dcb);
static uint64_t getCapabilities(MXS_ROUTER* instance);
static void handleError(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *errbuf,
                        DCB *backend_dcb,
                        mxs_error_action_t action,
                        bool *succp);

typedef struct
{
} TESTROUTER;

typedef struct
{
} TESTSESSION;

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostic,
        clientReply,
        handleError,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_ROUTER_VERSION,
        "A test router - not for use in real systems",
        "V1.0.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
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
 * @param options   The options for this query router
 *
 * @return The instance data for this new instance
 */
static  MXS_ROUTER  *
createInstance(SERVICE *service, char **options)
{
    return (MXS_ROUTER*)MXS_MALLOC(sizeof(TESTROUTER));
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance  The router instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_ROUTER_SESSION *
newSession(MXS_ROUTER *instance, MXS_SESSION *session)
{
    return (MXS_ROUTER_SESSION*)MXS_MALLOC(sizeof(TESTSESSION));
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance  The router instance data
 * @param session   The session being closed
 */
static  void
closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session)
{
}

static void freeSession(MXS_ROUTER* router_instance,
                        MXS_ROUTER_SESSION* router_client_session)
{
    MXS_FREE(router_client_session);
}

static  int
routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue)
{
    return 0;
}

void clientReply(MXS_ROUTER* instance, MXS_ROUTER_SESSION* session, GWBUF* queue, DCB* dcb)
{
}

/**
 * Diagnostics routine
 *
 * @param   instance    The router instance
 * @param   dcb     The DCB for diagnostic output
 */
static  void
diagnostic(MXS_ROUTER *instance, DCB *dcb)
{
}

static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return 0;
}


static void handleError(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *errbuf,
                        DCB *backend_dcb,
                        mxs_error_action_t action,
                        bool *succp)
{
}
