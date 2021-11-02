/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <vector>
#include <maxscale/routing.hh>
#include <maxscale/target.hh>

class SERVICE;
class MXS_SESSION;
struct json_t;

namespace maxscale
{
class ConfigParameters;
namespace config
{
class Configuration;
}
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
    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    /**
     * Handle backend connection network errors
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param reply     The reply object for this endpoint
     *
     * @return True if the session can continue, false if the session should be closed
     */
    virtual bool
    handleError(mxs::ErrorType type, GWBUF* pMessage, mxs::Endpoint* pProblem, const mxs::Reply& reply) = 0;

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

/**
 * Base class of all routers.
 */
class Router
{
public:
    virtual ~Router() = default;

    /**
     * This function is called after a client has been authenticated and query routing should begin.
     * A router module needs to implement its own RouterSession-class, which in turn implements the
     * query routing and client reply handling logic.
     *
     * @param session Base session object
     * @param endpoints Routing targets of the service
     * @return New router session or NULL on error
     */
    virtual mxs::RouterSession* newSession(MXS_SESSION* session, const mxs::Endpoints& endpoints) = 0;

    /**
     * @brief Called for diagnostic output
     *
     * @return Diagnostic information in JSON format
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
     * Get the configuration of a router instance
     *
     * The configure method of the returned configuration will be called after the initial creation of the
     * router as well as any time a parameter is modified at runtime.
     *
     * @return The configuration for the router instance or nullptr if the router does not use the new
     *         configuration mechanism
     */
    virtual mxs::config::Configuration& getConfiguration() = 0;
};

/**
 * Router C API.
 */
struct MXS_ROUTER_API
{
    /**
     * @brief Create a new instance of the router
     *
     * This function is called when a new router instance is created.
     *
     * @param service The service where the instance is created
     * @param params  Parameters for the router
     *
     * @return New router instance on NULL on error
     */
    Router* (* createInstance)(SERVICE* service);
};
}

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
enum mxs_router_capability_t
{
    RCAP_TYPE_RUNTIME_CONFIG = 0x00020000,      /**< Router supports runtime cofiguration */
};

enum mxs_target_t
{
    TYPE_UNDEFINED = 0,
    TYPE_MASTER,
    TYPE_ALL
};

namespace maxscale
{

template<class RouterClass>
class RouterApi
{
public:
    RouterApi() = delete;
    RouterApi(const RouterApi&) = delete;
    RouterApi& operator=(const RouterApi&) = delete;

    static Router* createInstance(SERVICE* pService)
    {
        Router* inst = nullptr;
        MXS_EXCEPTION_GUARD(inst = RouterClass::create(pService));
        return inst;
    }

    static MXS_ROUTER_API s_api;
};

template<class RouterClass>
MXS_ROUTER_API RouterApi<RouterClass>::s_api =
{
    &RouterApi<RouterClass>::createInstance,
};
}
