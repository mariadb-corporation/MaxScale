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

class AuroraMonitor : public MXS_MONITOR_INSTANCE
{
public:
    AuroraMonitor(const AuroraMonitor&) = delete;
    AuroraMonitor& operator = (const AuroraMonitor&) = delete;

    static AuroraMonitor* create(MXS_MONITOR* monitor);
    void destroy();
    bool start(const MXS_CONFIG_PARAMETER* param);
    void stop();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    bool         m_shutdown;      /**< True if the monitor is stopped */
    THREAD       m_thread;        /**< Monitor thread */
    char*        m_script;        /**< Launchable script */
    uint64_t     m_events;        /**< Enabled monitor events */
    MXS_MONITOR* m_monitor;       /**< Pointer to generic monitor structure */
    bool         m_checked;       /**< Whether server access has been checked */

    AuroraMonitor(MXS_MONITOR* monitor);
    ~AuroraMonitor();

    void main();
    static void main(void* data);
};
