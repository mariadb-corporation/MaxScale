/*
 * Copyright (c) 2017 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        desc,
        "V1.0.0",
        RCAP_TYPE_STMT_OUTPUT,
        &mxs::FilterApi<BinlogFilter>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        BinlogConfig::specification()
    };

    return &info;
}


// BinlogFilter constructor
BinlogFilter::BinlogFilter(const char* name)
    : m_config(name)
{
}

// BinlogFilter destructor
BinlogFilter::~BinlogFilter()
{
}

// static: filter create routine
BinlogFilter* BinlogFilter::create(const char* zName)
{
    return new BinlogFilter(zName);
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
uint64_t BinlogFilter::getCapabilities() const
{
    return RCAP_TYPE_STMT_OUTPUT;
}
