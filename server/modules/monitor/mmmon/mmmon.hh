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

#include <maxscale/ccdefs.hh>
#include <maxscale/monitor.hh>

/**
 * @file mmmon.hh - The Multi-Master monitor
 */

class MMMonitor : public maxscale::MonitorWorkerSimple
{
public:
    MMMonitor(const MMMonitor&) = delete;
    MMMonitor& operator=(const MMMonitor&) = delete;

    ~MMMonitor();
    static MMMonitor* create();
    void              diagnostics(DCB* dcb) const;
    json_t*           diagnostics_json() const;

protected:
    bool configure(const MXS_CONFIG_PARAMETER* params);
    bool has_sufficient_permissions() const;
    void update_server_status(MXS_MONITORED_SERVER* monitored_server);
    void post_tick();

private:
    int           m_detectStaleMaster;  /**< Monitor flag for Stale Master detection */

    MMMonitor();

    MXS_MONITORED_SERVER* get_current_master();
};
