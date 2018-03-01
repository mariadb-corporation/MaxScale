#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/router.h>

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
     * forward the packet to the client using `MXS_SESSION_ROUTE_REPLY`.
     *
     * @param pPacket  A client packet.
     * @param pBackend The backend the packet is coming from.
     */
    void clientReply(GWBUF* pPacket, DCB* pBackend);

    /**
     * Called when upstream's writeq is above high water mark
     *
     * @param op         Type of throttle operation
     */
    void throttle(throttle_op_t op);

    /**
     *
     * @param pMessage  The rror message.
     * @param pProblem  The DCB on which the error occurred.
     * @param action    The context.
     * @param pSuccess  On output, if false, the session will be terminated.
     */
    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);

protected:
    RouterSession(MXS_SESSION* pSession);

protected:
    MXS_SESSION* m_pSession; /*< The MXS_SESSION this router session is associated with. */
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
 *      static MyRouter* create(SERVICE* pService, char** pzOptions);
 *
 *      MyRouterSession* newSession(MXS_SESSION* pSession);
 *
 *      void diagnostics(DCB* pDcb);
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
    static MXS_ROUTER* createInstance(SERVICE* pService, char** pzOptions)
    {
        RouterType* pRouter = NULL;

        MXS_EXCEPTION_GUARD(pRouter = RouterType::create(pService, pzOptions));

        return pRouter;
    }

    static MXS_ROUTER_SESSION* newSession(MXS_ROUTER* pInstance, MXS_SESSION* pSession)
    {
        RouterType* pRouter = static_cast<RouterType*>(pInstance);
        RouterSessionType* pRouter_session;

        MXS_EXCEPTION_GUARD(pRouter_session = pRouter->newSession(pSession));

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

    static void diagnostics(MXS_ROUTER* pInstance, DCB* pDcb)
    {
        RouterType* pRouter = static_cast<RouterType*>(pInstance);

        MXS_EXCEPTION_GUARD(pRouter->diagnostics(pDcb));
    }

    static json_t* diagnostics_json(const MXS_ROUTER* pInstance)
    {
        const RouterType* pRouter = static_cast<const RouterType*>(pInstance);

        json_t* rval = NULL;

        MXS_EXCEPTION_GUARD(rval = pRouter->diagnostics_json());

        return rval;
    }

    static void clientReply(MXS_ROUTER*, MXS_ROUTER_SESSION* pData, GWBUF* pPacket, DCB* pBackend)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pRouter_session->clientReply(pPacket, pBackend));
    }

    static void throttle(MXS_ROUTER*, MXS_ROUTER_SESSION* pData, throttle_op_t op)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pRouter_session->throttle(op));
    }

    static void handleError(MXS_ROUTER*         pInstance,
                            MXS_ROUTER_SESSION* pData,
                            GWBUF*              pMessage,
                            DCB*                pProblem,
                            mxs_error_action_t  action,
                            bool*               pSuccess)
    {
        RouterSessionType* pRouter_session = static_cast<RouterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pRouter_session->handleError(pMessage, pProblem, action, pSuccess));
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

    static MXS_ROUTER_OBJECT s_object;

protected:
    Router(SERVICE *pService)
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
    &Router<RouterType, RouterSessionType>::diagnostics_json,
    &Router<RouterType, RouterSessionType>::clientReply,
    &Router<RouterType, RouterSessionType>::throttle,
    &Router<RouterType, RouterSessionType>::handleError,
    &Router<RouterType, RouterSessionType>::getCapabilities,
    &Router<RouterType, RouterSessionType>::destroyInstance,
};


}
