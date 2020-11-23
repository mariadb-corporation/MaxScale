/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <maxscale/filter.hh>
#include <maxbase/alloc.h>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include "mysqlhint.hh"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

// static
HintInstance* HintInstance::create(const char* zName, mxs::ConfigParameters* ppParams)
{
    return new(std::nothrow) HintInstance;
}

mxs::FilterSession* HintInstance::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return new(std::nothrow) HintSession(pSession, pService);
}

json_t* HintInstance::diagnostics() const
{
    return nullptr;
}

uint64_t HintInstance::getCapabilities() const
{
    return RCAP_TYPE_STMT_INPUT;
}

mxs::config::Configuration* HintInstance::getConfiguration()
{
    return nullptr;
}

HintSession::HintSession(MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
{
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
int HintSession::routeQuery(GWBUF* queue)
{
    if (modutil_is_SQL(queue) && gwbuf_length(queue) > 5)
    {
        process_hints(queue);
    }

    return mxs::FilterSession::routeQuery(queue);
}

extern "C"
{

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::ALPHA,
        MXS_FILTER_VERSION,
        "A hint parsing filter",
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<HintInstance>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}
