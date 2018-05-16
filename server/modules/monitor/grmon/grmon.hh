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
 * @file grmon.hh A MySQL Group Replication cluster monitor
 */

class GRMon : public maxscale::MonitorInstance
{
public:
    GRMon(const GRMon&) = delete;
    GRMon& operator&(const GRMon&) = delete;

    static GRMon* create(MXS_MONITOR* monitor);
    void destroy();
    bool start(const MXS_CONFIG_PARAMETER* params);
    void stop();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    MXS_MONITORED_SERVER* m_master;   /**< The master server */
    std::string           m_script;

    GRMon(MXS_MONITOR* monitor);
    ~GRMon();

    void main();
    static void main(void* data);
};
