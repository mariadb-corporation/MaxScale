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

#include <maxscale/cppdefs.hh>

#include <new>
#include <string>

#include <maxscale/monitor.h>
#include <maxscale/thread.h>
#include <maxscale/protocol/mysql.h>
#include <mysqld_error.h>

/**
 * The instance of a Group Replication Monitor
 */
struct GRMon : public MXS_SPECIFIC_MONITOR
{
    GRMon(const GRMon&);
    GRMon& operator&(const GRMon&);
public:
    static GRMon* create(MXS_MONITOR* monitor, const MXS_CONFIG_PARAMETER* params);
    void stop();
    ~GRMon();

private:
    THREAD                m_thread;   /**< Monitor thread */
    int                   m_shutdown; /**< Flag to shutdown the monitor thread */
    MXS_MONITORED_SERVER* m_master;   /**< The master server */
    std::string           m_script;
    uint64_t              m_events;   /**< Enabled events */
    MXS_MONITOR*          m_monitor;

    GRMon(MXS_MONITOR* monitor, const MXS_CONFIG_PARAMETER *params);

    void main();
    static void main(void* data);
};

GRMon::GRMon(MXS_MONITOR* monitor, const MXS_CONFIG_PARAMETER* params):
    m_shutdown(0),
    m_master(NULL),
    m_script(config_get_string(params, "script")),
    m_events(config_get_enum(params, "events", mxs_monitor_event_enum_values)),
    m_monitor(monitor)
{
}

GRMon::~GRMon()
{
}

GRMon* GRMon::create(MXS_MONITOR* monitor, const MXS_CONFIG_PARAMETER* params)
{
    GRMon* mon;
    MXS_EXCEPTION_GUARD(mon = new GRMon(monitor, params));

    if (mon && thread_start(&mon->m_thread, GRMon::main, mon, 0) == NULL)
    {
        delete mon;
        mon = NULL;
    }

    return mon;
}

void GRMon::main(void* data)
{
    GRMon* mon = (GRMon*)data;
    mon->main();
}

void GRMon::stop()
{
    atomic_store_int32(&m_shutdown, 1);
    thread_wait(m_thread);
}

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @return A handle to use when interacting with the monitor
 */
static MXS_SPECIFIC_MONITOR *
startMonitor(MXS_MONITOR *mon, const MXS_CONFIG_PARAMETER *params)
{
    return GRMon::create(mon, params);
}

/**
 * Stop a running monitor
 *
 * @param arg   Handle on thr running monior
 */
static void
stopMonitor(MXS_SPECIFIC_MONITOR *mon)
{
    GRMon *handle = static_cast<GRMon*>(mon);
    handle->stop();
    delete handle;
}

static MXS_SPECIFIC_MONITOR* initMonitor(MXS_MONITOR *mon,
                                         const MXS_CONFIG_PARAMETER *params)
{
    ss_dassert(!true);
    return NULL;
}

static void finishMonitor(MXS_SPECIFIC_MONITOR* mon)
{
    ss_dassert(!true);
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param arg   The monitor handle
 */
static void
diagnostics(const MXS_SPECIFIC_MONITOR *mon, DCB *dcb)
{
}

/**
 * Diagnostic interface
 *
 * @param arg   The monitor handle
 */
static json_t* diagnostics_json(const MXS_SPECIFIC_MONITOR *mon)
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

    if (rval != MONITOR_CONN_OK)
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

extern "C"
{
    /**
     * The module entry point routine. It is this routine that
     * must populate the structure that is referred to as the
     * "module object", this is a structure with the set of
     * external entry points for this module.
     *
     * @return The module object
     */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_MONITOR_OBJECT MyObject =
        {
            initMonitor,
            finishMonitor,
            startMonitor,
            stopMonitor,
            diagnostics,
            diagnostics_json
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_MONITOR,
            MXS_MODULE_GA,
            MXS_MONITOR_VERSION,
            "A Group Replication cluster monitor",
            "V1.0.0",
            MXS_NO_MODULE_CAPABILITIES,
            &MyObject,
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

}
