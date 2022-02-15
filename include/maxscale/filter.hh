/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file include/maxscale/filter.hh - The public filter interface
 */

#include <maxscale/ccdefs.hh>
#include <stdint.h>
#include <maxbase/jansson.h>
#include <maxscale/buffer.hh>
#include <maxscale/config.hh>
#include <maxscale/dcb.hh>
#include <maxscale/routing.hh>
#include <maxscale/session.hh>

/**
 * MXS_FILTER is an opaque type representing a particular filter instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c createInstance function of a filter module and subsequently
 * passing it back to the API functions of the filter.
 */
typedef struct mxs_filter
{
} MXS_FILTER;

/**
 * MXS_FILTER_SESSION is an opaque type representing the session related
 * data of a particular filter instance.
 *
 * MaxScale itself does not do anything with it, except for receiving it
 * from the @c newSession function of a filter module and subsequently
 * passing it back to the API functions of the filter.
 */
typedef struct mxs_filter_session
{
} MXS_FILTER_SESSION;

/**
 * The "module object" structure for a filter module. All entry points
 * marked with `(optional)` are optional entry points which can be set to NULL
 * if no implementation is required.
 */
typedef struct mxs_filter_object
{
    /**
     * @brief Create a new instance of the filter
     *
     * This function is called when a new filter instance is created. The return
     * value of this function will be passed as the first parameter to the
     * other API functions.
     *
     * @param name    Name of the filter instance
     * @param params  Filter parameters
     *
     * @return New filter instance on NULL on error
     */
    MXS_FILTER* (*createInstance)(const char* name, mxs::ConfigParameters* params);

    /**
     * Called to create a new user session within the filter
     *
     * This function is called when a new filter session is created for a client.
     * The return value of this function will be passed as the second parameter
     * to the @c routeQuery, @c clientReply, @c closeSession, @c freeSession,
     * @c setDownstream and @c setUpstream functions.
     *
     * @param instance Filter instance
     * @param session  Client MXS_SESSION object
     * @param service  The service in which this filter session is created
     * @param down     Downstream component of the filter chain, route queries here
     * @param up       Upstream component of the filter chain, send replies here
     *
     * @note Don't copy the upstream or downstream components, use the provided pointers instead.
     *
     * @return New filter session or NULL on error
     */
    MXS_FILTER_SESSION* (*newSession)(MXS_FILTER* instance,
        MXS_SESSION* session,
        SERVICE* service,
        mxs::Downstream* down,
        mxs::Upstream* up);

    /**
     * @brief Called when a session is closed
     *
     * The filter should close all objects but not free any memory.
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void (*closeSession)(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession);

    /**
     * @brief Called when a session is freed
     *
     * The session should free all allocated memory in this function.
     *
     * @param instance Filter instance
     * @param fsession Filter session
     */
    void (*freeSession)(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession);

    /**
     * @brief Called on each query that requires routing
     *
     * TODO: Document how routeQuery should be used
     *
     * @param instance Filter instance
     * @param fsession Filter session
     * @param queue    Request from the client
     *
     * @return If successful, the function returns 1. If an error occurs
     * and the session should be closed, the function returns 0.
     */
    int32_t (*routeQuery)(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);

    /**
     * @brief Called for each reply packet
     *
     * TODO: Document how clientReply should be used
     *
     * @param instance    Filter instance
     * @param fsession    Filter session
     * @param queue       Response from the server
     * @param down        The downstream components where the response came from
     * @param reply       The reply information (@see target.hh)
     *
     * @return If successful, the function returns 1. If an error occurs and the session should be closed, the
     *         function returns 0.
     */
    int32_t (*clientReply)(MXS_FILTER* instance,
        MXS_FILTER_SESSION* fsession,
        GWBUF* queue,
        const mxs::ReplyRoute& down,
        const mxs::Reply& reply);

    /**
     * @brief Called for diagnostic output
     *
     * @param instance Filter instance
     * @param fsession Filter session, NULL if general information about the filter is queried
     *
     * @return JSON formatted information about the filter
     *
     * @see jansson.h
     */
    json_t* (*diagnostics)(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);

    /**
     * @brief Called to obtain the capabilities of the filter
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.hh
     */
    uint64_t (*getCapabilities)(MXS_FILTER* instance);

    /**
     * @brief Called for destroying a filter instance
     *
     * @param instance Filter instance
     */
    void (*destroyInstance)(MXS_FILTER* instance);
} MXS_FILTER_OBJECT;

/**
 * The filter API version. If the MXS_FILTER_OBJECT structure or the filter API
 * is changed these values must be updated in line with the rules in the
 * file modinfo.h.
 */
#define MXS_FILTER_VERSION \
    {                      \
        4, 0, 0            \
    }

/**
 * MXS_FILTER_DEF represents a filter definition from the configuration file.
 * Its exact definition is private to MaxScale.
 */
struct mxs_filter_def;

typedef struct mxs_filter_def
{
} MXS_FILTER_DEF;

/**
 * Get the name of a filter definition. This corresponds to
 * to a filter section in the configuration file.
 *
 * @param filter_def  A filter definition.
 *
 * @return The filter name.
 */
const char* filter_def_get_name(const MXS_FILTER_DEF* filter_def);

/**
 * Get module name of a filter definition.
 *
 * @param filter_def  A filter definition.
 *
 * @return The module name.
 */
const char* filter_def_get_module_name(const MXS_FILTER_DEF* filter_def);

/**
 * Get the filter instance of a particular filter definition.
 *
 * @return A filter instance.
 */
MXS_FILTER* filter_def_get_instance(const MXS_FILTER_DEF* filter_def);

/**
 * Specifies capabilities specific for filters. Common capabilities
 * are defined by @c routing_capability_t.
 *
 * @see enum routing_capability
 *
 * @note The values of the capabilities here *must* be between 0x80000000
 *       and 0x01000000, that is, bits 24 to 31.
 */

typedef enum filter_capability
{
    FCAP_TYPE_NONE = 0x0  // TODO: remove once filter capabilities are defined
} filter_capability_t;

namespace maxscale
{

/**
 * @class FilterSession filter.hh <maxscale/filter.hh>
 *
 * FilterSession is a base class for filter sessions. A concrete filter session
 * class should be derived from this class and override all relevant functions.
 *
 * Note that even though this class is intended to be derived from, no functions
 * are virtual. That is by design, as the class will be used in a context where
 * the concrete class is known. That is, there is no need for the virtual mechanism.
 */
class FilterSession : public MXS_FILTER_SESSION
{
public:
    /**
     * @class Downstream
     *
     * An instance of this class represents a component following a filter.
     */
    class Downstream
    {
    public:
        Downstream() {}

        Downstream(const mxs::Downstream* down)
            : m_data(down)
        {}

        /**
         * Function for sending a packet from the client to the next component
         * in the routing chain towards the backend.
         *
         * @param pPacket  A packet to be delivered towards the backend.
         *
         * @return Whatever the following component returns.
         */
        int routeQuery(GWBUF* pPacket)
        {
            return m_data->routeQuery(m_data->instance, m_data->session, pPacket);
        }

        const mxs::Downstream* m_data {nullptr};
    };

    class Upstream
    {
    public:
        /**
         * @class Upstream
         *
         * An instance of this class represents a component preceeding a filter.
         */
        Upstream() {}

        Upstream(const mxs::Upstream* up)
            : m_data(up)
        {}

        /**
         * Function for sending a packet from the backend to the next component
         * in the routing chain towards the client.
         *
         * @param pPacket  A packet to be delivered towards the backend.
         *
         * @return Whatever the following component returns.
         */
        int clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
        {
            return m_data->clientReply(m_data->instance, m_data->session, pPacket, down, reply);
        }

        const mxs::Upstream* m_data {nullptr};
    };

    /**
     * The FilterSession instance will be deleted when a client session
     * has terminated. Will be called only after @c close() has been called.
     */
    ~FilterSession();

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called for setting the component following this filter session.
     *
     * @param down The component following this filter.
     */
    void setDownstream(const Downstream& down);

    /**
     * Called for setting the component preceeding this filter session.
     *
     * @param up The component preceeding this filter.
     */
    void setUpstream(const Upstream& up);

    /**
     * Called when a packet being is routed to the backend. The filter should
     * forward the packet to the downstream component.
     *
     * @param pPacket A client packet.
     *
     * @return 1 for success, 0 for error
     */
    int routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The filter should
     * forward the packet to the upstream component.
     *
     * @param pPacket A client packet.
     * @param down    The downstream components where the response came from
     * @param reply   The reply information (@see target.hh)
     *
     * @return 1 for success, 0 for error
     */
    int clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    /**
     * Called for obtaining diagnostics about the filter session.
     */
    json_t* diagnostics() const;

protected:
    FilterSession(MXS_SESSION* pSession, SERVICE* service);

    /**
     * To be called by a filter that short-circuits the request processing.
     * If this function is called (in routeQuery), the filter must return
     * without passing the request further.
     *
     * @param pResponse  The response to be sent to the client.
     * @param pTarget    The source of the response
     */
    void set_response(GWBUF* pResponse) const
    {
        session_set_response(m_pSession, m_pService, m_up.m_data, pResponse);
    }

protected:
    MXS_SESSION* m_pSession; /*< The MXS_SESSION this filter session is associated with. */
    SERVICE* m_pService;     /*< The service for which this session was created. */
    Downstream m_down;       /*< The downstream component. */
    Upstream m_up;           /*< The upstream component. */
};

/**
 * @class Filter filter.hh <maxscale/filter.hh>
 *
 * An instantiation of the Filter template is used for creating a filter.
 * Filter is an example of the "Curiously recurring template pattern"
 * https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
 * that is used for compile time polymorphism.
 *
 * The typical way for using the template is as follows:
 *
 * @code
 * class MyFilterSession : public maxscale::FilterSession
 * {
 *     // Override the relevant functions.
 * };
 *
 * class MyFilter : public maxscale::Filter<MyFilter, MyFilterSession>
 * {
 * public:
 *      // This creates a new filter instance
 *      static MyFilter* create(const char* zName, mxs::ConfigParameters* ppParams);
 *
 *      // This creates a new session for a filter instance
 *      MyFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);
 *
 *      // Diagnostic function that returns a JSON object
 *      json_t* diagnostics() const;
 *
 *      // Get filter capabilities
 *      uint64_t getCapabilities();
 * };
 * @endcode
 *
 * The concrete filter class must implement the methods @c create, @c newSession,
 * @c diagnostics and @c getCapabilities, with the prototypes as shown above.
 *
 * The plugin function @c GetModuleObject is then implemented as follows:
 *
 * @code
 * extern "C" MODULE* MXS_CREATE_MODULE()
 * {
 *     return &MyFilter::s_object;
 * };
 * @endcode
 */
template<class FilterType, class FilterSessionType>
class Filter : public MXS_FILTER
{
public:
    static MXS_FILTER* apiCreateInstance(const char* zName, mxs::ConfigParameters* ppParams)
    {
        FilterType* pFilter = NULL;

        MXS_EXCEPTION_GUARD(pFilter = FilterType::create(zName, ppParams));

        return pFilter;
    }

    static MXS_FILTER_SESSION* apiNewSession(MXS_FILTER* pInstance,
        MXS_SESSION* pSession,
        SERVICE* pService,
        mxs::Downstream* pDown,
        mxs::Upstream* pUp)
    {
        FilterType* pFilter               = static_cast<FilterType*>(pInstance);
        FilterSessionType* pFilterSession = NULL;

        MXS_EXCEPTION_GUARD(pFilterSession = pFilter->newSession(pSession, pService));

        if (pFilterSession)
        {
            typename FilterSessionType::Downstream down(pDown);
            typename FilterSessionType::Upstream up(pUp);

            MXS_EXCEPTION_GUARD(pFilterSession->setDownstream(down));
            MXS_EXCEPTION_GUARD(pFilterSession->setUpstream(up));
        }

        return pFilterSession;
    }

    static void apiCloseSession(MXS_FILTER*, MXS_FILTER_SESSION* pData)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pFilterSession->close());
    }

    static void apiFreeSession(MXS_FILTER*, MXS_FILTER_SESSION* pData)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(delete pFilterSession);
    }

    static int apiRouteQuery(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pData, GWBUF* pPacket)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        int rv = 0;
        MXS_EXCEPTION_GUARD(rv = pFilterSession->routeQuery(pPacket));

        return rv;
    }

    static int apiClientReply(MXS_FILTER* pInstance,
        MXS_FILTER_SESSION* pData,
        GWBUF* pPacket,
        const mxs::ReplyRoute& down,
        const mxs::Reply& reply)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        int rv = 0;
        MXS_EXCEPTION_GUARD(rv = pFilterSession->clientReply(pPacket, down, reply));

        return rv;
    }

    static json_t* apiDiagnostics(const MXS_FILTER* pInstance, const MXS_FILTER_SESSION* pData)
    {
        json_t* rval = NULL;

        if (pData)
        {
            const FilterSessionType* pFilterSession = static_cast<const FilterSessionType*>(pData);

            MXS_EXCEPTION_GUARD(rval = pFilterSession->diagnostics());
        }
        else
        {
            const FilterType* pFilter = static_cast<const FilterType*>(pInstance);

            MXS_EXCEPTION_GUARD(rval = pFilter->diagnostics());
        }

        return rval;
    }

    static uint64_t apiGetCapabilities(MXS_FILTER* pInstance)
    {
        uint64_t rv = 0;

        FilterType* pFilter = static_cast<FilterType*>(pInstance);

        MXS_EXCEPTION_GUARD(rv = pFilter->getCapabilities());

        return rv;
    }

    static void apiDestroyInstance(MXS_FILTER* pInstance)
    {
        FilterType* pFilter = static_cast<FilterType*>(pInstance);

        MXS_EXCEPTION_GUARD(delete pFilter);
    }

    static MXS_FILTER_OBJECT s_object;
};

template<class FilterType, class FilterSessionType>
MXS_FILTER_OBJECT Filter<FilterType, FilterSessionType>::s_object = {
    &FilterType::apiCreateInstance,
    &FilterType::apiNewSession,
    &FilterType::apiCloseSession,
    &FilterType::apiFreeSession,
    &FilterType::apiRouteQuery,
    &FilterType::apiClientReply,
    &FilterType::apiDiagnostics,
    &FilterType::apiGetCapabilities,
    &FilterType::apiDestroyInstance,
};
}  // namespace maxscale
