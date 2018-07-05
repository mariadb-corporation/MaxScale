#pragma once
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
 * @file router.h - The query router public interface definition
 */

#include <maxscale/cdefs.h>

#include <stdint.h>

#include <maxscale/buffer.h>
#include <maxscale/routing.h>
#include <maxscale/service.h>
#include <maxscale/session.h>
#include <maxscale/jansson.h>

MXS_BEGIN_DECLS

/**
 * MXS_ROUTER is an opaque type representing a particular router instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c createInstance function of a router module and subsequently
 * passing it back to the API functions of the router.
 */
typedef struct mxs_router
{
} MXS_ROUTER;

/**
 * MXS_ROUTER_SESSION is an opaque type representing the session related
 * data of a particular router instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c newSession function of a router module and subsequently
 * passing it back to the API functions of the router.
 */
typedef struct mxs_router_session
{
} MXS_ROUTER_SESSION;

typedef enum error_action
{
    ERRACT_NEW_CONNECTION = 0x001,
    ERRACT_REPLY_CLIENT   = 0x002
} mxs_error_action_t;

/**
 * @verbatim
 * The "module object" structure for a query router module. All entry points
 * marked with `(optional)` are optional entry points which can be set to NULL
 * if no implementation is required.
 *
 * The entry points are:
 *  createInstance  Called by the service to create a new instance of the query router
 *  newSession      Called to create a new user session within the query router
 *  closeSession    Called when a session is closed
 *  freeSession     Called when a session is freed
 *  routeQuery      Called on each query that requires routing
 *  diagnostics     Called to force the router to print diagnostic output
 *  clientReply     Called to reply to client the data from one or all backends (optional)
 *  handleError     Called to reply to client errors with optional closeSession
 *                  or make a request for a new backend connection
 *  getCapabilities Called to obtain the capabilities of the router (optional)
 *  destroyInstance Called for destroying a router instance (optional)
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct mxs_router_object
{
    /**
     * @brief Create a new instance of the router
     *
     * This function is called when a new router instance is created. The return
     * value of this function will be passed as the first parameter to the
     * other API functions.
     *
     * @param service The service where the instance is created
     * @param options Router options
     *
     * @return New router instance on NULL on error
     */
    MXS_ROUTER *(*createInstance)(SERVICE *service, char **options);

    /**
     * Called to create a new user session within the router
     *
     * This function is called when a new router session is created for a client.
     * The return value of this function will be passed as the second parameter
     * to the @c routeQuery, @c clientReply, @c closeSession, @c freeSession,
     * and @c handleError functions.
     *
     * @param instance Router instance
     * @param session  Client MXS_SESSION object
     *
     * @return New router session or NULL on error
     */
    MXS_ROUTER_SESSION *(*newSession)(MXS_ROUTER *instance, MXS_SESSION *session);

    /**
     * @brief Called when a session is closed
     *
     * The router should close all objects (including backend DCBs) but not free any memory.
     *
     * @param instance       Router instance
     * @param router_session Router session
     */
    void     (*closeSession)(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);

    /**
     * @brief Called when a session is freed
     *
     * The session should free all allocated memory in this function.
     *
     * @param instance       Router instance
     * @param router_session Router session
     */
    void     (*freeSession)(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);

    /**
     * @brief Called on each query that requires routing
     *
     * TODO: Document how routeQuery should be used
     *
     * @param instance       Router instance
     * @param router_session Router session
     * @param queue          Request from the client
     *
     * @return If successful, the function returns 1. If an error occurs
     * and the session should be closed, the function returns 0.
     */
    int32_t  (*routeQuery)(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue);


    /**
     * @brief Called for diagnostic output
     *
     * @param instance Router instance
     * @param dcb      DCB where the diagnostic information should be written
     */
    void     (*diagnostics)(MXS_ROUTER *instance, DCB *dcb);

    /**
     * @brief Called for diagnostic output
     *
     * @param instance Router instance
     *
     * @return Diagnostic information in JSON format
     *
     * @see jansson.h
     */
    json_t*  (*diagnostics_json)(const MXS_ROUTER *instance);

    /**
     * @brief Called for each reply packet
     *
     * TODO: Document how clientReply should be used
     *
     * @param instance       Router instance
     * @param router_session Router session
     * @param queue          Response from the server
     * @param backend_dcb    The backend DCB which responded to the query
     */
    void     (*clientReply)(MXS_ROUTER* instance, MXS_ROUTER_SESSION *router_session,
                            GWBUF *queue, DCB *backend_dcb);

    /**
     * @brief Called when a backend DCB has failed
     *
     * @param instance       Router instance
     * @param router_session Router session
     * @param errmsgbuf      Error message buffer
     * @param backend_dcb    The backend DCB that has failed
     * @param action         The type of the action (TODO: Remove this parameter)
     *
     * @param succp Pointer to a `bool` which should be set to true for success or false for error
     */
    void     (*handleError)(MXS_ROUTER         *instance,
                            MXS_ROUTER_SESSION *router_session,
                            GWBUF              *errmsgbuf,
                            DCB                *backend_dcb,
                            mxs_error_action_t action,
                            bool*              succp);

    /**
     * @brief Called to obtain the capabilities of the router
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.h
     */
    uint64_t (*getCapabilities)(MXS_ROUTER *instance);

    /**
     * @brief Called for destroying a router instance
     *
     * @param instance Router instance
     */
    void     (*destroyInstance)(MXS_ROUTER *instance);

    /**
     * @brief Configure router instance at runtime
     *
     * This function is guaranteed to be called by only one thread at a time.
     * The router must declare the RCAP_TYPE_RUNTIME_CONFIG in its capabilities
     * in order for this function to be called.
     *
     * Modifications to the router should be made in an atomic manner so that
     * existing sessions do not read a partial configuration. One way to do this
     * is to use shared pointers for storing configurations.
     *
     * @param instance Router instance
     * @param params   Updated parameters for the service. The parameters are
     *                 validated before this function is called.
     *
     * @return True if reconfiguration was successful, false if reconfiguration
     *         failed. If reconfiguration failed, the state of the router
     *         instance should not be modified.
     */
    bool (*configureInstance)(MXS_ROUTER *instance, MXS_CONFIG_PARAMETER* params);

} MXS_ROUTER_OBJECT;

/**
 * The router module API version. Any change that changes the router API
 * must update these versions numbers in accordance with the rules in
 * modinfo.h.
 */
#define MXS_ROUTER_VERSION  { 3, 1, 0 }

/**
 * Specifies capabilities specific for routers. Common capabilities
 * are defined by @c routing_capability_t.
 *
 * @see enum routing_capability
 *
 * @note The values of the capabilities here *must* be between 0x00010000
 *       and 0x00800000, that is, bits 16 to 23.
 */
typedef enum router_capability
{
    RCAP_TYPE_NO_RSESSION    = 0x00010000, /**< Router does not use router sessions */
    RCAP_TYPE_NO_USERS_INIT  = 0x00020000, /**< Prevent the loading of authenticator
                                             users when the service is started */
    RCAP_TYPE_NO_AUTH        = 0x00040000, /**< No `user` or `password` parameter required */
    RCAP_TYPE_RUNTIME_CONFIG = 0x00080000, /**< Router supports runtime cofiguration */
} mxs_router_capability_t;

typedef enum
{
    TYPE_UNDEFINED = 0,
    TYPE_MASTER,
    TYPE_ALL
} mxs_target_t;

/**
 * @brief Convert mxs_target_t to a string
 *
 * @param target Target to convert
 *
 * @return Target type as string
 */
static inline const char* mxs_target_to_str(mxs_target_t target)
{
    switch (target)
    {
    case TYPE_MASTER:
        return "master";

    case TYPE_ALL:
        return "all";

    default:
        return "UNDEFINED";
    }
}

MXS_END_DECLS
