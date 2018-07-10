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

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "commentfilter"

#include "commentfilter.hh"
#include <string>
#include <cstring>

namespace
{

const char CN_INJECT[] = "inject";

}

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
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            { CN_INJECT, MXS_MODULE_PARAM_QUOTEDSTRING, NULL, MXS_MODULE_OPT_REQUIRED },
            { MXS_END_MODULE_PARAMS }
        }
    };

    return &info;
}

CommentFilter::CommentFilter(std::string comment) : m_comment(comment)
{
    MXS_INFO("Comment filter with comment [%s] created.", m_comment.c_str());
}

CommentFilter::~CommentFilter()
{
}

// static
CommentFilter* CommentFilter::create(const char* zName, MXS_CONFIG_PARAMETER* pParams)
{
    return new CommentFilter(config_get_string(pParams, CN_INJECT));
}

CommentFilterSession* CommentFilter::newSession(MXS_SESSION* pSession)
{
    return CommentFilterSession::create(pSession, this);
}

// static
void CommentFilter::diagnostics(DCB* pDcb) const
{
    dcb_printf(pDcb, "Comment filter with comment: %s", m_comment.c_str());
}

// static
json_t* CommentFilter::diagnostics_json() const
{
    json_t* rval = json_object();
    json_object_set_new(rval, "Comment", json_string(m_comment.c_str()));
    return rval;
}

// static
uint64_t CommentFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}
