/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include "maxscale/filtermodule.hh"
#include "../../../core/internal/modules.hh"

using std::auto_ptr;

namespace maxscale
{

//
// FilterModule
//

const char* FilterModule::zName = MODULE_FILTER;

auto_ptr<FilterModule::Instance> FilterModule::createInstance(const char* zName,
                                                              mxs::ConfigParameters* pParameters)
{
    auto_ptr<Instance> sInstance;

    mxs::Filter* pFilter = m_pApi->createInstance(zName, pParameters);

    if (pFilter)
    {
        if (pFilter->getConfiguration()->configure(*pParameters))
        {
            sInstance.reset(new Instance(this, pFilter));
        }
        else
        {
            delete pFilter;
        }
    }

    return sInstance;
}

//
// FilterModule::Instance
//

FilterModule::Instance::Instance(FilterModule* pModule, mxs::Filter* pInstance)
    : m_module(*pModule)
    , m_pInstance(pInstance)
{
}

FilterModule::Instance::~Instance()
{
    m_module.destroyInstance(m_pInstance);
}

auto_ptr<FilterModule::Session> FilterModule::Instance::newSession(MXS_SESSION* pSession,
                                                                   SERVICE* pService,
                                                                   mxs::Routable* down,
                                                                   mxs::Routable* up)
{
    auto_ptr<Session> sFilter_session;

    auto pFilter_session = m_module.newSession(m_pInstance, pSession, pService);

    if (pFilter_session)
    {
        pFilter_session->setDownstream(down);
        pFilter_session->setUpstream(up);
        sFilter_session.reset(new Session(this, pFilter_session));
    }

    return sFilter_session;
}

//
// FilterModule::Session
//

FilterModule::Session::Session(Instance* pInstance, mxs::Routable* pFilter_session)
    : m_instance(*pInstance)
    , m_pFilter_session(pFilter_session)
{
}

FilterModule::Session::~Session()
{
    delete static_cast<mxs::FilterSession*>(m_pFilter_session);
}
}
