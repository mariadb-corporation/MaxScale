/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "binlogfilter"

#include "binlogfilter.hh"

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char desc[] = "A binlog event filter for slave servers";
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        desc,
        "V1.0.0",
        RCAP_TYPE_STMT_OUTPUT,
        &BinlogFilter::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {"match",             MXS_MODULE_PARAM_REGEX  },
            {"exclude",           MXS_MODULE_PARAM_REGEX  },
            {REWRITE_SRC,         MXS_MODULE_PARAM_REGEX  },
            {REWRITE_DEST,        MXS_MODULE_PARAM_STRING },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

// BinlogFilter constructor
BinlogFilter::BinlogFilter(const mxs::ConfigParameters* pParams)
    : m_config(pParams)
{
}

// BinlogFilter destructor
BinlogFilter::~BinlogFilter()
{
}

// static: filter create routine
BinlogFilter* BinlogFilter::create(const char* zName,
                                   mxs::ConfigParameters* pParams)
{
    BinlogFilter* rval = nullptr;
    auto src = pParams->get_string(REWRITE_SRC);
    auto dest = pParams->get_string(REWRITE_DEST);

    if (src.empty() != dest.empty())
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", REWRITE_SRC, REWRITE_DEST);
    }
    else
    {
        rval = new BinlogFilter(pParams);
    }

    return rval;
}

// BinlogFilterSession create routine
BinlogFilterSession* BinlogFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return BinlogFilterSession::create(pSession, pService, this);
}

// static
json_t* BinlogFilter::diagnostics() const
{
    return NULL;
}

// static
uint64_t BinlogFilter::getCapabilities()
{
    return RCAP_TYPE_STMT_OUTPUT;
}
