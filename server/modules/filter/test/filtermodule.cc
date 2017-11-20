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
#include <maxscale/filtermodule.hh>
#include "../../../core/maxscale/modules.h"

using std::auto_ptr;

namespace maxscale
{

//
// FilterModule
//

const char* FilterModule::zName = MODULE_FILTER;

auto_ptr<FilterModule::Instance> FilterModule::createInstance(const char* zName,
                                                              char** pzOptions,
                                                              MXS_CONFIG_PARAMETER* pParameters)
{
    auto_ptr<Instance> sInstance;

    MXS_FILTER* pFilter = m_pApi->createInstance(zName, pzOptions, pParameters);

    if (pFilter)
    {
        sInstance.reset(new Instance(this, pFilter));
    }

    return sInstance;
}

//
// FilterModule::Instance
//

FilterModule::Instance::Instance(FilterModule* pModule, MXS_FILTER* pInstance)
    : m_module(*pModule)
    , m_pInstance(pInstance)
{
}

FilterModule::Instance::~Instance()
{
    m_module.destroyInstance(m_pInstance);
}

auto_ptr<FilterModule::Session> FilterModule::Instance::newSession(MXS_SESSION* pSession)
{
    auto_ptr<Session> sFilter_session;

    MXS_FILTER_SESSION* pFilter_session = m_module.newSession(m_pInstance, pSession);

    if (pFilter_session)
    {
        sFilter_session.reset(new Session(this, pFilter_session));
    }

    return sFilter_session;
}

//
// FilterModule::Session
//

FilterModule::Session::Session(Instance* pInstance, MXS_FILTER_SESSION* pFilter_session)
    : m_instance(*pInstance)
    , m_pFilter_session(pFilter_session)
{
}

FilterModule::Session::~Session()
{
    m_instance.freeSession(m_pFilter_session);
}

}
