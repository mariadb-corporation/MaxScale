/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <vector>
#include <set>
#include <maxscale/parser.hh>
#include <maxscale/routing.hh>
#include <maxscale/target.hh>

class SERVICE;
class MXS_SESSION;
struct json_t;

namespace maxscale
{
class ProtocolData;
class ConfigParameters;
namespace config
{
class Configuration;
}
class Parser;
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
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    /**
     * Handle backend connection network errors
     *
     * If the router does not override this function, the error is by default propagated upstream to the next
     * component in the routing chain. For top-level services, this means that the MXS_SESSION handles it
     * which will cause the connection to be killed.
     *
     * @param type      The type of the error, either temporary or permanent.
     * @param message   The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param reply     The reply object for this endpoint.
     *
     * @return True if the session can continue, false if the session should be closed
     */
    virtual bool
    handleError(mxs::ErrorType type, const std::string& message, mxs::Endpoint* pProblem,
                const mxs::Reply& reply);

    /**
     * Called by the service when a ServerEndpoint connection has been released and placed to the pool.
     * A router should implement this function if it can use it to optimize its behavior. E.g. avoid sending
     * queries to the affected endpoint.
     *
     * @param down The pooled endpoint
     */
    virtual void endpointConnReleased(Endpoint* down)
    {
    }

    // Sets the upstream routable (a filter, if one exists)
    void setUpstream(mxs::Routable* up)
    {
        m_pUp = up;
    }

    /**
     * Returns a parser appropriate for the protocol of this session's client
     * connection. This function must only be called if it is know, due to the
     * context where it is called, that there will be a parser.
     *
     * @return The parser associated with the protocol of this session's client connection.
     */
    const Parser& parser() const
    {
        return const_cast<RouterSession*>(this)->parser();
    }

    Parser& parser()
    {
        mxb_assert_message(m_pParser, "Protocol of client connection does not have a parser.");
        return *m_pParser;
    }

    /**
     * @return The SQL of @c packet, or an empty string if it does not contain SQL.
     */
    std::string_view get_sql(const GWBUF& stmt) const
    {
        return parser().get_sql(stmt);
    }

    // TODO: To be removed when everyone can handle string_views.
    std::string get_sql_string(const GWBUF& stmt) const
    {
        return std::string { get_sql(stmt) };
    }

    // Sets the upstream component (session, service)
    void setUpstreamComponent(mxs::Component* upstream)
    {
        m_pUpstream = upstream;
    }

protected:
    RouterSession(MXS_SESSION* pSession);

    /**
     * To be called by a router that short-circuits the request processing.
     *
     * This function can only be used inside the routeQuery function of the router. If this function is
     * called, the router must return without passing the request further.
     *
     * @param pResponse  The response to be sent to the client.
     */
    void set_response(GWBUF&& response) const;

    /**
     * Get the protocol data for this session
     *
     * @return The protocol data if the protocol provided it. A null pointer if it didn't.
     */
    const mxs::ProtocolData* protocol_data() const;

    MXS_SESSION* m_pSession;    /*< The MXS_SESSION this router session is associated with. */
    Parser*      m_pParser;     /*< The parser suitable the protocol of this router. */

private:
    mxs::Routable*  m_pUp;          // The next upstream routable
    mxs::Component* m_pUpstream;    // The next upstream component
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
     * @return The configuration for the router instance
     */
    virtual mxs::config::Configuration& getConfiguration() = 0;

    /**
     * Get the set of supported protocols
     *
     * @return The names of the protocols supported by this router. If the router is protocol-agnostic,
     *         the constant MXS_ANY_PROTOCOL can be used.
     */
    virtual std::set<std::string> protocols() const = 0;
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
    RCAP_TYPE_RUNTIME_CONFIG = 0x00020000,      /**< Router supports runtime configuration */
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
