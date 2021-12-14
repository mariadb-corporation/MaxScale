/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file A MySQL Group Replication cluster monitor
 */

#define MXS_MODULE_NAME "grmon"

#include "grmon.hh"

#include <new>
#include <string>
#include <mysql.h>
#include <maxscale/protocol/mariadb/mysql.hh>

using maxscale::MonitorServer;

GRMon::GRMon(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
{
}

GRMon::~GRMon()
{
}

GRMon* GRMon::create(const std::string& name, const std::string& module)
{
    return new GRMon(name, module);
}

bool GRMon::has_sufficient_permissions()
{
    return true;
}

static inline bool is_false(const char* value)
{
    return strcasecmp(value, "0") == 0
           || strcasecmp(value, "no") == 0
           || strcasecmp(value, "off") == 0
           || strcasecmp(value, "false") == 0;
}

static bool is_master(MonitorServer* server)
{
    bool rval = false;
    MYSQL_RES* result;
    const char* master_query =
        "SELECT VARIABLE_VALUE, @@server_uuid, @@read_only FROM performance_schema.global_status "
        "WHERE VARIABLE_NAME= 'group_replication_primary_member'";

    if (mysql_query(server->con, master_query) == 0 && (result = mysql_store_result(server->con)))
    {
        for (MYSQL_ROW row = mysql_fetch_row(result); row; row = mysql_fetch_row(result))
        {
            if (strcasecmp(row[0], row[1]) == 0 && is_false(row[2]))
            {
                rval = true;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        server->mon_report_query_error();
    }

    return rval;
}

static bool is_slave(MonitorServer* server)
{
    bool rval = false;
    MYSQL_RES* result;
    const char slave_query[] = "SELECT MEMBER_STATE FROM "
                               "performance_schema.replication_group_members "
                               "WHERE MEMBER_ID = @@server_uuid";

    if (mysql_query(server->con, slave_query) == 0 && (result = mysql_store_result(server->con)))
    {
        for (MYSQL_ROW row = mysql_fetch_row(result); row; row = mysql_fetch_row(result))
        {
            if (strcasecmp(row[0], "ONLINE") == 0)
            {
                rval = true;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        server->mon_report_query_error();
    }

    return rval;
}

void GRMon::update_server_status(MonitorServer* monitored_server)
{
    if (is_master(monitored_server))
    {
        monitored_server->set_pending_status(SERVER_MASTER);
        monitored_server->clear_pending_status(SERVER_SLAVE);
    }
    else if (is_slave(monitored_server))
    {
        monitored_server->set_pending_status(SERVER_SLAVE);
        monitored_server->clear_pending_status(SERVER_MASTER);
    }
    else
    {
        monitored_server->clear_pending_status(SERVER_SLAVE);
        monitored_server->clear_pending_status(SERVER_MASTER);
    }
}

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
    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_GA,
        MXS_MONITOR_VERSION,
        "A Group Replication cluster monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<GRMon>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
