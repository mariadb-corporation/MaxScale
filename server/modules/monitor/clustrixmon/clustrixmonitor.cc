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

#include "clustrixmonitor.hh"


ClustrixMonitor::ClustrixMonitor(MXS_MONITOR* pMonitor)
    : maxscale::MonitorInstance(pMonitor)
{
}

//static
ClustrixMonitor* ClustrixMonitor::create(MXS_MONITOR* pMonitor)
{
    return new ClustrixMonitor(pMonitor);
}

void ClustrixMonitor::tick()
{
}
