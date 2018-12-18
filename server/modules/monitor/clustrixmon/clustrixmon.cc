/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "clustrixmon.hh"
#include <maxscale/modinfo.h>
#include "clustrixmonitor.hh"

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MariaDB Clustrix Monitor module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_GA,
        MXS_MONITOR_VERSION,
        "A Clustrix cluster monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<ClustrixMonitor>::s_api,
        NULL,                                       /* Process init. */
        NULL,                                       /* Process finish. */
        NULL,                                       /* Thread init. */
        NULL,                                       /* Thread finish. */
        {
            {
                CLUSTER_MONITOR_INTERVAL_NAME,
                MXS_MODULE_PARAM_COUNT,
                DEFAULT_CLUSTER_MONITOR_INTERVAL_ZVALUE
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
