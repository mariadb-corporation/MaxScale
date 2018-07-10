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

#include <maxscale/cppdefs.hh>
#include <maxscale/filter.h>

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
        Downstream()
        {
            m_data.instance = NULL;
            m_data.session = NULL;
            m_data.routeQuery = NULL;
        }

        Downstream(const MXS_DOWNSTREAM& down)
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
            return m_data.routeQuery(m_data.instance, m_data.session, pPacket);
        }

        MXS_DOWNSTREAM m_data;
    };

    class Upstream
    {
    public:
        /**
         * @class Upstream
         *
         * An instance of this class represents a component preceeding a filter.
         */
        Upstream()
        {
            m_data.instance = NULL;
            m_data.session = NULL;
            m_data.clientReply = NULL;
        }

        Upstream(const MXS_UPSTREAM& up)
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
        int clientReply(GWBUF* pPacket)
        {
            return m_data.clientReply(m_data.instance, m_data.session, pPacket);
        }

        MXS_UPSTREAM m_data;
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
     */
    int routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The filter should
     * forward the packet to the upstream component.
     *
     * @param pPacket A client packet.
     */
    int clientReply(GWBUF* pPacket);

    /**
     * Called for obtaining diagnostics about the filter session.
     *
     * @param pDcb  The dcb where the diagnostics should be written.
     */
    void diagnostics(DCB *pDcb);

    /**
     * Called for obtaining diagnostics about the filter session.
     */
    json_t* diagnostics_json() const;

protected:
    FilterSession(MXS_SESSION* pSession);

    /**
     * To be called by a filter that short-circuits the request processing.
     * If this function is called (in routeQuery), the filter must return
     * without passing the request further.
     *
     * @param pResponse  The response to be sent to the client.
     */
    void set_response(GWBUF* pResponse) const
    {
        session_set_response(m_pSession, &m_up.m_data, pResponse);
    }

protected:
    MXS_SESSION*   m_pSession; /*< The MXS_SESSION this filter session is associated with. */
    Downstream     m_down;     /*< The downstream component. */
    Upstream       m_up;       /*< The upstream component. */
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
 *      static MyFilter* create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* ppParams);
 *
 *      // This creates a new session for a filter instance
 *      MyFilterSession* newSession(MXS_SESSION* pSession);
 *
 *      // Diagnostic function that prints to a DCB
 *      void diagnostics(DCB* pDcb) const;
 *
 *      // Diagnostic function that returns a JSON object
 *      json_t* diagnostics_json() const;
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
    static MXS_FILTER* createInstance(const char* zName, MXS_CONFIG_PARAMETER* ppParams)
    {
        FilterType* pFilter = NULL;

        MXS_EXCEPTION_GUARD(pFilter = FilterType::create(zName, ppParams));

        return pFilter;
    }

    static MXS_FILTER_SESSION* newSession(MXS_FILTER* pInstance, MXS_SESSION* pSession)
    {
        FilterType* pFilter = static_cast<FilterType*>(pInstance);
        FilterSessionType* pFilterSession = NULL;

        MXS_EXCEPTION_GUARD(pFilterSession = pFilter->newSession(pSession));

        return pFilterSession;
    }

    static void closeSession(MXS_FILTER*, MXS_FILTER_SESSION* pData)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(pFilterSession->close());
    }

    static void freeSession(MXS_FILTER*, MXS_FILTER_SESSION* pData)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        MXS_EXCEPTION_GUARD(delete pFilterSession);
    }

    static void setDownstream(MXS_FILTER*, MXS_FILTER_SESSION* pData, MXS_DOWNSTREAM* pDownstream)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        typename FilterSessionType::Downstream down(*pDownstream);

        MXS_EXCEPTION_GUARD(pFilterSession->setDownstream(down));
    }

    static void setUpstream(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pData, MXS_UPSTREAM* pUpstream)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        typename FilterSessionType::Upstream up(*pUpstream);

        MXS_EXCEPTION_GUARD(pFilterSession->setUpstream(up));
    }

    static int routeQuery(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pData, GWBUF* pPacket)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        int rv = 0;
        MXS_EXCEPTION_GUARD(rv = pFilterSession->routeQuery(pPacket));

        return rv;
    }

    static int clientReply(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pData, GWBUF* pPacket)
    {
        FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

        int rv = 0;
        MXS_EXCEPTION_GUARD(rv = pFilterSession->clientReply(pPacket));

        return rv;
    }

    static void diagnostics(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pData, DCB* pDcb)
    {
        if (pData)
        {
            FilterSessionType* pFilterSession = static_cast<FilterSessionType*>(pData);

            MXS_EXCEPTION_GUARD(pFilterSession->diagnostics(pDcb));
        }
        else
        {
            FilterType* pFilter = static_cast<FilterType*>(pInstance);

            MXS_EXCEPTION_GUARD(pFilter->diagnostics(pDcb));
        }
    }

    static json_t* diagnostics_json(const MXS_FILTER* pInstance, const MXS_FILTER_SESSION* pData)
    {
        json_t* rval = NULL;

        if (pData)
        {
            const FilterSessionType* pFilterSession = static_cast<const FilterSessionType*>(pData);

            MXS_EXCEPTION_GUARD(rval = pFilterSession->diagnostics_json());
        }
        else
        {
            const FilterType* pFilter = static_cast<const FilterType*>(pInstance);

            MXS_EXCEPTION_GUARD(rval = pFilter->diagnostics_json());
        }

        return rval;
    }

    static uint64_t getCapabilities(MXS_FILTER* pInstance)
    {
        uint64_t rv = 0;

        FilterType* pFilter = static_cast<FilterType*>(pInstance);

        MXS_EXCEPTION_GUARD(rv = pFilter->getCapabilities());

        return rv;
    }

    static void destroyInstance(MXS_FILTER* pInstance)
    {
        FilterType* pFilter = static_cast<FilterType*>(pInstance);

        MXS_EXCEPTION_GUARD(delete pFilter);
    }

    static MXS_FILTER_OBJECT s_object;
};


template<class FilterType, class FilterSessionType>
MXS_FILTER_OBJECT Filter<FilterType, FilterSessionType>::s_object =
{
    &Filter<FilterType, FilterSessionType>::createInstance,
    &Filter<FilterType, FilterSessionType>::newSession,
    &Filter<FilterType, FilterSessionType>::closeSession,
    &Filter<FilterType, FilterSessionType>::freeSession,
    &Filter<FilterType, FilterSessionType>::setDownstream,
    &Filter<FilterType, FilterSessionType>::setUpstream,
    &Filter<FilterType, FilterSessionType>::routeQuery,
    &Filter<FilterType, FilterSessionType>::clientReply,
    &Filter<FilterType, FilterSessionType>::diagnostics,
    &Filter<FilterType, FilterSessionType>::diagnostics_json,
    &Filter<FilterType, FilterSessionType>::getCapabilities,
    &Filter<FilterType, FilterSessionType>::destroyInstance,
};

}
