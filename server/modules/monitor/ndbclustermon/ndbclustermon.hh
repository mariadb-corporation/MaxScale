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
#include <maxscale/thread.h>

/**
 * @file ndbcclustermon.hh A NDBC cluster monitor
 */

class NDBCMonitor : public MXS_MONITOR_INSTANCE
{
public:
    NDBCMonitor(const NDBCMonitor&) = delete;
    NDBCMonitor& operator = (const NDBCMonitor&) = delete;

    static NDBCMonitor* create(MXS_MONITOR* monitor);
    void destroy();
    bool start(const MXS_CONFIG_PARAMETER* param);
    void stop();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    THREAD m_thread;                /**< Monitor thread */
    unsigned long m_id;             /**< Monitor ID */
    uint64_t m_events;              /**< enabled events */
    int m_shutdown;                 /**< Flag to shutdown the monitor thread */
    int m_status;                   /**< Monitor status */
    MXS_MONITORED_SERVER *m_master; /**< Master server for MySQL Master/Slave replication */
    char* m_script;                 /**< Script to call when state changes occur on servers */
    MXS_MONITOR* m_monitor;         /**< Pointer to generic monitor structure */
    bool m_checked;                 /**< Whether server access has been checked */

    NDBCMonitor(MXS_MONITOR* monitor);
    ~NDBCMonitor();

    void main();
    static void main(void* data);
};
