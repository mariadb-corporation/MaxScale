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
 * @file mysql_mon.c - A MySQL replication cluster monitor
 */

#define MXS_MODULE_NAME "mysqlmon"

#include "../mysqlmon.h"
#include <string>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/debug.h>
#include <maxscale/modulecmd.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/utils.h>
// TODO: For monitorAddParameters
#include "../../../core/maxscale/monitor.h"

/** Column positions for SHOW SLAVE STATUS */
#define MYSQL55_STATUS_BINLOG_POS 5
#define MYSQL55_STATUS_BINLOG_NAME 6
#define MYSQL55_STATUS_IO_RUNNING 10
#define MYSQL55_STATUS_SQL_RUNNING 11
#define MYSQL55_STATUS_MASTER_ID 39

/** Column positions for SHOW SLAVE STATUS */
#define MARIA10_STATUS_BINLOG_NAME 7
#define MARIA10_STATUS_BINLOG_POS 8
#define MARIA10_STATUS_IO_RUNNING 12
#define MARIA10_STATUS_SQL_RUNNING 13
#define MARIA10_STATUS_MASTER_ID 41

/** Column positions for SHOW SLAVE HOSTS */
#define SLAVE_HOSTS_SERVER_ID 0
#define SLAVE_HOSTS_HOSTNAME 1
#define SLAVE_HOSTS_PORT 2

static void monitorMain(void *);

static void *startMonitor(MXS_MONITOR *, const MXS_CONFIG_PARAMETER*);
static void stopMonitor(MXS_MONITOR *);
static bool stop_monitor(MXS_MONITOR *);
static void diagnostics(DCB *, const MXS_MONITOR *);
static json_t* diagnostics_json(const MXS_MONITOR *);
static MXS_MONITORED_SERVER *getServerByNodeId(MXS_MONITORED_SERVER *, long);
static MXS_MONITORED_SERVER *getSlaveOfNodeId(MXS_MONITORED_SERVER *, long);
static MXS_MONITORED_SERVER *get_replication_tree(MXS_MONITOR *, int);
static void set_master_heartbeat(MYSQL_MONITOR *, MXS_MONITORED_SERVER *);
static void set_slave_heartbeat(MXS_MONITOR *, MXS_MONITORED_SERVER *);
static int add_slave_to_master(long *, int, long);
static bool isMySQLEvent(mxs_monitor_event_t event);
void check_maxscale_schema_replication(MXS_MONITOR *monitor);
static bool report_version_err = true;
static const char* hb_table_name = "maxscale_schema.replication_heartbeat";

static const char CN_FAILOVER[]           = "failover";
static const char CN_FAILOVER_TIMEOUT[]   = "failover_timeout";
static const char CN_SWITCHOVER[]         = "switchover";
static const char CN_SWITCHOVER_TIMEOUT[] = "switchover_timeout";

/** Default failover timeout */
#define DEFAULT_FAILOVER_TIMEOUT "90"
/** Default switchover timeout */
#define DEFAULT_SWITCHOVER_TIMEOUT "90"

/**
 * Check whether specified current master is acceptable.
 *
 * @param current_master        The specified current master.
 * @param server                The server to check against.
 * @param result                Result object, for adding error information.
 * @param current_master_found  On output, true if @server is @c current_master.
 *
 * @return False, if there is some error with the specified current master,
 *         True otherwise.
 */
bool mysql_switchover_check_current(SERVER* current_master,
                                    SERVER* server,
                                    json_t* result,
                                    bool* current_master_found)
{
    bool rv = true;
    bool is_master = SERVER_IS_MASTER(server);

    if (current_master == server)
    {
        *current_master_found = true;

        if (!is_master)
        {
            std::string s;
            s += "Specified current master ";
            s += current_master->unique_name;
            s += " is a server, but it is not the current master.";

            json_t* error = json_string(s.c_str());
            if (error)
            {
                json_object_set_new(result, "error", error);
            }

            rv = false;
        }
    }
    else if (is_master)
    {
        std::string s;
        s += "Current master not specified, although there is a master, ";
        s += server->unique_name;
        s += ".";

        json_t* error = json_string(s.c_str());
        if (error)
        {
            json_object_set_new(result, "error", error);
        }

        rv = false;
    }

    return rv;
}

/**
 * Check whether specified new master is acceptable.
 *
 * @param new_master        The specified new master.
 * @param server            The server to check against.
 * @param result            Result object, for adding error information.
 * @param new_master_found  On output, true if the @c server is @c new_master.
 *
 * @return False, if there is some error with the specified current master,
 *         True otherwise.
 */
bool mysql_switchover_check_new(SERVER* new_master,
                                SERVER* server,
                                json_t* result,
                                bool* new_master_found)
{
    bool rv = true;
    bool is_master = SERVER_IS_MASTER(server);

    if (new_master == server)
    {
        *new_master_found = true;

        if (is_master)
        {
            std::string s;
            s += "Specified new master ";
            s += new_master->unique_name;
            s += " is already master.";

            json_t* error = json_string(s.c_str());
            if (error)
            {
                json_object_set_new(result, "error", error);
            }

            rv = false;
        }
    }

    return rv;
}

/**
 * Check whether specified current and new master are acceptable.
 *
 * @param new_master        The specified new master.
 * @param server            The server to check against.
 * @param result            Result object, for adding error information.
 * @param new_master_found  On output, true if the @c server is @c new_master.
 *
 * @return False, if there is some error with the specified current master,
 *         True otherwise.
 */
bool mysql_switchover_check(MXS_MONITOR* mon, SERVER* new_master, SERVER* current_master, json_t* result)
{
    bool rv = true;

    bool current_master_found = false;
    bool new_master_found = false;

    // TODO: Is locking needed here?
    MXS_MONITORED_SERVER* monitored_server = mon->monitored_servers;

    while (rv && !current_master_found && !new_master_found && monitored_server)
    {
        SERVER* server = monitored_server->server;

        if (!current_master_found)
        {
            rv = mysql_switchover_check_current(current_master, server, result, &current_master_found);
        }

        if (rv)
        {
            rv = mysql_switchover_check_new(new_master, server, result, &new_master_found);
        }

        monitored_server = monitored_server->next;
    }

    if (rv && ((current_master && !current_master_found) || !new_master_found))
    {
        std::string s;

        if (current_master && !current_master_found)
        {
            s += "Current master ";
            s += current_master->unique_name;
            s += " specified, but not found amongst existing servers. ";
        }

        if (!new_master_found)
        {
            s += "Specified new master ";
            s += new_master->unique_name;
            s += " not found amongst existing servers.";
        }

        json_t* error = json_string(s.c_str());
        if (error)
        {
            json_object_set_new(result, "error", error);
        }

        rv = false;
    }

    return rv;
}

bool mysql_switchover_perform(MXS_MONITOR* mon, SERVER* new_master, SERVER* current_master, json_t* result)
{
    // TODO: Launch actual switchover command.

    std::string s;
    s += "Performing switchover ";
    if (current_master)
    {
        s += "from ";
        s += current_master ? current_master->unique_name : "(none)";
        s += " ";
    }
    s += "to ";
    s += new_master->unique_name;
    s += ".";

    json_t* data = json_string(s.c_str());

    json_object_set_new(result, "data", data);

    return true;
}

/**
 * Handle switchover
 *
 * @mon             The monitor.
 * @new_master      The specified new master.
 * @current_master  The specified current master.
 * @output          Pointer where to place output object.
 *
 * @return True, if switchover was performed, false otherwise.
 */
bool mysql_switchover(MXS_MONITOR* mon, SERVER* new_master, SERVER* current_master, json_t** output)
{
    bool rv = true;

    MYSQL_MONITOR *handle = static_cast<MYSQL_MONITOR*>(mon->handle);

    *output = NULL;

    json_t* result = json_object();
    if (result)
    {
        *output = result;

        bool stopped = stop_monitor(mon);

        if (stopped)
        {
            MXS_NOTICE("Stopped the monitor %s for the duration of switchover.", mon->name);
        }
        else
        {
            MXS_NOTICE("Monitor %s already stopped, switchover can proceed.", mon->name);
        }

        rv = mysql_switchover_check(mon, new_master, current_master, result);

        if (rv)
        {
            bool failover = config_get_bool(mon->parameters, CN_FAILOVER);

            rv = mysql_switchover_perform(mon, new_master, current_master, result);

            if (rv)
            {
                MXS_NOTICE("Switchover %s -> %s performed.",
                           current_master->unique_name ? current_master->unique_name : "(none)",
                           new_master->unique_name);

                if (stopped)
                {
                    startMonitor(mon, mon->parameters);
                }
            }
            else
            {
                if (failover)
                {
                    // TODO: There could be a more convenient way for this.
                    MXS_CONFIG_PARAMETER p = {};
                    p.name = const_cast<char*>(CN_FAILOVER);
                    p.value = const_cast<char*>("false");

                    monitorAddParameters(mon, &p);

                    MXS_ALERT("Switchover %s -> %s failed, failover has been disabled.",
                              current_master->unique_name ? current_master->unique_name : "(none)",
                              new_master->unique_name);
                }
                else
                {
                    MXS_ERROR("Switchover %s -> %s failed.",
                              current_master->unique_name ? current_master->unique_name : "(none)",
                              new_master->unique_name);
                }
            }
        }
        else
        {
            if (stopped)
            {
                startMonitor(mon, mon->parameters);
            }
        }
    }
    else
    {
        rv = false;
    }

    return rv;
}

/**
 * Command handler for 'switchover'
 *
 * @param args    The provided arguments.
 * @param output  Pointer where to place output object.
 *
 * @return True, if the command was executed, false otherwise.
 */
bool mysql_handle_switchover(const MODULECMD_ARG* args, json_t** output)
{
    ss_dassert(args->argc == 3);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);
    ss_dassert((MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER) ||
               (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_NONE));

    MXS_MONITOR* mon = args->argv[0].value.monitor;
    SERVER* new_master = args->argv[1].value.server;
    SERVER* current_master =
        (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER) ?
        args->argv[2].value.server : NULL;

    return mysql_switchover(mon, new_master, current_master, output);
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C"
{

MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MySQL Monitor module.");

    static modulecmd_arg_type_t switchover_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "MySQL Monitor name (from configuration file)"
        },
        { MODULECMD_ARG_SERVER,  "New master" },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Current master (obligatory if exists)" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "switchover", MODULECMD_TYPE_ACTIVE,
                               mysql_handle_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Perform master switchover");

    static MXS_MONITOR_OBJECT MyObject =
    {
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
        "A MySQL Master/Slave replication monitor",
        "V1.5.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"detect_replication_lag", MXS_MODULE_PARAM_BOOL, "false"},
            {"detect_stale_master", MXS_MODULE_PARAM_BOOL, "true"},
            {"detect_stale_slave",  MXS_MODULE_PARAM_BOOL, "true"},
            {"mysql51_replication", MXS_MODULE_PARAM_BOOL, "false"},
            {"multimaster", MXS_MODULE_PARAM_BOOL, "false"},
            {"detect_standalone_master", MXS_MODULE_PARAM_BOOL, "false"},
            {"failcount", MXS_MODULE_PARAM_COUNT, "5"},
            {"allow_cluster_recovery", MXS_MODULE_PARAM_BOOL, "true"},
            {"allow_external_slaves", MXS_MODULE_PARAM_BOOL, "true"},
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
            {CN_FAILOVER, MXS_MODULE_PARAM_BOOL, "false"},
            {CN_FAILOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, DEFAULT_FAILOVER_TIMEOUT},
            {CN_SWITCHOVER, MXS_MODULE_PARAM_BOOL, "false"},
            {CN_SWITCHOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, DEFAULT_SWITCHOVER_TIMEOUT},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

}

/**
 * Monitor specific information about a server
 */
typedef struct mysql_server_info
{
    int              server_id; /**< Value of @@server_id */
    int              master_id; /**< Master server id from SHOW SLAVE STATUS*/
    int              group;     /**< Multi-master group where this server
                                   belongs, 0 for servers not in groups */
    bool             read_only; /**< Value of @@read_only */
    bool             slave_configured; /**< Whether SHOW SLAVE STATUS returned rows */
    bool             slave_io;  /**< If Slave IO thread is running  */
    bool             slave_sql; /**< If Slave SQL thread is running */
    uint64_t         binlog_pos; /**< Binlog position from SHOW SLAVE STATUS */
    char            *binlog_name; /**< Binlog name from SHOW SLAVE STATUS */
    bool             binlog_relay; /** Server is a Binlog Relay */
} MYSQL_SERVER_INFO;

void* info_copy_func(const void *val)
{
    ss_dassert(val);
    MYSQL_SERVER_INFO *old_val = (MYSQL_SERVER_INFO*)val;
    MYSQL_SERVER_INFO *new_val = static_cast<MYSQL_SERVER_INFO*>(MXS_MALLOC(sizeof(MYSQL_SERVER_INFO)));
    char *binlog_name = MXS_STRDUP(old_val->binlog_name);

    if (new_val && binlog_name)
    {
        *new_val = *old_val;
        new_val->binlog_name = binlog_name;
    }
    else
    {
        MXS_FREE(new_val);
        MXS_FREE(binlog_name);
        new_val = NULL;
    }

    return new_val;
}

void info_free_func(void *val)
{
    if (val)
    {
        MYSQL_SERVER_INFO *old_val = (MYSQL_SERVER_INFO*)val;
        MXS_FREE(old_val->binlog_name);
        MXS_FREE(old_val);
    }
}

/**
 * @brief Helper function that initializes the server info hashtable
 *
 * @param handle MySQL monitor handle
 * @param database List of monitored databases
 * @return True on success, false if initialization failed. At the moment
 *         initialization can only fail if memory allocation fails.
 */
bool init_server_info(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *database)
{
    bool rval = true;

    MYSQL_SERVER_INFO info = {};
    info.binlog_name = const_cast<char*>("");

    while (database)
    {
        /** Delete any existing structures and replace them with empty ones */
        hashtable_delete(handle->server_info, database->server->unique_name);

        if (!hashtable_add(handle->server_info, database->server->unique_name, &info))
        {
            rval = false;
            break;
        }

        database = database->next;
    }

    return rval;
}

/*lint +e14 */

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @param arg   The current handle - NULL if first start
 * @param opt   Configuration parameters
 * @return A handle to use when interacting with the monitor
 */
static void *
startMonitor(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    MYSQL_MONITOR *handle = (MYSQL_MONITOR*) monitor->handle;

    if (handle)
    {
        handle->shutdown = 0;
        MXS_FREE(handle->script);
    }
    else
    {
        handle = (MYSQL_MONITOR *) MXS_MALLOC(sizeof(MYSQL_MONITOR));
        HASHTABLE *server_info = hashtable_alloc(MAX_NUM_SLAVES, hashtable_item_strhash, hashtable_item_strcmp);

        if (handle == NULL || server_info == NULL)
        {
            MXS_FREE(handle);
            hashtable_free(server_info);
            return NULL;
        }

        hashtable_memory_fns(server_info, hashtable_item_strdup, info_copy_func,
                             hashtable_item_free, info_free_func);
        handle->server_info = server_info;
        handle->shutdown = 0;
        handle->id = config_get_global_options()->id;
        handle->warn_failover = true;
        handle->monitor = monitor;
    }

    /** This should always be reset to NULL */
    handle->master = NULL;

    handle->detectStaleMaster = config_get_bool(params, "detect_stale_master");
    handle->detectStaleSlave = config_get_bool(params, "detect_stale_slave");
    handle->replicationHeartbeat = config_get_bool(params, "detect_replication_lag");
    handle->multimaster = config_get_bool(params, "multimaster");
    handle->detect_standalone_master = config_get_bool(params, "detect_standalone_master");
    handle->failcount = config_get_integer(params, "failcount");
    handle->allow_cluster_recovery = config_get_bool(params, "allow_cluster_recovery");
    handle->mysql51_replication = config_get_bool(params, "mysql51_replication");
    handle->script = config_copy_string(params, "script");
    handle->events = config_get_enum(params, "events", mxs_monitor_event_enum_values);
    handle->allow_external_slaves = config_get_bool(params, "allow_external_slaves");
    handle->failover = config_get_bool(params, CN_FAILOVER);
    handle->failover_timeout = config_get_integer(params, CN_FAILOVER_TIMEOUT);
    handle->switchover = config_get_bool(params, CN_SWITCHOVER);
    handle->switchover_timeout = config_get_integer(params, CN_SWITCHOVER_TIMEOUT);

    bool error = false;

    if (!check_monitor_permissions(monitor, "SHOW SLAVE STATUS"))
    {
        MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        error = true;
    }

    if (!init_server_info(handle, monitor->monitored_servers))
    {
        error = true;
    }

    if (error)
    {
        hashtable_free(handle->server_info);
        MXS_FREE(handle->script);
        MXS_FREE(handle);
        handle = NULL;
    }
    else
    {
        handle->status = MXS_MONITOR_RUNNING;

        if (thread_start(&handle->thread, monitorMain, handle, 0) == NULL)
        {
            MXS_ERROR("Failed to start monitor thread for monitor '%s'.", monitor->name);
            hashtable_free(handle->server_info);
            MXS_FREE(handle->script);
            MXS_FREE(handle);
            handle = NULL;
        }
    }

    return handle;
}

/**
 * Stop a running monitor
 *
 * @param mon  The monitor that should be stopped.
 */
static void
stopMonitor(MXS_MONITOR *mon)
{
    MYSQL_MONITOR *handle = (MYSQL_MONITOR *) mon->handle;

    handle->shutdown = 1;
    thread_wait(handle->thread);
}

/**
 * Stop a running monitor
 *
 * @param mon  The monitor that should be stopped.
 *
 * @return True, if the monitor had to be stopped.
 *         False, if the monitor already was stopped.
 */
static bool stop_monitor(MXS_MONITOR* mon)
{
    // There should be no race here as long as admin operations are performed
    // with the single admin lock locked.

    bool actually_stopped = false;

    MYSQL_MONITOR *handle = static_cast<MYSQL_MONITOR*>(mon->handle);

    if (handle->status == MXS_MONITOR_RUNNING)
    {
        stopMonitor(mon);
        actually_stopped = true;
    }

    return actually_stopped;
}

/**
 * Daignostic interface
 *
 * @param dcb   DCB to print diagnostics
 * @param arg   The monitor handle
 */
static void diagnostics(DCB *dcb, const MXS_MONITOR *mon)
{
    const MYSQL_MONITOR *handle = (const MYSQL_MONITOR *)mon->handle;

    dcb_printf(dcb, "Failover:\t%s\n", handle->failover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Failover Timeout:\t%u\n", handle->failover_timeout);
    dcb_printf(dcb, "Switchover:\t%s\n", handle->switchover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Switchover Timeout:\t%u\n", handle->switchover_timeout);
    dcb_printf(dcb, "MaxScale MonitorId:\t%lu\n", handle->id);
    dcb_printf(dcb, "Replication lag:\t%s\n", (handle->replicationHeartbeat == 1) ? "enabled" : "disabled");
    dcb_printf(dcb, "Detect Stale Master:\t%s\n", (handle->detectStaleMaster == 1) ? "enabled" : "disabled");
    dcb_printf(dcb, "Server information\n\n");

    for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db; db = db->next)
    {
        MYSQL_SERVER_INFO *serv_info =
            static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info, db->server->unique_name));
        dcb_printf(dcb, "Server: %s\n", db->server->unique_name);
        dcb_printf(dcb, "Server ID: %d\n", serv_info->server_id);
        dcb_printf(dcb, "Read only: %s\n", serv_info->read_only ? "ON" : "OFF");
        dcb_printf(dcb, "Slave configured: %s\n", serv_info->slave_configured ? "YES" : "NO");
        dcb_printf(dcb, "Slave IO running: %s\n", serv_info->slave_io ? "YES" : "NO");
        dcb_printf(dcb, "Slave SQL running: %s\n", serv_info->slave_sql ? "YES" : "NO");
        dcb_printf(dcb, "Master ID: %d\n", serv_info->master_id);
        dcb_printf(dcb, "Master binlog file: %s\n", serv_info->binlog_name);
        dcb_printf(dcb, "Master binlog position: %lu\n", serv_info->binlog_pos);

        if (handle->multimaster)
        {
            dcb_printf(dcb, "Master group: %d\n", serv_info->group);
        }

        dcb_printf(dcb, "\n");
    }
}

/**
 * Diagnostic interface
 *
 * @param arg   The monitor handle
 */
static json_t* diagnostics_json(const MXS_MONITOR *mon)
{
    json_t* rval = json_object();

    const MYSQL_MONITOR *handle = (const MYSQL_MONITOR *)mon->handle;
    json_object_set_new(rval, "monitor_id", json_integer(handle->id));
    json_object_set_new(rval, "detect_stale_master", json_boolean(handle->detectStaleMaster));
    json_object_set_new(rval, "detect_stale_slave", json_boolean(handle->detectStaleSlave));
    json_object_set_new(rval, "detect_replication_lag", json_boolean(handle->replicationHeartbeat));
    json_object_set_new(rval, "multimaster", json_boolean(handle->multimaster));
    json_object_set_new(rval, "detect_standalone_master", json_boolean(handle->detect_standalone_master));
    json_object_set_new(rval, "failcount", json_integer(handle->failcount));
    json_object_set_new(rval, "allow_cluster_recovery", json_boolean(handle->allow_cluster_recovery));
    json_object_set_new(rval, "mysql51_replication", json_boolean(handle->mysql51_replication));
    json_object_set_new(rval, CN_FAILOVER, json_boolean(handle->failover));
    json_object_set_new(rval, CN_FAILOVER_TIMEOUT, json_integer(handle->failover_timeout));
    json_object_set_new(rval, CN_SWITCHOVER, json_boolean(handle->switchover));
    json_object_set_new(rval, CN_SWITCHOVER_TIMEOUT, json_integer(handle->switchover_timeout));

    if (handle->script)
    {
        json_object_set_new(rval, "script", json_string(handle->script));
    }

    if (mon->monitored_servers)
    {
        json_t* arr = json_array();

        for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db; db = db->next)
        {
            json_t* srv = json_object();
            MYSQL_SERVER_INFO *serv_info =
                static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                db->server->unique_name));
            json_object_set_new(srv, "name", json_string(db->server->unique_name));
            json_object_set_new(srv, "server_id", json_integer(serv_info->server_id));
            json_object_set_new(srv, "master_id", json_integer(serv_info->master_id));

            json_object_set_new(srv, "read_only", json_boolean(serv_info->read_only));
            json_object_set_new(srv, "slave_configured", json_boolean(serv_info->slave_configured));
            json_object_set_new(srv, "slave_io_running", json_boolean(serv_info->slave_io));
            json_object_set_new(srv, "slave_sql_running", json_boolean(serv_info->slave_sql));

            json_object_set_new(srv, "master_binlog_file", json_string(serv_info->binlog_name));
            json_object_set_new(srv, "master_binlog_position", json_integer(serv_info->binlog_pos));

            if (handle->multimaster)
            {
                json_object_set_new(srv, "master_group", json_integer(serv_info->group));
            }

            json_array_append_new(arr, srv);
        }

        json_object_set_new(rval, "server_info", arr);
    }

    return rval;
}

enum mysql_server_version
{
    MYSQL_SERVER_VERSION_100,
    MYSQL_SERVER_VERSION_55,
    MYSQL_SERVER_VERSION_51
};

static inline void monitor_mysql_db(MXS_MONITORED_SERVER* database, MYSQL_SERVER_INFO *serv_info,
                                    enum mysql_server_version server_version)
{
    unsigned int columns;
    int i_io_thread, i_sql_thread, i_binlog_pos, i_master_id, i_binlog_name;
    const char *query;

    if (server_version == MYSQL_SERVER_VERSION_100)
    {
        columns = 42;
        query = "SHOW ALL SLAVES STATUS";
        i_io_thread = MARIA10_STATUS_IO_RUNNING;
        i_sql_thread = MARIA10_STATUS_SQL_RUNNING;
        i_binlog_name = MARIA10_STATUS_BINLOG_NAME;
        i_binlog_pos = MARIA10_STATUS_BINLOG_POS;
        i_master_id = MARIA10_STATUS_MASTER_ID;
    }
    else
    {
        columns = server_version == MYSQL_SERVER_VERSION_55 ? 40 : 38;
        query = "SHOW SLAVE STATUS";
        i_io_thread = MYSQL55_STATUS_IO_RUNNING;
        i_sql_thread = MYSQL55_STATUS_SQL_RUNNING;
        i_binlog_name = MYSQL55_STATUS_BINLOG_NAME;
        i_binlog_pos = MYSQL55_STATUS_BINLOG_POS;
        i_master_id = MYSQL55_STATUS_MASTER_ID;
    }

    /** Clear old states */
    monitor_clear_pending_status(database, SERVER_SLAVE | SERVER_MASTER | SERVER_RELAY_MASTER |
                                 SERVER_STALE_STATUS | SERVER_SLAVE_OF_EXTERNAL_MASTER);

    MYSQL_RES* result;

    if (mysql_query(database->con, query) == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < columns)
        {
            mysql_free_result(result);
            MXS_ERROR("\"%s\" returned less than the expected amount of columns. "
                      "Expected %u columns.", query, columns);
            return;
        }

        MYSQL_ROW row = mysql_fetch_row(result);
        long master_id = -1;

        if (row)
        {
            serv_info->slave_configured = true;
            int nconfigured = 0;
            int nrunning = 0;

            do
            {
                /* get Slave_IO_Running and Slave_SQL_Running values*/
                serv_info->slave_io = strncmp(row[i_io_thread], "Yes", 3) == 0;
                serv_info->slave_sql = strncmp(row[i_sql_thread], "Yes", 3) == 0;

                if (serv_info->slave_io && serv_info->slave_sql)
                {
                    if (nrunning == 0)
                    {
                        /** Only check binlog name for the first running slave */
                        char *binlog_name = MXS_STRDUP(row[i_binlog_name]);

                        if (binlog_name)
                        {
                            MXS_FREE(serv_info->binlog_name);
                            serv_info->binlog_name = binlog_name;
                            serv_info->binlog_pos = atol(row[i_binlog_pos]);
                        }
                    }

                    nrunning++;
                }

                /* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building
                 * the replication tree, slaves ids will be added to master(s) and we will have at least the
                 * root master server.
                 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
                 */
                if (serv_info->slave_io && server_version != MYSQL_SERVER_VERSION_51)
                {
                    /* Get Master_Server_Id */
                    master_id = atol(row[i_master_id]);
                    if (master_id == 0)
                    {
                        master_id = -1;
                    }
                }

                nconfigured++;
                row = mysql_fetch_row(result);
            }
            while (row);


            /* If all configured slaves are running set this node as slave */
            if (nrunning > 0 && nrunning == nconfigured)
            {
                monitor_set_pending_status(database, SERVER_SLAVE);
            }
        }
        else
        {
            /** Query returned no rows, replication is not configured */
            serv_info->slave_configured = false;
            serv_info->slave_io = false;
            serv_info->slave_sql = false;
            serv_info->binlog_pos = 0;
            serv_info->binlog_name[0] = '\0';
        }

        /** Store master_id of current node. For MySQL 5.1 it will be set at a later point. */
        database->server->master_id = master_id;
        serv_info->master_id = master_id;

        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(database);
    }
}

/**
 * Build the replication tree for a MySQL 5.1 cluster
 *
 * This function queries each server with SHOW SLAVE HOSTS to determine which servers
 * have slaves replicating from them.
 * @param mon Monitor
 * @return Lowest server ID master in the monitor
 */
static MXS_MONITORED_SERVER *build_mysql51_replication_tree(MXS_MONITOR *mon)
{
    MXS_MONITORED_SERVER* database = mon->monitored_servers;
    MXS_MONITORED_SERVER *ptr, *rval = NULL;
    int i;
    MYSQL_MONITOR *handle = static_cast<MYSQL_MONITOR*>(mon->handle);

    while (database)
    {
        bool ismaster = false;
        MYSQL_RES* result;
        MYSQL_ROW row;
        int nslaves = 0;
        if (database->con)
        {
            if (mysql_query(database->con, "SHOW SLAVE HOSTS") == 0
                && (result = mysql_store_result(database->con)) != NULL)
            {
                if (mysql_field_count(database->con) < 4)
                {
                    mysql_free_result(result);
                    MXS_ERROR("\"SHOW SLAVE HOSTS\" "
                              "returned less than the expected amount of columns. "
                              "Expected 4 columns.");
                    return NULL;
                }

                if (mysql_num_rows(result) > 0)
                {
                    ismaster = true;
                    while (nslaves < MAX_NUM_SLAVES && (row = mysql_fetch_row(result)))
                    {
                        /* get Slave_IO_Running and Slave_SQL_Running values*/
                        database->server->slaves[nslaves] = atol(row[SLAVE_HOSTS_SERVER_ID]);
                        nslaves++;
                        MXS_DEBUG("Found slave at %s:%s", row[SLAVE_HOSTS_HOSTNAME], row[SLAVE_HOSTS_PORT]);
                    }
                    database->server->slaves[nslaves] = 0;
                }

                mysql_free_result(result);
            }
            else
            {
                mon_report_query_error(database);
            }

            /* Set the Slave Role */
            if (ismaster)
            {
                handle->master = database;

                MXS_DEBUG("Master server found at [%s]:%d with %d slaves",
                          database->server->name,
                          database->server->port,
                          nslaves);

                monitor_set_pending_status(database, SERVER_MASTER);
                database->server->depth = 0; // Add Depth 0 for Master

                if (rval == NULL || rval->server->node_id > database->server->node_id)
                {
                    rval = database;
                }
            }
        }
        database = database->next;
    }

    database = mon->monitored_servers;

    /** Set master server IDs */
    while (database)
    {
        ptr = mon->monitored_servers;

        while (ptr)
        {
            for (i = 0; ptr->server->slaves[i]; i++)
            {
                if (ptr->server->slaves[i] == database->server->node_id)
                {
                    database->server->master_id = ptr->server->node_id;
                    database->server->depth = 1; // Add Depth 1 for Slave
                    break;
                }
            }
            ptr = ptr->next;
        }
        if (SERVER_IS_SLAVE(database->server) &&
            (database->server->master_id <= 0 ||
             database->server->master_id != handle->master->server->node_id))
        {
            if (handle->allow_external_slaves)
            {
                monitor_set_pending_status(database, SERVER_SLAVE);
            }
            else
            {
                monitor_clear_pending_status(database, SERVER_SLAVE);
            }
            monitor_set_pending_status(database, SERVER_SLAVE_OF_EXTERNAL_MASTER);
        }
        database = database->next;
    }
    return rval;
}

/**
 * Monitor an individual server
 *
 * @param handle        The MySQL Monitor object
 * @param database  The database to probe
 */
static void
monitorDatabase(MXS_MONITOR *mon, MXS_MONITORED_SERVER *database)
{
    MYSQL_MONITOR* handle = static_cast<MYSQL_MONITOR*>(mon->handle);
    MYSQL_ROW row;
    MYSQL_RES *result;
    unsigned long int server_version = 0;
    char *server_string;

    /* Don't probe servers in maintenance mode */
    if (SERVER_IN_MAINT(database->server))
    {
        return;
    }

    /** Store previous status */
    database->mon_prev_status = database->server->status;

    mxs_connect_result_t rval = mon_ping_or_connect_to_db(mon, database);
    if (rval == MONITOR_CONN_OK)
    {
        server_clear_status_nolock(database->server, SERVER_AUTH_ERROR);
        monitor_clear_pending_status(database, SERVER_AUTH_ERROR);
    }
    else
    {
        /* The current server is not running
         *
         * Store server NOT running in server and monitor server pending struct
         *
         */
        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(database->server, SERVER_AUTH_ERROR);
            monitor_set_pending_status(database, SERVER_AUTH_ERROR);
        }
        server_clear_status_nolock(database->server, SERVER_RUNNING);
        monitor_clear_pending_status(database, SERVER_RUNNING);

        /* Also clear M/S state in both server and monitor server pending struct */
        server_clear_status_nolock(database->server, SERVER_SLAVE);
        server_clear_status_nolock(database->server, SERVER_MASTER);
        server_clear_status_nolock(database->server, SERVER_RELAY_MASTER);
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_clear_pending_status(database, SERVER_MASTER);
        monitor_clear_pending_status(database, SERVER_RELAY_MASTER);

        /* Clean addition status too */
        server_clear_status_nolock(database->server, SERVER_SLAVE_OF_EXTERNAL_MASTER);
        server_clear_status_nolock(database->server, SERVER_STALE_STATUS);
        server_clear_status_nolock(database->server, SERVER_STALE_SLAVE);
        monitor_clear_pending_status(database, SERVER_SLAVE_OF_EXTERNAL_MASTER);
        monitor_clear_pending_status(database, SERVER_STALE_STATUS);
        monitor_clear_pending_status(database, SERVER_STALE_SLAVE);

        /* Log connect failure only once */
        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }

        return;
    }

    /* Store current status in both server and monitor server pending struct */
    server_set_status_nolock(database->server, SERVER_RUNNING);
    monitor_set_pending_status(database, SERVER_RUNNING);

    /* get server version from current server */
    server_version = mysql_get_server_version(database->con);

    /* get server version string */
    mxs_mysql_set_server_version(database->con, database->server);
    server_string = database->server->version_string;

    MYSQL_SERVER_INFO *serv_info =
        static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info, database->server->unique_name));
    ss_dassert(serv_info);

    /* Check whether current server is MaxScale Binlog Server */
    if (mysql_query(database->con, "SELECT @@maxscale_version") == 0 &&
        (result = mysql_store_result(database->con)) != NULL)
    {
        serv_info->binlog_relay = true;
        mysql_free_result(result);
    }
    else
    {
        serv_info->binlog_relay = false;
    }

    /* Get server_id and read_only from current node */
    if (mysql_query(database->con, "SELECT @@server_id, @@read_only") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        long server_id = -1;

        if (mysql_field_count(database->con) != 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for 'SELECT @@server_id, @@read_only'. Expected 2 columns."
                      " MySQL Version: %s", server_string);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            server_id = strtol(row[0], NULL, 10);
            if ((errno == ERANGE && (server_id == LONG_MAX
                                     || server_id == LONG_MIN)) || (errno != 0 && server_id == 0))
            {
                server_id = -1;
            }

            database->server->node_id = server_id;
            serv_info->server_id = server_id;
            serv_info->read_only = (row[1] && strcmp(row[1], "1") == 0);
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(database);
    }

    /* Check first for MariaDB 10.x.x and get status for multi-master replication */
    if (server_version >= 100000)
    {
        monitor_mysql_db(database, serv_info, MYSQL_SERVER_VERSION_100);
    }
    else if (server_version >= 5 * 10000 + 5 * 100)
    {
        monitor_mysql_db(database, serv_info, MYSQL_SERVER_VERSION_55);
    }
    else
    {
        if (handle->mysql51_replication)
        {
            monitor_mysql_db(database, serv_info, MYSQL_SERVER_VERSION_51);
        }
        else if (report_version_err)
        {
            report_version_err = false;
            MXS_ERROR("MySQL version is lower than 5.5 and 'mysql51_replication' option is "
                      "not enabled, replication tree cannot be resolved. To enable MySQL 5.1 replication "
                      "detection, add 'mysql51_replication=true' to the monitor section.");
        }
    }

}

/**
 * @brief A node in a graph
 */
struct graph_node
{
    int index;
    int lowest_index;
    int cycle;
    bool active;
    struct graph_node *parent;
    MYSQL_SERVER_INFO *info;
    MXS_MONITORED_SERVER *db;
};

/**
 * @brief Visit a node in the graph
 *
 * This function is the main function used to determine whether the node is a
 * part of a cycle. It is an implementation of the Tarjan's strongly connected
 * component algorithm. All one node cycles are ignored since normal
 * master-slave monitoring handles that.
 *
 * Tarjan's strongly connected component algorithm:
 *
 *     https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
 */
static void visit_node(struct graph_node *node, struct graph_node **stack,
                       int *stacksize, int *index, int *cycle)
{
    /** Assign an index to this node */
    node->lowest_index = node->index = *index;
    node->active = true;
    *index += 1;

    stack[*stacksize] = node;
    *stacksize += 1;

    if (node->parent == NULL)
    {
        /** This node does not connect to another node, it can't be a part of a cycle */
        node->lowest_index = -1;
    }
    else if (node->parent->index == 0)
    {
        /** Node has not been visited */
        visit_node(node->parent, stack, stacksize, index, cycle);

        if (node->parent->lowest_index < node->lowest_index)
        {
            /** The parent connects to a node with a lower index, this node
                could be a part of a cycle. */
            node->lowest_index = node->parent->lowest_index;
        }
    }
    else if (node->parent->active)
    {
        /** This node could be a root node of the cycle */
        if (node->parent->index < node->lowest_index)
        {
            /** Root node found */
            node->lowest_index = node->parent->index;
        }
    }
    else
    {
        /** Node connects to an already connected cycle, it can't be a part of it */
        node->lowest_index = -1;
    }

    if (node->active && node->parent && node->lowest_index > 0)
    {
        if (node->lowest_index == node->index &&
            node->lowest_index == node->parent->lowest_index)
        {
            /**
             * Found a multi-node cycle from the graph. The cycle is formed from the
             * nodes with a lowest_index value equal to the lowest_index value of the
             * current node. Rest of the nodes on the stack are not part of a cycle
             * and can be discarded.
             */

            *cycle += 1;

            while (*stacksize > 0)
            {
                struct graph_node *top = stack[(*stacksize) - 1];
                top->active = false;

                if (top->lowest_index == node->lowest_index)
                {
                    top->cycle = *cycle;
                }
                *stacksize -= 1;
            }
        }
    }
    else
    {
        /** Pop invalid nodes off the stack */
        node->active = false;
        if (*stacksize > 0)
        {
            *stacksize -= 1;
        }
    }
}

/**
 * @brief Find the strongly connected components in the replication tree graph
 *
 * Each replication cluster is a directed graph made out of replication
 * trees. If this graph has strongly connected components (more generally
 * cycles), it is considered a multi-master cluster due to the fact that there
 * are multiple nodes where the data can originate.
 *
 * Detecting the cycles in the graph allows this monitor to better understand
 * the relationships between the nodes. All nodes that are a part of a cycle can
 * be labeled as master nodes. This information will later be used to choose the
 * right master where the writes should go.
 *
 * This function also populates the MYSQL_SERVER_INFO structures group
 * member. Nodes in a group get a positive group ID where the nodes not in a
 * group get a group ID of 0.
 */
void find_graph_cycles(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *database, int nservers)
{
    struct graph_node graph[nservers];
    struct graph_node *stack[nservers];
    int nodes = 0;

    for (MXS_MONITORED_SERVER *db = database; db; db = db->next)
    {
        graph[nodes].info =
            static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info, db->server->unique_name));
        graph[nodes].db = db;
        ss_dassert(graph[nodes].info);
        graph[nodes].index = graph[nodes].lowest_index = 0;
        graph[nodes].cycle = 0;
        graph[nodes].active = false;
        graph[nodes].parent = NULL;
        nodes++;
    }

    /** Build the graph */
    for (int i = 0; i < nservers; i++)
    {
        if (graph[i].info->master_id > 0)
        {
            /** Found a connected node */
            for (int k = 0; k < nservers; k++)
            {
                if (graph[k].info->server_id == graph[i].info->master_id)
                {
                    graph[i].parent = &graph[k];
                    break;
                }
            }
        }
    }

    int index = 1;
    int cycle = 0;
    int stacksize = 0;

    for (int i = 0; i < nservers; i++)
    {
        if (graph[i].index == 0)
        {
            /** Index is 0, this node has not yet been visited */
            visit_node(&graph[i], stack, &stacksize, &index, &cycle);
        }
    }

    for (int i = 0; i < nservers; i++)
    {
        graph[i].info->group = graph[i].cycle;

        if (graph[i].cycle > 0)
        {
            /** We have at least one cycle in the graph */
            if (graph[i].info->read_only)
            {
                monitor_set_pending_status(graph[i].db, SERVER_SLAVE);
                monitor_clear_pending_status(graph[i].db, SERVER_MASTER);
            }
            else
            {
                monitor_set_pending_status(graph[i].db, SERVER_MASTER);
                monitor_clear_pending_status(graph[i].db, SERVER_SLAVE);
            }
        }
        else if (handle->detectStaleMaster && cycle == 0 &&
                 graph[i].db->server->status & SERVER_MASTER &&
                 (graph[i].db->pending_status & SERVER_MASTER) == 0)
        {
            /**
             * Stale master detection is handled here for multi-master mode.
             *
             * If we know that no cycles were found from the graph and that a
             * server once had the master status, replication has broken
             * down. These masters are assigned the stale master status allowing
             * them to be used as masters even if they lose their slaves. A
             * slave in this case can be either a normal slave or another
             * master.
             */
            if (graph[i].info->read_only)
            {
                /** The master is in read-only mode, set it into Slave state */
                monitor_set_pending_status(graph[i].db, SERVER_SLAVE);
                monitor_clear_pending_status(graph[i].db, SERVER_MASTER | SERVER_STALE_STATUS);
            }
            else
            {
                monitor_set_pending_status(graph[i].db, SERVER_MASTER | SERVER_STALE_STATUS);
                monitor_clear_pending_status(graph[i].db, SERVER_SLAVE);
            }
        }
    }
}

/**
 * @brief Check whether failover conditions have been met
 *
 * This function checks whether all the conditions to trigger a failover have
 * been met. For a failover to happen, only one server must be available and
 * other servers must have passed the configured tolerance level of failures.
 *
 * @param handle Monitor instance
 * @param db     Monitor servers
 *
 * @return True if failover is required
 */
bool failover_required(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *db)
{
    int candidates = 0;

    while (db)
    {
        if (SERVER_IS_RUNNING(db->server))
        {
            candidates++;
            MYSQL_SERVER_INFO *server_info =
                static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                db->server->unique_name));

            if (server_info->read_only || server_info->slave_configured || candidates > 1)
            {
                return false;
            }
        }
        else if (db->mon_err_count < handle->failcount)
        {
            return false;
        }

        db = db->next;
    }

    return candidates == 1;
}

/**
 * @brief Initiate simple failover
 *
 * This function does the actual failover by assigning the last remaining server
 * the master status and setting all other servers into maintenance mode. By
 * setting the servers into maintenance mode, we prevent any possible conflicts
 * when the failed servers come back up.
 *
 * @param handle Monitor instance
 * @param db     Monitor servers
 */
void do_failover(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *db)
{
    while (db)
    {
        if (SERVER_IS_RUNNING(db->server))
        {
            if (!SERVER_IS_MASTER(db->server) && handle->warn_failover)
            {
                MXS_WARNING("Failover initiated, server '%s' is now the master.%s",
                            db->server->unique_name,
                            handle->allow_cluster_recovery ?
                            "" : " All other servers are set into maintenance mode.");
                handle->warn_failover = false;
            }

            server_clear_set_status(db->server, SERVER_SLAVE, SERVER_MASTER | SERVER_STALE_STATUS);
            monitor_set_pending_status(db, SERVER_MASTER | SERVER_STALE_STATUS);
            monitor_clear_pending_status(db, SERVER_SLAVE);
            handle->master = db;
        }
        else if (!handle->allow_cluster_recovery)
        {
            server_set_status_nolock(db->server, SERVER_MAINT);
            monitor_set_pending_status(db, SERVER_MAINT);
        }
        db = db->next;
    }
}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg   The handle of the monitor
 */
static void
monitorMain(void *arg)
{
    MYSQL_MONITOR *handle  = (MYSQL_MONITOR *) arg;
    MXS_MONITOR* mon = handle->monitor;
    MXS_MONITORED_SERVER *ptr;
    int replication_heartbeat;
    bool detect_stale_master;
    int num_servers = 0;
    MXS_MONITORED_SERVER *root_master = NULL;
    size_t nrounds = 0;
    int log_no_master = 1;
    bool heartbeat_checked = false;

    replication_heartbeat = handle->replicationHeartbeat;
    detect_stale_master = handle->detectStaleMaster;

    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in monitor module. Exiting.");
        handle->status = MXS_MONITOR_STOPPED;
        return;
    }

    load_server_journal(mon, &handle->master);

    while (1)
    {
        if (handle->shutdown)
        {
            handle->status = MXS_MONITOR_STOPPING;
            mysql_thread_end();
            handle->status = MXS_MONITOR_STOPPED;
            return;
        }
        /** Wait base interval */
        thread_millisleep(MXS_MON_BASE_INTERVAL_MS);

        if (handle->replicationHeartbeat && !heartbeat_checked)
        {
            check_maxscale_schema_replication(mon);
            heartbeat_checked = true;
        }

        /**
         * Calculate how far away the monitor interval is from its full
         * cycle and if monitor interval time further than the base
         * interval, then skip monitoring checks. Excluding the first
         * round.
         */
        if (nrounds != 0 &&
            (((nrounds * MXS_MON_BASE_INTERVAL_MS) % mon->interval) >=
             MXS_MON_BASE_INTERVAL_MS) && (!mon->server_pending_changes))
        {
            nrounds += 1;
            continue;
        }
        nrounds += 1;
        /* reset num_servers */
        num_servers = 0;

        lock_monitor_servers(mon);
        servers_status_pending_to_current(mon);

        /* start from the first server in the list */
        ptr = mon->monitored_servers;

        while (ptr)
        {
            ptr->mon_prev_status = ptr->server->status;

            /* copy server status into monitor pending_status */
            ptr->pending_status = ptr->server->status;

            /* monitor current node */
            monitorDatabase(mon, ptr);

            /* reset the slave list of current node */
            memset(&ptr->server->slaves, 0, sizeof(ptr->server->slaves));

            num_servers++;

            if (mon_status_changed(ptr))
            {
                if (SRV_MASTER_STATUS(ptr->mon_prev_status))
                {
                    /** Master failed, can't recover */
                    MXS_NOTICE("Server [%s]:%d lost the master status.",
                               ptr->server->name,
                               ptr->server->port);
                }
            }

            if (mon_status_changed(ptr))
            {
#if defined(SS_DEBUG)
                MXS_INFO("Backend server [%s]:%d state : %s",
                         ptr->server->name,
                         ptr->server->port,
                         STRSRVSTATUS(ptr->server));
#else
                MXS_DEBUG("Backend server [%s]:%d state : %s",
                          ptr->server->name,
                          ptr->server->port,
                          STRSRVSTATUS(ptr->server));
#endif
            }

            if (SERVER_IS_DOWN(ptr->server))
            {
                /** Increase this server'e error count */
                ptr->mon_err_count += 1;
            }
            else
            {
                /** Reset this server's error count */
                ptr->mon_err_count = 0;
            }

            ptr = ptr->next;
        }

        ptr = mon->monitored_servers;
        /* if only one server is configured, that's is Master */
        if (num_servers == 1)
        {
            if (SERVER_IS_RUNNING(ptr->server))
            {
                ptr->server->depth = 0;
                /* status cleanup */
                monitor_clear_pending_status(ptr, SERVER_SLAVE);

                /* master status set */
                monitor_set_pending_status(ptr, SERVER_MASTER);

                ptr->server->depth = 0;
                handle->master = ptr;
                root_master = ptr;
            }
        }
        else
        {
            /* Compute the replication tree */
            if (handle->mysql51_replication)
            {
                root_master = build_mysql51_replication_tree(mon);
            }
            else
            {
                root_master = get_replication_tree(mon, num_servers);
            }
        }

        if (handle->multimaster && num_servers > 0)
        {
            /** Find all the master server cycles in the cluster graph. If
                multiple masters are found, the servers with the read_only
                variable set to ON will be assigned the slave status. */
            find_graph_cycles(handle, mon->monitored_servers, num_servers);
        }

        ptr = mon->monitored_servers;
        while (ptr)
        {
            MYSQL_SERVER_INFO *serv_info =
                static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                ptr->server->unique_name));
            ss_dassert(serv_info);

            if (ptr->server->node_id > 0 && ptr->server->master_id > 0 &&
                getSlaveOfNodeId(mon->monitored_servers, ptr->server->node_id) &&
                getServerByNodeId(mon->monitored_servers, ptr->server->master_id) &&
                (!handle->multimaster || serv_info->group == 0))
            {
                /** This server is both a slave and a master i.e. a relay master */
                monitor_set_pending_status(ptr, SERVER_RELAY_MASTER);
                monitor_clear_pending_status(ptr, SERVER_MASTER);
            }

            /* Remove SLAVE status if this server is a Binlog Server relay */
            if (serv_info->binlog_relay)
            {
                monitor_clear_pending_status(ptr, SERVER_SLAVE);
            }

            ptr = ptr->next;
        }

        /* Update server status from monitor pending status on that server*/

        ptr = mon->monitored_servers;
        while (ptr)
        {
            if (!SERVER_IN_MAINT(ptr->server))
            {
                MYSQL_SERVER_INFO *serv_info =
                    static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                    ptr->server->unique_name));
                ss_dassert(serv_info);

                /** If "detect_stale_master" option is On, let's use the previous master.
                 *
                 * Multi-master mode detects the stale masters in find_graph_cycles().
                 */
                if (detect_stale_master && root_master && !handle->multimaster &&
                    (strcmp(ptr->server->name, root_master->server->name) == 0 &&
                     ptr->server->port == root_master->server->port) &&
                    (ptr->server->status & SERVER_MASTER) &&
                    !(ptr->pending_status & SERVER_MASTER) &&
                    !serv_info->read_only)
                {
                    /**
                     * In this case server->status will not be updated from pending_status
                     * Set the STALE bit for this server in server struct
                     */
                    server_set_status_nolock(ptr->server, SERVER_STALE_STATUS | SERVER_MASTER);
                    ptr->pending_status |= SERVER_STALE_STATUS | SERVER_MASTER;

                    /** Log the message only if the master server didn't have
                     * the stale master bit set */
                    if ((ptr->mon_prev_status & SERVER_STALE_STATUS) == 0)
                    {
                        MXS_WARNING("All slave servers under the current master "
                                    "server have been lost. Assigning Stale Master"
                                    " status to the old master server '%s' (%s:%i).",
                                    ptr->server->unique_name, ptr->server->name,
                                    ptr->server->port);
                    }
                }

                if (handle->detectStaleSlave)
                {
                    unsigned bits = SERVER_SLAVE | SERVER_RUNNING;

                    if ((ptr->mon_prev_status & bits) == bits &&
                        root_master && SERVER_IS_MASTER(root_master->server))
                    {
                        /** Slave with a running master, assign stale slave candidacy */
                        if ((ptr->pending_status & bits) == bits)
                        {
                            ptr->pending_status |= SERVER_STALE_SLAVE;
                        }
                        /** Server lost slave when a master is available, remove
                         * stale slave candidacy */
                        else if ((ptr->pending_status & bits) == SERVER_RUNNING)
                        {
                            ptr->pending_status &= ~SERVER_STALE_SLAVE;
                        }
                    }
                    /** If this server was a stale slave candidate, assign
                     * slave status to it */
                    else if (ptr->mon_prev_status & SERVER_STALE_SLAVE &&
                             ptr->pending_status & SERVER_RUNNING &&
                             // Master is down
                             (!root_master || !SERVER_IS_MASTER(root_master->server) ||
                              // Master just came up
                              (SERVER_IS_MASTER(root_master->server) &&
                               (root_master->mon_prev_status & SERVER_MASTER) == 0)))
                    {
                        ptr->pending_status |= SERVER_SLAVE;
                    }
                    else if (root_master == NULL && serv_info->slave_configured)
                    {
                        ptr->pending_status |= SERVER_SLAVE;
                    }
                }

                ptr->server->status = ptr->pending_status;
            }
            ptr = ptr->next;
        }

        /** Now that all servers have their status correctly set, we can check
            if we need to do a failover */
        if (handle->detect_standalone_master)
        {
            if (failover_required(handle, mon->monitored_servers))
            {
                /** Other servers have died, initiate a failover to the last remaining server */
                do_failover(handle, mon->monitored_servers);
            }
            else
            {
                handle->warn_failover = true;
            }
        }

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(mon, handle->script, handle->events);

        if (handle->failover)
        {
            if (!mon_process_failover(mon, handle->failover_timeout))
            {
                MXS_ALERT("Failed to perform failover, disabling failover functionality. "
                          "To enable failover functionality, manually set 'failover' to "
                          "'true' for monitor '%s' via MaxAdmin or the REST API.", mon->name);
                handle->failover = false;
            }
        }

        /* log master detection failure of first master becomes available after failure */
        if (root_master &&
            mon_status_changed(root_master) &&
            !(root_master->server->status & SERVER_STALE_STATUS))
        {
            if (root_master->pending_status & (SERVER_MASTER) && SERVER_IS_RUNNING(root_master->server))
            {
                if (!(root_master->mon_prev_status & SERVER_STALE_STATUS) &&
                    !(root_master->server->status & SERVER_MAINT))
                {
                    MXS_NOTICE("A Master Server is now available: %s:%i",
                               root_master->server->name,
                               root_master->server->port);
                }
            }
            else
            {
                MXS_ERROR("No Master can be determined. Last known was %s:%i",
                          root_master->server->name,
                          root_master->server->port);
            }
            log_no_master = 1;
        }
        else
        {
            if (!root_master && log_no_master)
            {
                MXS_ERROR("No Master can be determined");
                log_no_master = 0;
            }
        }

        /* Do now the heartbeat replication set/get for MySQL Replication Consistency */
        if (replication_heartbeat &&
            root_master &&
            (SERVER_IS_MASTER(root_master->server) ||
             SERVER_IS_RELAY_SERVER(root_master->server)))
        {
            set_master_heartbeat(handle, root_master);
            ptr = mon->monitored_servers;

            while (ptr)
            {
                MYSQL_SERVER_INFO *serv_info =
                    static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                    ptr->server->unique_name));
                ss_dassert(serv_info);

                if ((!SERVER_IN_MAINT(ptr->server)) && SERVER_IS_RUNNING(ptr->server))
                {
                    if (ptr->server->node_id != root_master->server->node_id &&
                        (SERVER_IS_SLAVE(ptr->server) ||
                         SERVER_IS_RELAY_SERVER(ptr->server)) &&
                        !serv_info->binlog_relay)  // No select lag for Binlog Server
                    {
                        set_slave_heartbeat(mon, ptr);
                    }
                }
                ptr = ptr->next;
            }
        }

        mon_hangup_failed_servers(mon);
        servers_status_current_to_pending(mon);
        store_server_journal(mon, handle->master);
        release_monitor_servers(mon);
    } /*< while (1) */
}

/**
 * Fetch a MySQL node by node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id   The MySQL server_id to fetch
 * @return      The server with the required server_id
 */
static MXS_MONITORED_SERVER *
getServerByNodeId(MXS_MONITORED_SERVER *ptr, long node_id)
{
    SERVER *current;
    while (ptr)
    {
        current = ptr->server;
        if (current->node_id == node_id)
        {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/**
 * Fetch a MySQL slave node from a node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id   The MySQL server_id to fetch
 * @return      The slave server of this node_id
 */
static MXS_MONITORED_SERVER *
getSlaveOfNodeId(MXS_MONITORED_SERVER *ptr, long node_id)
{
    SERVER *current;
    while (ptr)
    {
        current = ptr->server;
        if (current->master_id == node_id)
        {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/*******
 * This function sets the replication heartbeat
 * into the maxscale_schema.replication_heartbeat table in the current master.
 * The inserted values will be seen from all slaves replication from this master.
 *
 * @param handle    The monitor handle
 * @param database      The number database server
 */
static void set_master_heartbeat(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *database)
{
    unsigned long id = handle->id;
    time_t heartbeat;
    time_t purge_time;
    char heartbeat_insert_query[512] = "";
    char heartbeat_purge_query[512] = "";
    MYSQL_RES *result;
    long returned_rows;

    if (handle->master == NULL)
    {
        MXS_ERROR("set_master_heartbeat called without an available Master server");
        return;
    }

    /* check if the maxscale_schema database and replication_heartbeat table exist */
    if (mysql_query(database->con, "SELECT table_name FROM information_schema.tables "
                    "WHERE table_schema = 'maxscale_schema' AND table_name = 'replication_heartbeat'"))
    {
        MXS_ERROR( "Error checking for replication_heartbeat in Master server"
                   ": %s", mysql_error(database->con));
        database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
    }

    result = mysql_store_result(database->con);

    if (result == NULL)
    {
        returned_rows = 0;
    }
    else
    {
        returned_rows = mysql_num_rows(result);
        mysql_free_result(result);
    }

    if (0 == returned_rows)
    {
        /* create repl_heartbeat table in maxscale_schema database */
        if (mysql_query(database->con, "CREATE TABLE IF NOT EXISTS "
                        "maxscale_schema.replication_heartbeat "
                        "(maxscale_id INT NOT NULL, "
                        "master_server_id INT NOT NULL, "
                        "master_timestamp INT UNSIGNED NOT NULL, "
                        "PRIMARY KEY ( master_server_id, maxscale_id ) )"))
        {
            MXS_ERROR("Error creating maxscale_schema.replication_heartbeat "
                      "table in Master server: %s", mysql_error(database->con));

            database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
        }
    }

    /* auto purge old values after 48 hours*/
    purge_time = time(0) - (3600 * 48);

    sprintf(heartbeat_purge_query,
            "DELETE FROM maxscale_schema.replication_heartbeat WHERE master_timestamp < %lu", purge_time);

    if (mysql_query(database->con, heartbeat_purge_query))
    {
        MXS_ERROR("Error deleting from maxscale_schema.replication_heartbeat "
                  "table: [%s], %s",
                  heartbeat_purge_query,
                  mysql_error(database->con));
    }

    heartbeat = time(0);

    /* set node_ts for master as time(0) */
    database->server->node_ts = heartbeat;

    sprintf(heartbeat_insert_query,
            "UPDATE maxscale_schema.replication_heartbeat SET master_timestamp = %lu WHERE master_server_id = %li AND maxscale_id = %lu",
            heartbeat, handle->master->server->node_id, id);

    /* Try to insert MaxScale timestamp into master */
    if (mysql_query(database->con, heartbeat_insert_query))
    {

        database->server->rlag = MAX_RLAG_NOT_AVAILABLE;

        MXS_ERROR("Error updating maxscale_schema.replication_heartbeat table: [%s], %s",
                  heartbeat_insert_query,
                  mysql_error(database->con));
    }
    else
    {
        if (mysql_affected_rows(database->con) == 0)
        {
            heartbeat = time(0);
            sprintf(heartbeat_insert_query,
                    "REPLACE INTO maxscale_schema.replication_heartbeat (master_server_id, maxscale_id, master_timestamp ) VALUES ( %li, %lu, %lu)",
                    handle->master->server->node_id, id, heartbeat);

            if (mysql_query(database->con, heartbeat_insert_query))
            {

                database->server->rlag = MAX_RLAG_NOT_AVAILABLE;

                MXS_ERROR("Error inserting into "
                          "maxscale_schema.replication_heartbeat table: [%s], %s",
                          heartbeat_insert_query,
                          mysql_error(database->con));
            }
            else
            {
                /* Set replication lag to 0 for the master */
                database->server->rlag = 0;

                MXS_DEBUG("heartbeat table inserted data for %s:%i",
                          database->server->name, database->server->port);
            }
        }
        else
        {
            /* Set replication lag as 0 for the master */
            database->server->rlag = 0;

            MXS_DEBUG("heartbeat table updated for Master %s:%i",
                      database->server->name, database->server->port);
        }
    }
}

/*******
 * This function gets the replication heartbeat
 * from the maxscale_schema.replication_heartbeat table in the current slave
 * and stores the timestamp and replication lag in the slave server struct
 *
 * @param handle    The monitor handle
 * @param database      The number database server
 */
static void set_slave_heartbeat(MXS_MONITOR* mon, MXS_MONITORED_SERVER *database)
{
    MYSQL_MONITOR *handle = (MYSQL_MONITOR*) mon->handle;
    unsigned long id = handle->id;
    time_t heartbeat;
    char select_heartbeat_query[256] = "";
    MYSQL_ROW row;
    MYSQL_RES *result;

    if (handle->master == NULL)
    {
        MXS_ERROR("set_slave_heartbeat called without an available Master server");
        return;
    }

    /* Get the master_timestamp value from maxscale_schema.replication_heartbeat table */

    sprintf(select_heartbeat_query, "SELECT master_timestamp "
            "FROM maxscale_schema.replication_heartbeat "
            "WHERE maxscale_id = %lu AND master_server_id = %li",
            id, handle->master->server->node_id);

    /* if there is a master then send the query to the slave with master_id */
    if (handle->master != NULL && (mysql_query(database->con, select_heartbeat_query) == 0
                                   && (result = mysql_store_result(database->con)) != NULL))
    {
        int rows_found = 0;

        while ((row = mysql_fetch_row(result)))
        {
            int rlag = MAX_RLAG_NOT_AVAILABLE;
            time_t slave_read;

            rows_found = 1;

            heartbeat = time(0);
            slave_read = strtoul(row[0], NULL, 10);

            if ((errno == ERANGE && (slave_read == LONG_MAX || slave_read == LONG_MIN)) || (errno != 0 &&
                                                                                            slave_read == 0))
            {
                slave_read = 0;
            }

            if (slave_read)
            {
                /* set the replication lag */
                rlag = heartbeat - slave_read;
            }

            /* set this node_ts as master_timestamp read from replication_heartbeat table */
            database->server->node_ts = slave_read;

            if (rlag >= 0)
            {
                /* store rlag only if greater than monitor sampling interval */
                database->server->rlag = ((unsigned int)rlag > (mon->interval / 1000)) ? rlag : 0;
            }
            else
            {
                database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
            }

            MXS_DEBUG("Slave %s:%i has %i seconds lag",
                      database->server->name,
                      database->server->port,
                      database->server->rlag);
        }
        if (!rows_found)
        {
            database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
            database->server->node_ts = 0;
        }

        mysql_free_result(result);
    }
    else
    {
        database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
        database->server->node_ts = 0;

        if (handle->master->server->node_id < 0)
        {
            MXS_ERROR("error: replication heartbeat: "
                      "master_server_id NOT available for %s:%i",
                      database->server->name,
                      database->server->port);
        }
        else
        {
            MXS_ERROR("error: replication heartbeat: "
                      "failed selecting from hearthbeat table of %s:%i : [%s], %s",
                      database->server->name,
                      database->server->port,
                      select_heartbeat_query,
                      mysql_error(database->con));
        }
    }
}

/*******
 * This function computes the replication tree
 * from a set of MySQL Master/Slave monitored servers
 * and returns the root server with SERVER_MASTER bit.
 * The tree is computed even for servers in 'maintenance' mode.
 *
 * @param handle    The monitor handle
 * @param num_servers   The number of servers monitored
 * @return      The server at root level with SERVER_MASTER bit
 */

static MXS_MONITORED_SERVER *get_replication_tree(MXS_MONITOR *mon, int num_servers)
{
    MYSQL_MONITOR* handle = (MYSQL_MONITOR*) mon->handle;
    MXS_MONITORED_SERVER *ptr;
    MXS_MONITORED_SERVER *backend;
    SERVER *current;
    int depth = 0;
    long node_id;
    int root_level;

    ptr = mon->monitored_servers;
    root_level = num_servers;

    while (ptr)
    {
        /* The server could be in SERVER_IN_MAINT
         * that means SERVER_IS_RUNNING returns 0
         * Let's check only for SERVER_IS_DOWN: server is not running
         */
        if (SERVER_IS_DOWN(ptr->server))
        {
            ptr = ptr->next;
            continue;
        }
        depth = 0;
        current = ptr->server;

        node_id = current->master_id;
        if (node_id < 1)
        {
            MXS_MONITORED_SERVER *find_slave;
            find_slave = getSlaveOfNodeId(mon->monitored_servers, current->node_id);

            if (find_slave == NULL)
            {
                current->depth = -1;
                ptr = ptr->next;

                continue;
            }
            else
            {
                current->depth = 0;
            }
        }
        else
        {
            depth++;
        }

        while (depth <= num_servers)
        {
            /* set the root master at lowest depth level */
            if (current->depth > -1 && current->depth < root_level)
            {
                root_level = current->depth;
                handle->master = ptr;
            }
            backend = getServerByNodeId(mon->monitored_servers, node_id);

            if (backend)
            {
                node_id = backend->server->master_id;
            }
            else
            {
                node_id = -1;
            }

            if (node_id > 0)
            {
                current->depth = depth + 1;
                depth++;

            }
            else
            {
                MXS_MONITORED_SERVER *master;
                current->depth = depth;

                master = getServerByNodeId(mon->monitored_servers, current->master_id);
                if (master && master->server && master->server->node_id > 0)
                {
                    add_slave_to_master(master->server->slaves, sizeof(master->server->slaves),
                                        current->node_id);
                    master->server->depth = current->depth - 1;

                    if (handle->master && master->server->depth < handle->master->server->depth)
                    {
                        /** A master with a lower depth was found, remove
                            the master status from the previous master. */
                        monitor_clear_pending_status(handle->master, SERVER_MASTER);
                    }

                    MYSQL_SERVER_INFO* info =
                        static_cast<MYSQL_SERVER_INFO*>(hashtable_fetch(handle->server_info,
                                                                        master->server->unique_name));
                    ss_dassert(info);

                    if (SERVER_IS_RUNNING(master->server))
                    {
                        /** Only set the Master status if read_only is disabled */
                        monitor_set_pending_status(master, info->read_only ? SERVER_SLAVE : SERVER_MASTER);
                    }

                    handle->master = master;
                }
                else
                {
                    if (current->master_id > 0)
                    {
                        if (handle->allow_external_slaves)
                        {
                            monitor_set_pending_status(ptr, SERVER_SLAVE);
                        }
                        else
                        {
                            monitor_clear_pending_status(ptr, SERVER_SLAVE);
                        }
                        monitor_set_pending_status(ptr, SERVER_SLAVE_OF_EXTERNAL_MASTER);
                    }
                }
                break;
            }

        }

        ptr = ptr->next;
    }

    /*
     * Return the root master
     */

    if (handle->master != NULL)
    {
        /* If the root master is in MAINT, return NULL */
        if (SERVER_IN_MAINT(handle->master->server))
        {
            return NULL;
        }
        else
        {
            return handle->master;
        }
    }
    else
    {
        return NULL;
    }
}

/*******
 * This function add a slave id into the slaves server field
 * of its master server
 *
 * @param slaves_list   The slave list array of the master server
 * @param list_size     The size of the slave list
 * @param node_id       The node_id of the slave to be inserted
 * @return      1 for inserted value and 0 otherwise
 */
static int add_slave_to_master(long *slaves_list, int list_size, long node_id)
{
    for (int i = 0; i < list_size; i++)
    {
        if (slaves_list[i] == 0)
        {
            slaves_list[i] = node_id;
            return 1;
        }
    }
    return 0;
}

/**
 * Check if replicate_ignore_table is defined and if maxscale_schema.replication_hearbeat
 * table is in the list.
 * @param database Server to check
 * @return False if the table is not replicated or an error occurred when querying
 * the server
 */
bool check_replicate_ignore_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mysql_query(database->con,
                    "show variables like 'replicate_ignore_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0 &&
                strcasestr(row[1], hb_table_name))
            {
                MXS_WARNING("'replicate_ignore_table' is "
                            "defined on server '%s' and '%s' was found in it. ",
                            database->server->unique_name, hb_table_name);
                rval = false;
            }
        }

        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_ignore_table': %s",
                  database->server->unique_name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_do_table is defined and if maxscale_schema.replication_hearbeat
 * table is not in the list.
 * @param database Server to check
 * @return False if the table is not replicated or an error occurred when querying
 * the server
 */
bool check_replicate_do_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mysql_query(database->con,
                    "show variables like 'replicate_do_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0 &&
                strcasestr(row[1], hb_table_name) == NULL)
            {
                MXS_WARNING("'replicate_do_table' is "
                            "defined on server '%s' and '%s' was not found in it. ",
                            database->server->unique_name, hb_table_name);
                rval = false;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_do_table': %s",
                  database->server->unique_name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_wild_do_table is defined and if it doesn't match
 * maxscale_schema.replication_heartbeat.
 * @param database Database server
 * @return False if the table is not replicated or an error occurred when trying to
 * query the server.
 */
bool check_replicate_wild_do_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mysql_query(database->con,
                    "show variables like 'replicate_wild_do_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0)
            {
                mxs_pcre2_result_t rc = modutil_mysql_wildcard_match(row[1], hb_table_name);
                if (rc == MXS_PCRE2_NOMATCH)
                {
                    MXS_WARNING("'replicate_wild_do_table' is "
                                "defined on server '%s' and '%s' does not match it. ",
                                database->server->unique_name,
                                hb_table_name);
                    rval = false;
                }
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_wild_do_table': %s",
                  database->server->unique_name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if replicate_wild_ignore_table is defined and if it matches
 * maxscale_schema.replication_heartbeat.
 * @param database Database server
 * @return False if the table is not replicated or an error occurred when trying to
 * query the server.
 */
bool check_replicate_wild_ignore_table(MXS_MONITORED_SERVER* database)
{
    MYSQL_RES *result;
    bool rval = true;

    if (mysql_query(database->con,
                    "show variables like 'replicate_wild_ignore_table'") == 0 &&
        (result = mysql_store_result(database->con)) &&
        mysql_num_fields(result) > 1)
    {
        MYSQL_ROW row;

        while ((row = mysql_fetch_row(result)))
        {
            if (strlen(row[1]) > 0)
            {
                mxs_pcre2_result_t rc = modutil_mysql_wildcard_match(row[1], hb_table_name);
                if (rc == MXS_PCRE2_MATCH)
                {
                    MXS_WARNING("'replicate_wild_ignore_table' is "
                                "defined on server '%s' and '%s' matches it. ",
                                database->server->unique_name,
                                hb_table_name);
                    rval = false;
                }
            }
        }
        mysql_free_result(result);
    }
    else
    {
        MXS_ERROR("Failed to query server %s for "
                  "'replicate_wild_do_table': %s",
                  database->server->unique_name,
                  mysql_error(database->con));
        rval = false;
    }
    return rval;
}

/**
 * Check if the maxscale_schema.replication_heartbeat table is replicated on all
 * servers and log a warning if problems were found.
 * @param monitor Monitor structure
 */
void check_maxscale_schema_replication(MXS_MONITOR *monitor)
{
    MXS_MONITORED_SERVER* database = monitor->monitored_servers;
    bool err = false;

    while (database)
    {
        mxs_connect_result_t rval = mon_ping_or_connect_to_db(monitor, database);
        if (rval == MONITOR_CONN_OK)
        {
            if (!check_replicate_ignore_table(database) ||
                !check_replicate_do_table(database) ||
                !check_replicate_wild_do_table(database) ||
                !check_replicate_wild_ignore_table(database))
            {
                err = true;
            }
        }
        else
        {
            mon_log_connect_error(database, rval);
        }
        database = database->next;
    }

    if (err)
    {
        MXS_WARNING("Problems were encountered when checking if '%s' is replicated. Make sure that "
                    "the table is replicated to all slaves.", hb_table_name);
    }
}
