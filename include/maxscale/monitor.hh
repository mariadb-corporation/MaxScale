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
#include <maxscale/monitor.h>

namespace maxscale
{

/**
 * The purpose of the template MonitorApi is to provide an implementation
 * of the monitor C-API. The template is instantiated with a class that
 * provides the actual behaviour of a monitor.
 */
template<class MonitorInstance>
class MonitorApi
{
public:
    MonitorApi() = delete;
    MonitorApi(const MonitorApi&) = delete;
    MonitorApi& operator = (const MonitorApi&) = delete;

    static MXS_MONITOR_INSTANCE* createInstance(MXS_MONITOR* pMonitor)
    {
        MonitorInstance* pInstance = NULL;
        MXS_EXCEPTION_GUARD(pInstance = MonitorInstance::create(pMonitor));
        return pInstance;
    }

    static void destroyInstance(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(MonitorInstance::destroy(static_cast<MonitorInstance*>(pInstance)));
    }

    static bool startMonitor(MXS_MONITOR_INSTANCE* pInstance,
                             const MXS_CONFIG_PARAMETER* pParams)
    {
        bool started = false;
        MXS_EXCEPTION_GUARD(started = static_cast<MonitorInstance*>(pInstance)->start(pParams));
        return started;
    }

    static void stopMonitor(MXS_MONITOR_INSTANCE* pInstance)
    {
        MXS_EXCEPTION_GUARD(static_cast<MonitorInstance*>(pInstance)->stop());
    }

    static void diagnostics(const MXS_MONITOR_INSTANCE* pInstance, DCB* pDcb)
    {
        MXS_EXCEPTION_GUARD(static_cast<const MonitorInstance*>(pInstance)->diagnostics(pDcb));
    }

    static json_t* diagnostics_json(const MXS_MONITOR_INSTANCE* pInstance)
    {
        json_t* pJson = NULL;
        MXS_EXCEPTION_GUARD(pJson = static_cast<const MonitorInstance*>(pInstance)->diagnostics_json());
        return pJson;
    }

    static MXS_MONITOR_API s_api;
};

template<class MonitorInstance>
MXS_MONITOR_API MonitorApi<MonitorInstance>::s_api =
{
    &MonitorApi<MonitorInstance>::createInstance,
    &MonitorApi<MonitorInstance>::destroyInstance,
    &MonitorApi<MonitorInstance>::startMonitor,
    &MonitorApi<MonitorInstance>::stopMonitor,
    &MonitorApi<MonitorInstance>::diagnostics,
    &MonitorApi<MonitorInstance>::diagnostics_json,
};

}
