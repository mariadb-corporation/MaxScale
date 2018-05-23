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

/**
 * @file A MySQL Group Replication cluster monitor
 */

#define MXS_MODULE_NAME "grmon"

#include "grmon.hh"

#include <new>
#include <string>

#include <maxscale/protocol/mysql.h>
#include <mysqld_error.h>


GRMon::GRMon(MXS_MONITOR* monitor)
    : MonitorInstance(monitor)
{
}

GRMon::~GRMon()
{
}

GRMon* GRMon::create(MXS_MONITOR* monitor)
{
    return new GRMon(monitor);
}

void GRMon::destroy()
{
    delete this;
}

bool GRMon::has_sufficient_permissions() const
{
    return true;
}

void GRMon::configure(const MXS_CONFIG_PARAMETER* params)
{
}

void GRMon::diagnostics(DCB* dcb) const
{
}

json_t* GRMon::diagnostics_json() const
{
    return NULL;
}

static inline bool is_false(const char* value)
{
    return strcasecmp(value, "0") == 0 ||
           strcasecmp(value, "no") == 0 ||
           strcasecmp(value, "off") == 0 ||
           strcasecmp(value, "false") == 0;
}

static bool is_master(MXS_MONITORED_SERVER* server)
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
        mon_report_query_error(server);
    }

    return rval;
}

static bool is_slave(MXS_MONITORED_SERVER* server)
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
        mon_report_query_error(server);
    }

    return rval;
}

void GRMon::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    /* Don't even probe server flagged as in maintenance */
    if (SERVER_IN_MAINT(monitored_server->server))
    {
        return;
    }

    /** Store previous status */
    monitored_server->mon_prev_status = monitored_server->server->status;

    mxs_connect_result_t rval = mon_ping_or_connect_to_db(m_monitor, monitored_server);

    if (!mon_connection_is_ok(rval))
    {
        if (mysql_errno(monitored_server->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(monitored_server->server, SERVER_AUTH_ERROR);
        }
        else
        {
            server_clear_status_nolock(monitored_server->server, SERVER_AUTH_ERROR);
        }

        monitored_server->server->node_id = -1;

        server_clear_status_nolock(monitored_server->server, SERVER_RUNNING);

        if (mon_status_changed(monitored_server) && mon_print_fail_status(monitored_server))
        {
            mon_log_connect_error(monitored_server, rval);
        }
    }
    else
    {
        /* If we get this far then we have a working connection */
        server_set_status_nolock(monitored_server->server, SERVER_RUNNING);
    }

    if (is_master(monitored_server))
    {
        server_set_status_nolock(monitored_server->server, SERVER_MASTER);
        server_clear_status_nolock(monitored_server->server, SERVER_SLAVE);
    }
    else if (is_slave(monitored_server))
    {
        server_set_status_nolock(monitored_server->server, SERVER_SLAVE);
        server_clear_status_nolock(monitored_server->server, SERVER_MASTER);
    }
    else
    {
        server_clear_status_nolock(monitored_server->server, SERVER_SLAVE);
        server_clear_status_nolock(monitored_server->server, SERVER_MASTER);
    }
}

void GRMon::tick()
{
    for (MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers; ptr; ptr = ptr->next)
    {
        update_server_status(ptr);
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
            NULL, /* Process init. */
            NULL, /* Process finish. */
            NULL, /* Thread init. */
            NULL, /* Thread finish. */
            {
                {
                    "script",
                    MXS_MODULE_PARAM_PATH,
                    NULL,
                    MXS_MODULE_OPT_PATH_X_OK
                },
                {
                    "events",
                    MXS_MODULE_PARAM_ENUM,
                    MXS_MONITOR_EVENT_DEFAULT_VALUE,
                    MXS_MODULE_OPT_NONE,
                    mxs_monitor_event_enum_values
                },
                {MXS_END_MODULE_PARAMS}
            }
        };

    return &info;
}
