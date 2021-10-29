/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <maxscale/filter.hh>
#include <maxbase/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include "mysqlhint.hh"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

// static
HintInstance* HintInstance::create(const char* zName, MXS_CONFIG_PARAMETER* ppParams)
{
    return new(std::nothrow) HintInstance;
}

HintSession* HintInstance::newSession(MXS_SESSION* pSession)
{
    return new(std::nothrow) HintSession(pSession);
}

void HintInstance::diagnostics(DCB* pDcb) const
{
}

json_t* HintInstance::diagnostics_json() const
{
    return nullptr;
}

uint64_t HintInstance::getCapabilities()
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

HintSession::HintSession(MXS_SESSION* session)
    : mxs::FilterSession(session)
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
        MXS_MODULE_API_FILTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_FILTER_VERSION,
        "A hint parsing filter",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &HintInstance::s_object,
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
