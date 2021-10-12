/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <maxscale/filter.hh>
#include "module.hh"

namespace maxscale
{

/**
 * An instance of FilterModule represents a filter module.
 */
class FilterModule : public SpecificModule<FilterModule, MXS_FILTER_OBJECT>
{
    FilterModule(const FilterModule&);
    FilterModule& operator=(const FilterModule&);

public:
    static const char* zName;   /*< The name describing the module type. */

    class Session;
    class Instance
    {
        Instance(const Instance&);
        Instance& operator=(const Instance&);
    public:
        ~Instance();

        /**
         * Create a new filter session.
         *
         * @param pSession  The session to which the filter session belongs.
         *
         * @return A new filter session or NULL if the creation failed.
         */
        std::auto_ptr<Session> newSession(MXS_SESSION* pSession, SERVICE* pService,
                                          mxs::Downstream* pDown, mxs::Upstream* pUp);

    private:
        friend class FilterModule;

        Instance(FilterModule* pModule, MXS_FILTER* pInstance);

    private:
        friend class Session;

        void freeSession(MXS_FILTER_SESSION* pFilter_session)
        {
            m_module.freeSession(m_pInstance, pFilter_session);
        }

        int routeQuery(MXS_FILTER_SESSION* pFilter_session, GWBUF* pStatement)
        {
            return m_module.routeQuery(m_pInstance, pFilter_session, pStatement);
        }

        int clientReply(MXS_FILTER_SESSION* pFilter_session, GWBUF* pStatement, const mxs::Reply& reply)
        {
            return m_module.clientReply(m_pInstance, pFilter_session, pStatement, reply);
        }

    private:
        FilterModule& m_module;
        MXS_FILTER*   m_pInstance;
    };

    class Session
    {
        Session(const Session&);
        Session& operator=(const Session&);

    public:
        ~Session();

        /**
         * The following member functions correspond to the MaxScale filter API.
         */
        int routeQuery(GWBUF* pStatement)
        {
            return m_instance.routeQuery(m_pFilter_session, pStatement);
        }

        int clientReply(GWBUF* pBuffer, const mxs::Reply& reply)
        {
            return m_instance.clientReply(m_pFilter_session, pBuffer, reply);
        }

    private:
        friend class Instance;

        Session(Instance* pInstance, MXS_FILTER_SESSION* pFilter_session);

    private:
        Instance&           m_instance;
        MXS_FILTER_SESSION* m_pFilter_session;
    };

    /**
     * Create a new instance.
     *
     * @param zName        The name of the instance (config file section name),
     * @param pzOptions    Optional options.
     * @param pParameters  Configuration parameters.
     *
     * @return A new instance or NULL if creation failed.
     */
    std::auto_ptr<Instance> createInstance(const char* zName, mxs::ConfigParameters* pParameters);

private:
    friend class Instance;

    void destroyInstance(MXS_FILTER* pInstance)
    {
        m_pApi->destroyInstance(pInstance);
    }

    MXS_FILTER_SESSION* newSession(MXS_FILTER* pInstance,
                                   MXS_SESSION* pSession,
                                   SERVICE* pService,
                                   mxs::Downstream* pDown,
                                   mxs::Upstream* pUp)
    {
        return m_pApi->newSession(pInstance, pSession, pService, pDown, pUp);
    }

    void freeSession(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pFilter_session)
    {
        m_pApi->freeSession(pInstance, pFilter_session);
    }

    int routeQuery(MXS_FILTER* pInstance, MXS_FILTER_SESSION* pFilter_session, GWBUF* pStatement)
    {
        return m_pApi->routeQuery(pInstance, pFilter_session, pStatement);
    }

    int clientReply(MXS_FILTER* pInstance,
                    MXS_FILTER_SESSION* pFilter_session,
                    GWBUF* pStatement,
                    const mxs::Reply& reply)
    {
        mxs::ReplyRoute route;
        return m_pApi->clientReply(pInstance, pFilter_session, pStatement, route, reply);
    }

private:
    // Not accepted by CentOS6: friend Base;
    friend class SpecificModule<FilterModule, MXS_FILTER_OBJECT>;

    FilterModule(const MXS_MODULE* pModule)
        : SpecificModule<FilterModule, MXS_FILTER_OBJECT>(pModule)
    {
    }
};
}
