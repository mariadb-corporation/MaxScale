/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "commentfilter"

#include "commentfilter.hh"
#include <string>
#include <cstring>
#include "commentconfig.hh"

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A comment filter that can inject comments in sql queries",
        "V1.0.0",
        RCAP_TYPE_NONE,
        &CommentFilter::s_object, // This is defined in the MaxScale filter template
        NULL,                     /* Process init. */
        NULL,                     /* Process finish. */
        NULL,                     /* Thread init. */
        NULL,                     /* Thread finish. */
    };

    static bool populated = false;

    if (!populated)
    {
        CommentConfig::populate(info);
        populated = true;
    }

    return &info;
}

CommentFilter::CommentFilter(CommentConfig&& config)
    : m_config(std::move(config))
{
    MXS_INFO("Comment filter with comment [%s] created.", m_config.inject.c_str());
}


CommentFilter::~CommentFilter()
{
}

// static
CommentFilter* CommentFilter::create(const char* zName, mxs::ConfigParameters* pParams)
{
    CommentFilter* filter = nullptr;
    CommentConfig config(zName);

    if (config.configure(*pParams))
    {
        filter = new CommentFilter(std::move(config));
    }

    return filter;
}

CommentFilterSession* CommentFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return CommentFilterSession::create(pSession, pService, this);
}

// static
json_t* CommentFilter::diagnostics() const
{
    json_t* rval = json_object();
    m_config.fill(rval);
    return rval;
}

// static
uint64_t CommentFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}
