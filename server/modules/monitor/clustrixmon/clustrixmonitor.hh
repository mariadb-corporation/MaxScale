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
#pragma once

#include "clustrixmon.hh"
#include <maxscale/monitor.hh>

class ClustrixMonitor : public maxscale::MonitorInstance
{
    ClustrixMonitor(const ClustrixMonitor&) = delete;
    ClustrixMonitor& operator=(const ClustrixMonitor&) = delete;
public:

    static ClustrixMonitor* create(MXS_MONITOR* pMonitor);

private:
    ClustrixMonitor(MXS_MONITOR* pMonitor);

    void tick();
};
