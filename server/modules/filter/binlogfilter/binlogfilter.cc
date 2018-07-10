/*
 * Copyright (c) 2017 MariaDB Corporation Ab
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
#define MXS_MODULE_NAME "binlogfilter"

#include "binlogfilter.hh"

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A binlog event filter for slave servers",
        "V1.0.0",
        RCAP_TYPE_NONE,
        &BinlogFilter::s_object, // This is defined in the MaxScale filter template
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"filter_events", MXS_MODULE_PARAM_BOOL, "false"},
            {"skip_table", MXS_MODULE_PARAM_STRING, ""},
            {"skip_db", MXS_MODULE_PARAM_STRING, ""},
            { MXS_END_MODULE_PARAMS }
        }
    };

    return &info;
}

// BinlogFilter constructor
BinlogFilter::BinlogFilter(const MXS_CONFIG_PARAMETER* pParams)
    : m_config(pParams)
{
}

// BinlogFilter destructor
BinlogFilter::~BinlogFilter()
{
}

// static: filter create routine
BinlogFilter* BinlogFilter::create(const char* zName,
                                   MXS_CONFIG_PARAMETER* pParams)
{
    return new BinlogFilter(pParams);
}

// BinlogFilterSession create routine
BinlogFilterSession* BinlogFilter::newSession(MXS_SESSION* pSession)
{
    return BinlogFilterSession::create(pSession, this);
}

// static
void BinlogFilter::diagnostics(DCB* pDcb) const
{
}

// static
json_t* BinlogFilter::diagnostics_json() const
{
    return NULL;
}

// static
uint64_t BinlogFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}

// Return BinlogFilter active state
bool BinlogFilter::is_active() const
{
    return m_config.active;
}
