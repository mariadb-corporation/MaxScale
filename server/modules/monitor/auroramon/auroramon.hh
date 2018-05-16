#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/monitor.hh>
#include <maxscale/thread.hh>

/**
 * @file auroramon.hh - The Aurora monitor
 */

class AuroraMonitor : public maxscale::MonitorInstance
{
public:
    AuroraMonitor(const AuroraMonitor&) = delete;
    AuroraMonitor& operator = (const AuroraMonitor&) = delete;

    static AuroraMonitor* create(MXS_MONITOR* monitor);
    void destroy();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    AuroraMonitor(MXS_MONITOR* monitor);
    ~AuroraMonitor();

    bool has_sufficient_permissions() const;
    void configure(const MXS_CONFIG_PARAMETER* params);

    void main();
};
