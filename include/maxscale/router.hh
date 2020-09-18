/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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
#include <maxscale/config2.hh>
#include <maxscale/routing.hh>
#include <maxscale/session.hh>
#include <maxscale/target.hh>

using Endpoints = std::vector<mxs::Endpoint*>;

class SERVICE;

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
class RouterSession : public mxs::Routable
{
public:
    /**
     * The RouterSession instance will be deleted when a client session
     * has terminated.
     */
    virtual ~RouterSession() = default;


    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `RouterSession::clientReply`.
     *
     * @param pPacket A buffer containing the reply from the backend
     * @param down    The route the reply took
     * @param reply   The reply object
     */
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    /**
     * Handle backend connection network errors
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param reply     The reply object for this endpoint
     *
     * @return True if the session can continue, false if the session should be closed
     */
    virtual bool handleError(mxs::ErrorType type,
                             GWBUF* pMessage,
                             mxs::Endpoint* pProblem,
                             const mxs::Reply& reply) = 0;

    // Sets the upstream component (don't override this in the inherited class)
    void setUpstream(mxs::Routable* up)
    {
        m_pUp = up;
    }

protected:
    RouterSession(MXS_SESSION* pSession);

protected:
    MXS_SESSION*   m_pSession;      /*< The MXS_SESSION this router session is associated with. */
    mxs::Routable* m_pUp;
};
}

/**
 * Base class of all routers.
 */
class MXS_ROUTER
{
public:
    virtual ~MXS_ROUTER() = default;

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
    virtual mxs::RouterSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints) = 0;

    /**
     * @brief Called for diagnostic output
     *
     * @param instance Router instance
     *
     * @return Diagnostic information in JSON format
     *
     * @see jansson.h
     */
    virtual json_t* diagnostics() const = 0;

    /**
     * @brief Called to obtain the capabilities of the router
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.hh
     */
    virtual uint64_t getCapabilities() const = 0;

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
    virtual bool configure(mxs::ConfigParameters* param) = 0;

    /**
     * Get the configuration of a router instance
     *
     * The configure method of the returned configuration will be called after the initial creation of the
     * router as well as any time a parameter is modified at runtime.
     *
     * @return The configuration for the router instance or nullptr if the router does not use the new
     *         configuration mechanism
     */
    virtual mxs::config::Configuration* getConfiguration() = 0;
};

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
    MXS_ROUTER* (*createInstance)(SERVICE * service, mxs::ConfigParameters* params);
} MXS_ROUTER_OBJECT;

/**
 * The router module API version. Any change that changes the router API
 * must update these versions numbers in accordance with the rules in
 * modinfo.h.
 */
// TODO: Update this from 4.0.0 to 5.0.0 for 2.6
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

    static MXS_ROUTER* createInstance(SERVICE* pService, mxs::ConfigParameters* params)
    {
        RouterType* pRouter = NULL;

        MXS_EXCEPTION_GUARD(pRouter = RouterType::create(pService, params));

        return pRouter;
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
    &Router<RouterType, RouterSessionType>::createInstance
};

template<class RouterInstance>
class RouterApi
{
public:
    RouterApi() = delete;
    RouterApi(const RouterApi&) = delete;
    RouterApi& operator=(const RouterApi&) = delete;

    static MXS_ROUTER* createInstance(SERVICE* pService, mxs::ConfigParameters* params)
    {
        RouterInstance* pInstance = NULL;
        MXS_EXCEPTION_GUARD(pInstance = RouterInstance::create(pService, params));
        return pInstance;
    }

    static MXS_ROUTER_OBJECT s_api;
};

template<class RouterInstance>
MXS_ROUTER_OBJECT RouterApi<RouterInstance>::s_api =
{
    &RouterApi<RouterInstance>::createInstance,
};
}
