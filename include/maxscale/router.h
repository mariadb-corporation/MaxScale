#pragma once
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

/**
 * @file router.h - The query router public interface definition
 */

#include <maxscale/cdefs.h>

#include <stdint.h>

#include <maxscale/buffer.h>
#include <maxscale/routing.h>
#include <maxscale/service.h>
#include <maxscale/session.h>

MXS_BEGIN_DECLS

/**
 * MXS_ROUTER is an opaque type representing a particular router instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c createInstance function of a router module and subsequently
 * passing it back to the API functions of the router.
 */
typedef void *MXS_ROUTER;

typedef enum error_action
{
    ERRACT_NEW_CONNECTION = 0x001,
    ERRACT_REPLY_CLIENT   = 0x002
} mxs_error_action_t;

/**
 * @verbatim
 * The "module object" structure for a query router module
 *
 * The entry points are:
 *  createInstance  Called by the service to create a new instance of the query router
 *  newSession      Called to create a new user session within the query router
 *  closeSession    Called when a session is closed
 *  freeSession     Called when a session is freed
 *  routeQuery      Called on each query that requires routing
 *  diagnostics     Called to force the router to print diagnostic output
 *  clientReply     Called to reply to client the data from one or all backends
 *  handleError     Called to reply to client errors with optional closeSession
 *                  or make a request for a new backend connection
 *  getCapabilities Called to obtain the capabilities of the router
 *  destroyInstance Called for destroying a router instance
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct mxs_router_object
{
    MXS_ROUTER *(*createInstance)(SERVICE *service, char **options);
    void    *(*newSession)(MXS_ROUTER *instance, MXS_SESSION *session);
    void     (*closeSession)(MXS_ROUTER *instance, void *router_session);
    void     (*freeSession)(MXS_ROUTER *instance, void *router_session);
    int32_t  (*routeQuery)(MXS_ROUTER *instance, void *router_session, GWBUF *queue);
    void     (*diagnostics)(MXS_ROUTER *instance, DCB *dcb);
    void     (*clientReply)(MXS_ROUTER* instance, void* router_session, GWBUF* queue,
                            DCB *backend_dcb);
    void     (*handleError)(MXS_ROUTER*    instance,
                            void*          router_session,
                            GWBUF*         errmsgbuf,
                            DCB*           backend_dcb,
                            mxs_error_action_t action,
                            bool*          succp);
    uint64_t (*getCapabilities)(void);
    void     (*destroyInstance)(MXS_ROUTER *instance);
} MXS_ROUTER_OBJECT;

/**
 * The router module API version. Any change that changes the router API
 * must update these versions numbers in accordance with the rules in
 * modinfo.h.
 */
#define MXS_ROUTER_VERSION  { 2, 0, 0 }

/**
 * Specifies capabilities specific for routers. Common capabilities
 * are defined by @c routing_capability_t.
 *
 * @see routing_capability_t
 *
 * @note The values of the capabilities here *must* be between 0x00010000
 *       and 0x80000000, that is, bits 16 to 31.
 */
typedef enum router_capability
{
    RCAP_TYPE_NO_RSESSION   = 0x00010000, /**< Router does not use router sessions */
    RCAP_TYPE_NO_USERS_INIT = 0x00020000, /**< Prevent the loading of authenticator
                                             users when the service is started */
} mxs_router_capability_t;

typedef enum
{
    TYPE_UNDEFINED = 0,
    TYPE_MASTER,
    TYPE_ALL
} mxs_target_t;

MXS_END_DECLS
