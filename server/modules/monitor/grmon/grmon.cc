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
    , m_master(NULL)
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
    m_master = NULL;
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

static void update_server_status(MXS_MONITOR* monitor, MXS_MONITORED_SERVER* server)
{
    /* Don't even probe server flagged as in maintenance */
    if (SERVER_IN_MAINT(server->server))
    {
        return;
    }

    /** Store previous status */
    server->mon_prev_status = server->server->status;

    mxs_connect_result_t rval = mon_ping_or_connect_to_db(monitor, server);

    if (!mon_connection_is_ok(rval))
    {
        if (mysql_errno(server->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(server->server, SERVER_AUTH_ERROR);
        }
        else
        {
            server_clear_status_nolock(server->server, SERVER_AUTH_ERROR);
        }

        server->server->node_id = -1;

        server_clear_status_nolock(server->server, SERVER_RUNNING);

        if (mon_status_changed(server) && mon_print_fail_status(server))
        {
            mon_log_connect_error(server, rval);
        }
    }
    else
    {
        /* If we get this far then we have a working connection */
        server_set_status_nolock(server->server, SERVER_RUNNING);
    }

    if (is_master(server))
    {
        server_set_status_nolock(server->server, SERVER_MASTER);
        server_clear_status_nolock(server->server, SERVER_SLAVE);
    }
    else if (is_slave(server))
    {
        server_set_status_nolock(server->server, SERVER_SLAVE);
        server_clear_status_nolock(server->server, SERVER_MASTER);
    }
    else
    {
        server_clear_status_nolock(server->server, SERVER_SLAVE);
        server_clear_status_nolock(server->server, SERVER_MASTER);
    }
}

void GRMon::main()
{
    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed. Exiting.");
        return;
    }

    load_server_journal(m_monitor, NULL);

    while (!m_shutdown)
    {
        lock_monitor_servers(m_monitor);
        servers_status_pending_to_current(m_monitor);

        for (MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers; ptr; ptr = ptr->next)
        {
            update_server_status(m_monitor, ptr);
        }

        mon_hangup_failed_servers(m_monitor);
        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(m_monitor,
                                  m_script.empty() ? NULL : m_script.c_str(),
                                  m_events);

        servers_status_current_to_pending(m_monitor);
        store_server_journal(m_monitor, NULL);
        release_monitor_servers(m_monitor);

        /** Sleep until the next monitoring interval */
        size_t ms = 0;
        while (ms < m_monitor->interval && !m_shutdown)
        {
            if (m_monitor->server_pending_changes)
            {
                // Admin has changed something, skip sleep
                break;
            }
            thread_millisleep(MXS_MON_BASE_INTERVAL_MS);
            ms += MXS_MON_BASE_INTERVAL_MS;
        }
    }

    mysql_thread_end();
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
