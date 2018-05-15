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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/monitor.hh>
#include <maxscale/thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxscale/log_manager.h>
#include <maxscale/secrets.h>
#include <maxscale/dcb.h>
#include <maxscale/modinfo.h>
#include <maxscale/config.h>

/**
 * @file mmmon.h - The Multi-Master monitor
 */

/**
 * The handle for an instance of a Multi-Master Monitor module
 */
class MMMonitor : public MXS_MONITOR_INSTANCE
{
public:
    MMMonitor(const MMMonitor&) = delete;
    MMMonitor& operator = (const MMMonitor&) = delete;

    static MMMonitor* create(MXS_MONITOR* monitor);
    void destroy();
    bool start(const MXS_CONFIG_PARAMETER* param);
    void stop();
    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;

private:
    THREAD m_thread;                /**< Monitor thread */
    int m_shutdown;                 /**< Flag to shutdown the monitor thread */
    int m_status;                   /**< Monitor status */
    unsigned long m_id;             /**< Monitor ID */
    int m_detectStaleMaster;        /**< Monitor flag for Stale Master detection */
    MXS_MONITORED_SERVER *m_master; /**< Master server for Master/Slave replication */
    char* m_script;                 /**< Script to call when state changes occur on servers */
    uint64_t m_events;              /**< enabled events */
    MXS_MONITOR* m_monitor;         /**< Pointer to generic monitor structure */
    bool m_checked;                 /**< Whether server access has been checked */

    MMMonitor(MXS_MONITOR* monitor);
    ~MMMonitor();

    MXS_MONITORED_SERVER *get_current_master();

    void main();
    static void main(void* data);
};
