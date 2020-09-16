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

namespace maxscale
{
class FilterSession;
}

/**
 * MXS_FILTER is the abstract class that filters implement.
 */
struct MXS_FILTER
{
    virtual ~MXS_FILTER() = default;

    /**
     * Called to create a new user session within the filter
     *
     * This function is called when a new filter session is created for a client.
     *
     * @param session  Client MXS_SESSION object
     * @param service  The service in which this filter session is created
     *
     * @return New filter session or NULL on error
     */
    virtual mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service) = 0;

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
    virtual json_t* diagnostics() const = 0;

    /**
     * @brief Called to obtain the capabilities of the filter
     *
     * @return Zero or more bitwise-or'd values from the mxs_routing_capability_t enum
     *
     * @see routing.hh
     */
    virtual uint64_t getCapabilities() const = 0;

    /**
     * Get the configuration of a filter instance
     *
     * The configure method of the returned configuration will be called after the initial creation of the
     * filter as well as any time a parameter is modified at runtime.
     *
     * @return The configuration for the filter instance or nullptr if the filter does not use the new
     *         configuration mechanism
     */
    virtual mxs::config::Configuration* getConfiguration() = 0;
};

struct MXS_FILTER_OBJECT;

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
class FilterSession : public mxs::Routable
{
public:
    /**
     * The FilterSession instance will be deleted when a client session
     * has terminated. Will be called only after @c close() has been called.
     */
    virtual ~FilterSession();

    /**
     * Called for setting the component following this filter session.
     *
     * @param down The component following this filter.
     */
    void setDownstream(mxs::Routable* down);

    /**
     * Called for setting the component preceeding this filter session.
     *
     * @param up The component preceeding this filter.
     */
    void setUpstream(mxs::Routable* up);

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
        session_set_response(m_pSession, m_pService, m_up, pResponse);
    }

protected:
    MXS_SESSION*   m_pSession;      /*< The MXS_SESSION this filter session is associated with. */
    SERVICE*       m_pService;      /*< The service for which this session was created. */
    mxs::Routable* m_down = nullptr;/*< The downstream component. */
    mxs::Routable* m_up = nullptr;  /*< The upstream component. */
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
 *
 *      // Reconfigure filter at runtime (see configureInstance)
 *      bool configure(mxs::ConfigParameters* param);
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

    static MXS_FILTER_OBJECT s_object;
};

template<class FilterType, class FilterSessionType>
MXS_FILTER_OBJECT Filter<FilterType, FilterSessionType>::s_object =
{
    &FilterType::apiCreateInstance,
};
}

/**
 * The filter API version. If the MXS_FILTER_OBJECT structure or the filter API
 * is changed these values must be updated in line with the rules in the
 * file modinfo.h.
 */
// TODO: Update this from 4.0.0 to 5.0.0 for 2.6
#define MXS_FILTER_VERSION {4, 0, 0}

/**
 * The "module object" structure for a filter module.
 */
struct MXS_FILTER_OBJECT
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
    MXS_FILTER* (* createInstance)(const char* name, mxs::ConfigParameters* params);
};

/**
 * MXS_FILTER_DEF represents a filter definition from the configuration file.
 * Its exact definition is private to MaxScale.
 */
struct mxs_filter_def;

typedef struct mxs_filter_def
{
} MXS_FILTER_DEF;

/**
 * Get the filter instance of a particular filter definition.
 *
 * @return A filter instance.
 */
MXS_FILTER* filter_def_get_instance(const MXS_FILTER_DEF* filter_def);

/**
 * Get common filter parameters
 *
 * @return An array of filter parameters that are common to all filters
 */
const MXS_MODULE_PARAM* common_filter_params();

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
    FCAP_TYPE_NONE = 0x0    // TODO: remove once filter capabilities are defined
} filter_capability_t;
