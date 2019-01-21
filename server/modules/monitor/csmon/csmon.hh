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

class CsMonitor : public maxscale::MonitorWorkerSimple
{
public:
    CsMonitor(const CsMonitor&) = delete;
    CsMonitor& operator=(const CsMonitor&) = delete;

    ~CsMonitor();
    static CsMonitor* create();

protected:
    bool has_sufficient_permissions() const;
    void update_server_status(MXS_MONITORED_SERVER* monitored_server);

private:
    CsMonitor();
    bool configure(const MXS_CONFIG_PARAMETER* pParams) override;

    SERVER* m_primary;
};
