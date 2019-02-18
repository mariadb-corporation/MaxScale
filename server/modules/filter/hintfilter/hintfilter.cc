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

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <maxscale/filter.hh>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include "mysqlhint.hh"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

// static
HINT_INSTANCE* HINT_INSTANCE::create(const char* zName, MXS_CONFIG_PARAMETER* ppParams)
{
    return new(std::nothrow) HINT_INSTANCE;
}

HINT_SESSION* HINT_INSTANCE::newSession(MXS_SESSION* pSession)
{
    return new(std::nothrow) HINT_SESSION(pSession);
}

void HINT_INSTANCE::diagnostics(DCB* pDcb) const
{
}

json_t* HINT_INSTANCE::diagnostics_json() const
{
    return nullptr;
}

uint64_t HINT_INSTANCE::getCapabilities()
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

HINT_SESSION::HINT_SESSION(MXS_SESSION* session)
    : mxs::FilterSession(session)
{
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
HINT_SESSION::~HINT_SESSION()
{
    for (auto& a : named_hints)
    {
        hint_free(a.second);
    }

    for (auto& a : stack)
    {
        hint_free(a);
    }
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
int HINT_SESSION::routeQuery(GWBUF* queue)
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
            &HINT_INSTANCE::s_object,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}
