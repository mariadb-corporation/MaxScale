/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <maxbase/jansson.h>
#include <maxscale/buffer.hh>
#include <maxscale/routing.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>

using Endpoints = std::vector<mxs::Endpoint*>;

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

/**
 * The "module object" structure for a query router module. All entry points
 * marked with `(optional)` are optional entry points which can be set to NULL
 * if no implementation is required.
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
     * @param params  Parameters for the router
     *
     * @return New router instance on NULL on error
     */
    MXS_ROUTER*(*createInstance)(SERVICE * service, mxs::ConfigParameters* params);

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
     * @param up       The upstream component where responses are routed to
     *
     * @return New router session or NULL on error
     */
    MXS_ROUTER_SESSION*(*newSession)(MXS_ROUTER * instance, MXS_SESSION* session, mxs::Upstream* up,
                                     const Endpoints& endpoints);

    /**
     * @brief Called when a session is closed
     *
     * The router should close all objects (including backend DCBs) but not free any memory.
     *
     * @param instance       Router instance
     * @param router_session Router session
     */
    void (* closeSession)(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);

    /**
     * @brief Called when a session is freed
     *
     * The session should free all allocated memory in this function.
     *
     * @param instance       Router instance
     * @param router_session Router session
     */
    void (* freeSession)(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session);

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
    int32_t (* routeQuery)(MXS_ROUTER* instance, MXS_ROUTER_SESSION* router_session, GWBUF* queue);

    /**
     * @brief Called for diagnostic output
     *
     * @param instance Router instance
     *
     * @return Diagnostic information in JSON format
     *
     * @see jansson.h
     */
    json_t*  (*diagnostics)(const MXS_ROUTER * instance);

    /**
     * @brief Called for each reply packet
     *
     * TODO: Document how clientReply should be used
     *
     * @param instance       Router instance
     * @param router_session Router session
     * @param queue          Response from the server
     * @param backend_dcb    The downstream endpoint which responded to the query
     */
    void (* clientReply)(MXS_ROUTER* instance,
                         MXS_ROUTER_SESSION* router_session,
                         GWBUF* queue,
                         const mxs::ReplyRoute& down,
                         const mxs::Reply& reply);

    /**
     * @brief Called when a backend DCB has failed
     *
     * If the router session can continue with reduced capabilities, for example execute only read-only
     * queries when a master is lost, then the function should close the DCB and return true. If the router
     * cannot continue, the function should return false.
     *
     * @param instance       Router instance
     * @param router_session Router session
     * @param type           Error type, transient or permanent
     * @param errmsgbuf      Error message buffer
     * @param down           The downstream endpoint that failed
     * @param reply          The current reply state at the time the error occurred
     *
     * @return True for success or false for error
     */
    bool (* handleError)(MXS_ROUTER* instance,
                         MXS_ROUTER_SESSION* router_session,
                         mxs::ErrorType type,
                         GWBUF* errmsgbuf,
                         mxs::Endpoint* down,
                         const mxs::Reply& reply);

    /**
     * @brief Called to obtain the capabilities of the router
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.hh
     */
    uint64_t (* getCapabilities)(MXS_ROUTER* instance);

    /**
     * @brief Called for destroying a router instance
     *
     * @param instance Router instance
     */
    void (* destroyInstance)(MXS_ROUTER* instance);

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
    bool (* configureInstance)(MXS_ROUTER* instance, mxs::ConfigParameters* params);
} MXS_ROUTER_OBJECT;

/**
 * The router module API version. Any change that changes the router API
 * must update these versions numbers in accordance with the rules in
 * modinfo.h.
 */
#define MXS_ROUTER_VERSION {4, 0, 0}

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
    RCAP_TYPE_NO_USERS_INIT  = 0x00010000,  /**< Prevent the loading of authenticator
                                             *  users when the service is started */
    RCAP_TYPE_RUNTIME_CONFIG = 0x00020000,  /**< Router supports runtime cofiguration */
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

namespace maxscale
{

/**
 * @class RouterSession router.hh <maxscale/router.hh>
 *
 * RouterSession is a base class for router sessions. A concrete router session
 * class should be derived from this class and override all relevant functions.
 *
 * Note that even though this class is intended to be derived from, no functions
 * are virtual. That is by design, as the class will be used in a context where
 * the concrete class is known. That is, there is no need for the virtual mechanism.
 */
class RouterSession : public MXS_ROUTER_SESSION
{
public:
    /**
     * The RouterSession instance will be deleted when a client session
     * has terminated. Will be called only after @c close() has been called.
     */
    ~RouterSession();

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called when a packet being is routed to the backend. The router should
     * forward the packet to the appropriate server(s).
     *
     * @param pPacket A client packet.
     */
    int32_t routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `RouterSession::clientReply`.
     *
     * @param pPacket A buffer containing the reply from the backend
     * @param down    The route the reply took
     * @param reply   The reply object
     */
    void clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    /**
     * Handle backend connection network errors
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param reply     The reply object for this endpoint
     *
     * @return True if the session can continue, false if the session should be closed
     */
    bool handleError(mxs::ErrorType type, GWBUF* pMessage, mxs::Endpoint* pProblem, const mxs::Reply& reply);

    // Sets the upstream component (don't override this in the inherited class)
    void setUpstream(mxs::Upstream* up)
    {
        m_pUp = up;
    }

protected:
    RouterSession(MXS_SESSION* pSession);

protected:
    MXS_SESSION*   m_pSession;  /*< The MXS_SESSION this router session is associated with. */
    mxs::Upstream* m_pUp;
};


/**
 * @class Router router.hh <maxscale/router.hh>
 *
 * An instantiation of the Router template is used for creating a router.
 * Router is an example of the "Curiously recurring template pattern"
 * https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 * that is used for compile time polymorfism.
 *
 * The typical way for using the template is as follows:
 *
 * @code
 * class MyRouterSession : public maxscale::RouterSession
 * {
 *     // Override the relevant functions.
 * };
 *
 * class MyRouter : public maxscale::Router<MyRouter, MyRouterSession>
 * {
 * public:
 *      static MyRouter* create(SERVICE* pService, mxs::ConfigParameters* params);
 *
 *      MyRouterSession* newSession(MXS_SESSION* pSession);
 *
 *      uint64_t getCapabilities();
 * };
 * @endcode
 *
 * The concrete router class must implement the methods @c create, @c newSession,
 * @c diagnostics and @c getCapabilities, with the prototypes as shown above.
 *
 * The plugin function @c GetModuleObject is then implemented as follows:
 *
 * @code
 * extern "C" MXS_MODULE* MXS_CREATE_MODULE()
 * {
 *     static MXS_MODULE module_object =
 *     {
 *         ...
 *         &MyRouter::s_object,
 *         ...
 *     };
 *
 *     return &module_object;
 * }
 * @endcode
 */
template<class RouterType, class RouterSessionType>
class Router : public MXS_ROUTER
{
public:

    // The default configure entry point, does nothing and always fails
    bool configure(mxs::ConfigParameters* param)
    {
        return false;
    }

    static MXS_ROUTER* createInstance(SERVICE* pService, mxs::ConfigParameters* params)
    {
        RouterType* pRouter = NULL;

        MXS_EXCEPTION_GUARD(pRouter = RouterType::create(pService, params));

        return pRouter;
    }

    static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* pInstance, MXS_SESSION* pSession,
                                          mxs::Upstream* up, const Endpoints& endpoints)
    {
        RouterType* pRouter = static_cast<RouterType*>(pInstance);
        RouterSessionType* pRouter_session = nullptr;

        MXS_EXCEPTION_GUARD(pRouter_session = pRouter->newSession(pSession, endpoints));

        if (pRouter_session)
        {
            pRouter_session->setUpstream(up);
        }

        return pRouter_session;
    }

    static void closeSession(MXS_ROUTER*, MXS_ROUTER_SESSION* pData)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pRouter_session->close());
    }

    static void freeSession(MXS_ROUTER*, MXS_ROUTER_SESSION* pData)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(delete pRouter_session);
    }

    static int32_t routeQuery(MXS_ROUTER*, MXS_ROUTER_SESSION* pData, GWBUF* pPacket)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        int32_t rv = 0;
        MXS_EXCEPTION_GUARD(rv = pRouter_session->routeQuery(pPacket));

        return rv;
    }

    static json_t* diagnostics(const MXS_ROUTER* pInstance)
    {
        const RouterType* pRouter = static_cast<const RouterType*>(pInstance);

        json_t* rval = NULL;

        MXS_EXCEPTION_GUARD(rval = pRouter->diagnostics());

        return rval;
    }

    static void clientReply(MXS_ROUTER*, MXS_ROUTER_SESSION* pData, GWBUF* pPacket,
                            const mxs::ReplyRoute& pDown, const mxs::Reply& reply)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pRouter_session->clientReply(pPacket, pDown, reply));
    }

    static bool handleError(MXS_ROUTER* pInstance,
                            MXS_ROUTER_SESSION* pData,
                            mxs::ErrorType type,
                            GWBUF* pMessage,
                            mxs::Endpoint* pProblem,
                            const mxs::Reply& pReply)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        bool rv = false;
        MXS_EXCEPTION_GUARD(rv = pRouter_session->handleError(type, pMessage, pProblem, pReply));
        return rv;
    }

    static uint64_t getCapabilities(MXS_ROUTER* pInstance)
    {
        uint64_t rv = 0;

        RouterType* pRouter = static_cast<RouterType*>(pInstance);

        MXS_EXCEPTION_GUARD(rv = pRouter->getCapabilities());

        return rv;
    }

    static void destroyInstance(MXS_ROUTER* pInstance)
    {
        RouterType* pRouter = static_cast<RouterType*>(pInstance);

        MXS_EXCEPTION_GUARD(delete pRouter);
    }

    static bool configure(MXS_ROUTER* pInstance, mxs::ConfigParameters* param)
    {
        RouterType* pRouter = static_cast<RouterType*>(pInstance);
        bool rval = false;
        MXS_EXCEPTION_GUARD(rval = pRouter->configure(param));
        return rval;
    }

    static MXS_ROUTER_OBJECT s_object;

protected:
    Router(SERVICE* pService)
        : m_pService(pService)
    {
    }

    SERVICE* m_pService;
};


template<class RouterType, class RouterSessionType>
MXS_ROUTER_OBJECT Router<RouterType, RouterSessionType>::s_object =
{
    &Router<RouterType, RouterSessionType>::createInstance,
    &Router<RouterType, RouterSessionType>::newSession,
    &Router<RouterType, RouterSessionType>::closeSession,
    &Router<RouterType, RouterSessionType>::freeSession,
    &Router<RouterType, RouterSessionType>::routeQuery,
    &Router<RouterType, RouterSessionType>::diagnostics,
    &Router<RouterType, RouterSessionType>::clientReply,
    &Router<RouterType, RouterSessionType>::handleError,
    &Router<RouterType, RouterSessionType>::getCapabilities,
    &Router<RouterType, RouterSessionType>::destroyInstance,
    &Router<RouterType, RouterSessionType>::configure,
};
}
