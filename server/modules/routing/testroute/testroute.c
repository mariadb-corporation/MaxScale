/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
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

static  ROUTER *createInstance(SERVICE *service, char **options);
static  void   *newSession(ROUTER *instance, SESSION *session);
static  void   closeSession(ROUTER *instance, void *session);
static  void   freeSession(ROUTER *instance, void *session);
static  int    routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static  void   clientReply(ROUTER *instance, void *session, GWBUF *queue, DCB*);
static  void   diagnostic(ROUTER *instance, DCB *dcb);
static  uint64_t getCapabilities ();
static void    handleError(ROUTER           *instance,
                           void             *router_session,
                           GWBUF            *errbuf,
                           DCB              *backend_dcb,
                           error_action_t   action,
                           bool             *succp);

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
MODULE_INFO* GetModuleObject()
{
    static ROUTER_OBJECT MyObject =
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

    static MODULE_INFO info =
    {
        MODULE_API_ROUTER,
        MODULE_IN_DEVELOPMENT,
        ROUTER_VERSION,
        "A test router - not for use in real systems",
        "V1.0.0",
        &MyObject
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
static  ROUTER  *
createInstance(SERVICE *service, char **options)
{
    return (ROUTER*)MXS_MALLOC(sizeof(TESTROUTER));
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance  The router instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static  void    *
newSession(ROUTER *instance, SESSION *session)
{
    return (SESSION*)MXS_MALLOC(sizeof(TESTSESSION));
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance  The router instance data
 * @param session   The session being closed
 */
static  void
closeSession(ROUTER *instance, void *session)
{
}

static void freeSession(
    ROUTER* router_instance,
    void*   router_client_session)
{
    MXS_FREE(router_client_session);
}

static  int
routeQuery(ROUTER *instance, void *session, GWBUF *queue)
{
    return 0;
}

void clientReply(ROUTER* instance, void* session, GWBUF* queue, DCB* dcb)
{
}

/**
 * Diagnostics routine
 *
 * @param   instance    The router instance
 * @param   dcb     The DCB for diagnostic output
 */
static  void
diagnostic(ROUTER *instance, DCB *dcb)
{
}

static uint64_t getCapabilities(void)
{
    return 0;
}


static void handleError(
    ROUTER           *instance,
    void             *router_session,
    GWBUF            *errbuf,
    DCB              *backend_dcb,
    error_action_t   action,
    bool             *succp)
{
}
