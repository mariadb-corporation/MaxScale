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
 * @file monitor.c  - The monitor module management routines
 */
#include <maxscale/monitor.hh>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <set>
#include <zlib.h>
#include <sys/stat.h>
#include <vector>

#include <maxscale/alloc.h>
#include <maxscale/clock.h>
#include <maxscale/json_api.h>
#include <maxscale/log_manager.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/paths.h>
#include <maxscale/pcre2.h>
#include <maxscale/secrets.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.hh>
#include <maxscale/json_api.h>
#include <mysqld_error.h>

#include "internal/config.h"
#include "internal/externcmd.h"
#include "internal/monitor.h"
#include "internal/modules.h"

/** Schema version, journals must have a matching version */
#define MMB_SCHEMA_VERSION     2

/** Constants for byte lengths of the values */
#define MMB_LEN_BYTES          4
#define MMB_LEN_SCHEMA_VERSION 1
#define MMB_LEN_CRC32          4
#define MMB_LEN_VALUE_TYPE     1
#define MMB_LEN_SERVER_STATUS  8

/** Type of the stored value */
enum stored_value_type
{
    SVT_SERVER = 1, // Generic server state information
    SVT_MASTER = 2, // The master server name
};

using std::string;
using std::set;

const char CN_BACKEND_CONNECT_ATTEMPTS[]  = "backend_connect_attempts";
const char CN_BACKEND_CONNECT_TIMEOUT[]   = "backend_connect_timeout";
const char CN_BACKEND_READ_TIMEOUT[]      = "backend_read_timeout";
const char CN_BACKEND_WRITE_TIMEOUT[]     = "backend_write_timeout";
const char CN_DISK_SPACE_CHECK_INTERVAL[] = "disk_space_check_interval";
const char CN_EVENTS[]                    = "events";
const char CN_JOURNAL_MAX_AGE[]           = "journal_max_age";
const char CN_MONITOR_INTERVAL[]          = "monitor_interval";
const char CN_SCRIPT[]                    = "script";
const char CN_SCRIPT_TIMEOUT[]            = "script_timeout";

static MXS_MONITOR  *allMonitors = NULL;
static SPINLOCK monLock = SPINLOCK_INIT;

static void monitor_server_free_all(MXS_MONITORED_SERVER *servers);
static void remove_server_journal(MXS_MONITOR *monitor);
static bool journal_is_stale(MXS_MONITOR *monitor, time_t max_age);

/** Server type specific bits */
static uint64_t server_type_bits = SERVER_MASTER | SERVER_SLAVE | SERVER_JOINED | SERVER_NDB;

/** All server bits */
static uint64_t all_server_bits = SERVER_RUNNING |  SERVER_MAINT | SERVER_MASTER | SERVER_SLAVE |
                                  SERVER_JOINED | SERVER_NDB;

/**
 * Create a new monitor, load the associated module for the monitor
 * and start execution on the monitor.
 *
 * @param name          The name of the monitor module to load
 * @param module        The module to load
 * @return      The newly created monitor
 */
MXS_MONITOR* monitor_create(const char *name, const char *module)
{
    char* my_name = MXS_STRDUP(name);
    char *my_module = MXS_STRDUP(module);
    MXS_MONITOR *mon = (MXS_MONITOR *)MXS_MALLOC(sizeof(MXS_MONITOR));

    if (!mon || !my_module || !my_name)
    {
        MXS_FREE(mon);
        MXS_FREE(my_name);
        MXS_FREE(my_module);
        return NULL;
    }

    if ((mon->api = (MXS_MONITOR_API*)load_module(module, MODULE_MONITOR)) == NULL)
    {
        MXS_ERROR("Unable to load monitor module '%s'.", my_name);
        MXS_FREE(mon);
        MXS_FREE(my_module);
        MXS_FREE(my_name);
        return NULL;
    }
    mon->active = true;
    mon->state = MONITOR_STATE_ALLOC;
    mon->name = my_name;
    mon->module_name = my_module;
    mon->instance = NULL;
    mon->monitored_servers = NULL;
    *mon->password = '\0';
    *mon->user = '\0';
    mon->read_timeout = DEFAULT_READ_TIMEOUT;
    mon->write_timeout = DEFAULT_WRITE_TIMEOUT;
    mon->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
    mon->connect_attempts = DEFAULT_CONNECTION_ATTEMPTS;
    mon->interval = DEFAULT_MONITOR_INTERVAL;
    mon->journal_max_age = DEFAULT_JOURNAL_MAX_AGE;
    mon->script_timeout = DEFAULT_SCRIPT_TIMEOUT;
    mon->parameters = NULL;
    mon->check_maintenance_flag = MAINTENANCE_FLAG_NOCHECK;
    memset(mon->journal_hash, 0, sizeof(mon->journal_hash));
    mon->disk_space_threshold = NULL;
    mon->disk_space_check_interval = 0;
    spinlock_init(&mon->lock);

    if ((mon->instance = mon->api->createInstance(mon)) == NULL)
    {
        MXS_ERROR("Unable to create monitor instance for '%s', using module '%s'.",
                  name, module);
        MXS_FREE(mon);
        MXS_FREE(my_module);
        MXS_FREE(my_name);
    }

    spinlock_acquire(&monLock);
    mon->next = allMonitors;
    allMonitors = mon;
    spinlock_release(&monLock);

    return mon;
}

/**
 * Free a monitor, first stop the monitor and then remove the monitor from
 * the chain of monitors and free the memory.
 *
 * @param mon   The monitor to free
 */
void
monitor_destroy(MXS_MONITOR *mon)
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    if (allMonitors == mon)
    {
        allMonitors = mon->next;
    }
    else
    {
        ptr = allMonitors;
        while (ptr->next && ptr->next != mon)
        {
            ptr = ptr->next;
        }
        if (ptr->next)
        {
            ptr->next = mon->next;
        }
    }
    spinlock_release(&monLock);
    mon->api->destroyInstance(mon->instance);
    mon->state = MONITOR_STATE_FREED;
    delete mon->disk_space_threshold;
    config_parameter_free(mon->parameters);
    monitor_server_free_all(mon->monitored_servers);
    MXS_FREE(mon->name);
    MXS_FREE(mon->module_name);
    MXS_FREE(mon);
}

void monitor_destroy_all()
{
    // monitor_destroy() grabs 'monLock', so it cannot be grabbed here
    // without additional changes. But this function should only be
    // called at system shutdown in single-thread context.

    while (allMonitors)
    {
        MXS_MONITOR *monitor = allMonitors;
        monitor_destroy(monitor);
    }
}

/**
 * Start an individual monitor that has previously been stopped.
 *
 * @param monitor The Monitor that should be started
 */
void
monitor_start(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    if (monitor)
    {
        spinlock_acquire(&monitor->lock);

        if (journal_is_stale(monitor, monitor->journal_max_age))
        {
            MXS_WARNING("Removing stale journal file for monitor '%s'.", monitor->name);
            remove_server_journal(monitor);
        }

        if ((*monitor->api->startMonitor)(monitor->instance, params))
        {
            monitor->state = MONITOR_STATE_RUNNING;
        }
        else
        {
            MXS_ERROR("Failed to start monitor '%s'.", monitor->name);
        }

        spinlock_release(&monitor->lock);
    }
}

/**
 * Start all monitors
 */
void monitor_start_all()
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (ptr->active)
        {
            monitor_start(ptr, ptr->parameters);
        }
        ptr = ptr->next;
    }
    spinlock_release(&monLock);
}

/**
 * Stop a given monitor
 *
 * @param monitor       The monitor to stop
 */
void
monitor_stop(MXS_MONITOR *monitor)
{
    if (monitor)
    {
        spinlock_acquire(&monitor->lock);

        /** Only stop the monitor if it is running */
        if (monitor->state == MONITOR_STATE_RUNNING)
        {
            monitor->state = MONITOR_STATE_STOPPING;
            monitor->api->stopMonitor(monitor->instance);
            monitor->state = MONITOR_STATE_STOPPED;

            MXS_MONITORED_SERVER* db = monitor->monitored_servers;
            while (db)
            {
                // TODO: Create a generic entry point for this or move it inside stopMonitor
                mysql_close(db->con);
                db->con = NULL;
                db = db->next;
            }
        }

        spinlock_release(&monitor->lock);
    }
}

void monitor_deactivate(MXS_MONITOR* monitor)
{
    spinlock_acquire(&monLock);
    monitor->active = false;
    spinlock_release(&monLock);
}

/**
 * Shutdown all running monitors
 */
void
monitor_stop_all()
{
    spinlock_acquire(&monLock);

    MXS_MONITOR* monitor = allMonitors;
    while (monitor)
    {
        if (monitor->active)
        {
            monitor_stop(monitor);
        }
        monitor = monitor->next;
    }

    spinlock_release(&monLock);
}

/**
 * Add a server to a monitor. Simply register the server that needs to be
 * monitored to the running monitor module.
 *
 * @param mon           The Monitor instance
 * @param server        The Server to add to the monitoring
 */
bool monitor_add_server(MXS_MONITOR *mon, SERVER *server)
{
    bool rval = false;

    if (monitor_server_in_use(server))
    {
        MXS_ERROR("Server '%s' is already monitored.", server->name);
    }
    else
    {
        rval = true;
        MXS_MONITORED_SERVER *db = (MXS_MONITORED_SERVER *)MXS_MALLOC(sizeof(MXS_MONITORED_SERVER));
        MXS_ABORT_IF_NULL(db);

        db->server = server;
        db->con = NULL;
        db->next = NULL;
        db->mon_err_count = 0;
        db->log_version_err = true;
        db->new_event = true;

        /** Server status is uninitialized */
        db->mon_prev_status = -1;
        /* pending status is updated by get_replication_tree */
        db->pending_status = 0;

        monitor_state_t old_state = mon->state;

        if (old_state == MONITOR_STATE_RUNNING)
        {
            monitor_stop(mon);
        }

        spinlock_acquire(&mon->lock);

        if (mon->monitored_servers == NULL)
        {
            mon->monitored_servers = db;
        }
        else
        {
            MXS_MONITORED_SERVER *ptr = mon->monitored_servers;
            while (ptr->next != NULL)
            {
                ptr = ptr->next;
            }
            ptr->next = db;
        }
        spinlock_release(&mon->lock);

        if (old_state == MONITOR_STATE_RUNNING)
        {
            monitor_start(mon, mon->parameters);
        }
    }

    return rval;
}

static void monitor_server_free(MXS_MONITORED_SERVER *tofree)
{
    if (tofree)
    {
        if (tofree->con)
        {
            mysql_close(tofree->con);
        }
        MXS_FREE(tofree);
    }
}

/**
 * Free monitor server list
 * @param servers Servers to free
 */
static void monitor_server_free_all(MXS_MONITORED_SERVER *servers)
{
    while (servers)
    {
        MXS_MONITORED_SERVER *tofree = servers;
        servers = servers->next;
        monitor_server_free(tofree);
    }
}

/**
 * Remove a server from a monitor.
 *
 * @param mon           The Monitor instance
 * @param server        The Server to remove
 */
void monitor_remove_server(MXS_MONITOR *mon, SERVER *server)
{
    monitor_state_t old_state = mon->state;

    if (old_state == MONITOR_STATE_RUNNING)
    {
        monitor_stop(mon);
    }

    spinlock_acquire(&mon->lock);

    MXS_MONITORED_SERVER *ptr = mon->monitored_servers;

    if (ptr && ptr->server == server)
    {
        mon->monitored_servers = mon->monitored_servers->next;
    }
    else
    {
        MXS_MONITORED_SERVER *prev = ptr;

        while (ptr)
        {
            if (ptr->server == server)
            {
                prev->next = ptr->next;
                break;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    spinlock_release(&mon->lock);

    if (ptr)
    {
        monitor_server_free(ptr);
    }

    if (old_state == MONITOR_STATE_RUNNING)
    {
        monitor_start(mon, mon->parameters);
    }
}

/**
 * Add a default user to the monitor. This user is used to connect to the
 * monitored databases but may be overriden on a per server basis.
 *
 * @param mon           The monitor instance
 * @param user          The default username to use when connecting
 * @param passwd        The default password associated to the default user.
 */
void
monitor_add_user(MXS_MONITOR *mon, const char *user, const char *passwd)
{
    if (user != mon->user)
    {
        snprintf(mon->user, sizeof(mon->user), "%s", user);
    }

    if (passwd != mon->password)
    {
        snprintf(mon->password, sizeof(mon->password), "%s", passwd);
    }
}

/**
 * Show all monitors
 *
 * @param dcb   DCB for printing output
 */
void
monitor_show_all(DCB *dcb)
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (ptr->active)
        {
            monitor_show(dcb, ptr);
        }
        ptr = ptr->next;
    }
    spinlock_release(&monLock);
}

/**
 * Show a single monitor
 *
 * @param dcb   DCB for printing output
 */
void
monitor_show(DCB *dcb, MXS_MONITOR *monitor)
{
    const char *state;

    switch (monitor->state)
    {
    case MONITOR_STATE_RUNNING:
        state = "Running";
        break;
    case MONITOR_STATE_STOPPING:
        state = "Stopping";
        break;
    case MONITOR_STATE_STOPPED:
        state = "Stopped";
        break;
    case MONITOR_STATE_ALLOC:
        state = "Allocated";
        break;
    default:
        state = "Unknown";
        break;
    }

    dcb_printf(dcb, "Monitor:                %p\n", monitor);
    dcb_printf(dcb, "Name:                   %s\n", monitor->name);
    dcb_printf(dcb, "State:                  %s\n", state);
    dcb_printf(dcb, "Sampling interval:      %lu milliseconds\n", monitor->interval);
    dcb_printf(dcb, "Connect Timeout:        %i seconds\n", monitor->connect_timeout);
    dcb_printf(dcb, "Read Timeout:           %i seconds\n", monitor->read_timeout);
    dcb_printf(dcb, "Write Timeout:          %i seconds\n", monitor->write_timeout);
    dcb_printf(dcb, "Connect attempts:       %i \n", monitor->connect_attempts);
    dcb_printf(dcb, "Monitored servers:      ");

    const char *sep = "";

    for (MXS_MONITORED_SERVER *db = monitor->monitored_servers; db; db = db->next)
    {
        dcb_printf(dcb, "%s[%s]:%d", sep, db->server->address, db->server->port);
        sep = ", ";
    }

    dcb_printf(dcb, "\n");

    if (monitor->instance)
    {
        if (monitor->api->diagnostics)
        {
            monitor->api->diagnostics(monitor->instance, dcb);
        }
        else
        {
            dcb_printf(dcb, " (no diagnostics)\n");
        }
    }
    else
    {
        dcb_printf(dcb, " Monitor failed\n");
    }
    dcb_printf(dcb, "\n");
}

/**
 * List all the monitors
 *
 * @param dcb   DCB for printing output
 */
void
monitor_list(DCB *dcb)
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    dcb_printf(dcb, "---------------------+---------------------\n");
    dcb_printf(dcb, "%-20s | Status\n", "Monitor");
    dcb_printf(dcb, "---------------------+---------------------\n");
    while (ptr)
    {
        if (ptr->active)
        {
            dcb_printf(dcb, "%-20s | %s\n", ptr->name,
                       ptr->state & MONITOR_STATE_RUNNING
                       ? "Running" : "Stopped");
        }
        ptr = ptr->next;
    }
    dcb_printf(dcb, "---------------------+---------------------\n");
    spinlock_release(&monLock);
}

/**
 * Find a monitor by name
 *
 * @param       name    The name of the monitor
 * @return      Pointer to the monitor or NULL
 */
MXS_MONITOR *
monitor_find(const char *name)
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (!strcmp(ptr->name, name) && ptr->active)
        {
            break;
        }
        ptr = ptr->next;
    }
    spinlock_release(&monLock);
    return ptr;
}
/**
 * Find a destroyed monitor by name
 *
 * @param name The name of the monitor
 * @return  Pointer to the destroyed monitor or NULL if monitor is not found
 */
MXS_MONITOR* monitor_repurpose_destroyed(const char* name, const char* module)
{
    MXS_MONITOR* rval = NULL;

    spinlock_acquire(&monLock);

    for (MXS_MONITOR *ptr = allMonitors; ptr; ptr = ptr->next)
    {
        if (strcmp(ptr->name, name) == 0 && strcmp(ptr->module_name, module) == 0)
        {
            ss_dassert(!ptr->active);
            ptr->active = true;
            rval = ptr;
        }
    }

    spinlock_release(&monLock);

    return rval;
}

/**
 * Set the monitor sampling interval.
 *
 * @param mon           The monitor instance
 * @param interval      The sampling interval in milliseconds
 */
void
monitor_set_interval(MXS_MONITOR *mon, unsigned long interval)
{
    mon->interval = interval;
}

/**
 * Set the maximum age of the monitor journal
 *
 * @param mon           The monitor instance
 * @param interval      The journal age in seconds
 */
void monitor_set_journal_max_age(MXS_MONITOR *mon, time_t value)
{
    mon->journal_max_age = value;
}

void monitor_set_script_timeout(MXS_MONITOR *mon, uint32_t value)
{
    mon->script_timeout = value;
}

/**
 * Set Monitor timeouts for connect/read/write
 *
 * @param mon           The monitor instance
 * @param type          The timeout handling type
 * @param value         The timeout to set
 */
bool monitor_set_network_timeout(MXS_MONITOR *mon, int type, int value, const char* key)
{
    bool rval = true;

    if (value > 0)
    {
        switch (type)
        {
        case MONITOR_CONNECT_TIMEOUT:
            mon->connect_timeout = value;
            break;

        case MONITOR_READ_TIMEOUT:
            mon->read_timeout = value;
            break;

        case MONITOR_WRITE_TIMEOUT:
            mon->write_timeout = value;
            break;

        case MONITOR_CONNECT_ATTEMPTS:
            mon->connect_attempts = value;
            break;

        default:
            MXS_ERROR("Monitor setNetworkTimeout received an unsupported action type %i", type);
            ss_dassert(!true);
            rval = false;
            break;
        }
    }
    else
    {
        MXS_ERROR("Value '%s' for monitor '%s' is not a positive integer: %d", key, mon->name, value);
        rval = false;
    }
    return rval;
}

/**
 * Provide a row to the result set that defines the set of monitors
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
monitorRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    int i = 0;;
    char buf[20];
    RESULT_ROW *row;
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (i < *rowno && ptr)
    {
        i++;
        ptr = ptr->next;
    }
    if (ptr == NULL)
    {
        spinlock_release(&monLock);
        MXS_FREE(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, ptr->name);
    resultset_row_set(row, 1, ptr->state & MONITOR_STATE_RUNNING
                      ? "Running" : "Stopped");
    spinlock_release(&monLock);
    return row;
}

/**
 * Return a resultset that has the current set of monitors in it
 *
 * @return A Result set
 */
RESULTSET *
monitor_get_list()
{
    RESULTSET *set;
    int *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(monitorRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Monitor", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Status", 10, COL_TYPE_VARCHAR);

    return set;
}

/**
 * @brief Check if the monitor user has all required permissions to operate properly.
 *
 * @param service Monitor to inspect
 * @param query Query to execute
 * @return True on success, false if monitor credentials lack permissions
 */
bool check_monitor_permissions(MXS_MONITOR* monitor, const char* query)
{
    if (monitor->monitored_servers == NULL || // No servers to check
        config_get_global_options()->skip_permission_checks)
    {
        return true;
    }

    char *user = monitor->user;
    char *dpasswd = decrypt_password(monitor->password);
    MXS_CONFIG* cnf = config_get_global_options();
    bool rval = false;

    for (MXS_MONITORED_SERVER *mondb = monitor->monitored_servers; mondb; mondb = mondb->next)
    {
        if (!mon_connection_is_ok(mon_ping_or_connect_to_db(monitor, mondb)))
        {
            MXS_ERROR("[%s] Failed to connect to server '%s' ([%s]:%d) when"
                      " checking monitor user credentials and permissions: %s",
                      monitor->name, mondb->server->name, mondb->server->address,
                      mondb->server->port, mysql_error(mondb->con));
            switch (mysql_errno(mondb->con))
            {
            case ER_ACCESS_DENIED_ERROR:
            case ER_DBACCESS_DENIED_ERROR:
            case ER_ACCESS_DENIED_NO_PASSWORD_ERROR:
                break;
            default:
                rval = true;
                break;
            }
        }
        else if (mxs_mysql_query(mondb->con, query) != 0)
        {
            switch (mysql_errno(mondb->con))
            {
            case ER_TABLEACCESS_DENIED_ERROR:
            case ER_COLUMNACCESS_DENIED_ERROR:
            case ER_SPECIFIC_ACCESS_DENIED_ERROR:
            case ER_PROCACCESS_DENIED_ERROR:
            case ER_KILL_DENIED_ERROR:
                rval = false;
                break;

            default:
                rval = true;
                break;
            }

            MXS_ERROR("[%s] Failed to execute query '%s' with user '%s'. MySQL error message: %s",
                      monitor->name, query, user, mysql_error(mondb->con));
        }
        else
        {
            rval = true;
            MYSQL_RES *res = mysql_use_result(mondb->con);
            if (res == NULL)
            {
                MXS_ERROR("[%s] Result retrieval failed when checking monitor permissions: %s",
                          monitor->name, mysql_error(mondb->con));
            }
            else
            {
                mysql_free_result(res);
            }
        }
    }

    MXS_FREE(dpasswd);
    return rval;
}

/**
 * Add parameters to the monitor
 * @param monitor Monitor
 * @param params Config parameters
 */
void monitor_add_parameters(MXS_MONITOR *monitor, MXS_CONFIG_PARAMETER *params)
{
    spinlock_acquire(&monitor->lock);

    while (params)
    {
        MXS_CONFIG_PARAMETER* old = config_get_param(monitor->parameters, params->name);

        if (old)
        {
            MXS_FREE(old->value);
            old->value = MXS_STRDUP_A(params->value);
        }
        else
        {
            MXS_CONFIG_PARAMETER* clone = config_clone_param(params);
            clone->next = monitor->parameters;
            monitor->parameters = clone;
        }

        params = params->next;
    }

    spinlock_release(&monitor->lock);
}

bool monitor_remove_parameter(MXS_MONITOR *monitor, const char *key)
{
    MXS_CONFIG_PARAMETER *prev = NULL;
    bool rval = false;

    spinlock_acquire(&monitor->lock);

    for (MXS_CONFIG_PARAMETER *p = monitor->parameters; p; p = p->next)
    {
        if (strcmp(p->name, key) == 0)
        {
            if (p == monitor->parameters)
            {
                monitor->parameters = monitor->parameters->next;
            }
            else
            {
                prev->next = p->next;
            }

            p->next = NULL;
            config_parameter_free(p);
            rval = true;
            break;
        }

        prev = p;
    }

    spinlock_release(&monitor->lock);

    return rval;
}

void mon_alter_parameter(MXS_MONITOR* monitor, const char* key, const char* value)
{
    spinlock_acquire(&monitor->lock);

    for (MXS_CONFIG_PARAMETER* p = monitor->parameters; p; p = p->next)
    {
        if (strcmp(p->name, key) == 0)
        {
            MXS_FREE(p->value);
            p->value = MXS_STRDUP_A(value);
            break;
        }
    }

    spinlock_release(&monitor->lock);
}

/**
 * Set pending status bits in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bits to set for the server
 */
void monitor_set_pending_status(MXS_MONITORED_SERVER *ptr, uint64_t bit)
{
    ptr->pending_status |= bit;
}

/**
 * Clear pending status bits in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bits to clear for the server
 */
void monitor_clear_pending_status(MXS_MONITORED_SERVER *ptr, uint64_t bit)
{
    ptr->pending_status &= ~bit;
}

/*
 * Determine a monitor event, defined by the difference between the old
 * status of a server and the new status.
 *
 * @param   node                The monitor server data for a particular server
 * @result  monitor_event_t     A monitor event (enum)
 *
 * @note This function must only be called from mon_process_state_changes
 */
static mxs_monitor_event_t mon_get_event_type(MXS_MONITORED_SERVER* node)
{
    typedef enum
    {
        DOWN_EVENT,
        UP_EVENT,
        LOSS_EVENT,
        NEW_EVENT,
        UNSUPPORTED_EVENT
    } general_event_type;

    general_event_type event_type = UNSUPPORTED_EVENT;

    uint64_t prev = node->mon_prev_status & all_server_bits;
    uint64_t present = node->server->status & all_server_bits;

    if (prev == present)
    {
        /* This should never happen */
        ss_dassert(false);
        return UNDEFINED_EVENT;
    }

    if ((prev & SERVER_RUNNING) == 0)
    {
        /* The server was not running previously */
        if ((present & SERVER_RUNNING) != 0)
        {
            event_type = UP_EVENT;
        }
        else
        {
            /* Otherwise, was not running and still is not running. This should never happen. */
            ss_dassert(false);
        }
    }
    else
    {
        /* Previous state must have been running */
        if ((present & SERVER_RUNNING) == 0)
        {
            event_type = DOWN_EVENT;
        }
        else
        {
            /** These are used to detect whether we actually lost something or
             * just transitioned from one state to another */
            uint64_t prev_bits = prev & (SERVER_MASTER | SERVER_SLAVE);
            uint64_t present_bits = present & (SERVER_MASTER | SERVER_SLAVE);

            /* Was running and still is */
            if ((!prev_bits || !present_bits || prev_bits == present_bits) &&
                (prev & server_type_bits))
            {
                /* We used to know what kind of server it was */
                event_type = LOSS_EVENT;
            }
            else
            {
                /* We didn't know what kind of server it was, now we do*/
                event_type = NEW_EVENT;
            }
        }
    }

    mxs_monitor_event_t rval = UNDEFINED_EVENT;

    switch (event_type)
    {
    case UP_EVENT:
        rval = (present & SERVER_MASTER) ? MASTER_UP_EVENT :
               (present & SERVER_SLAVE) ? SLAVE_UP_EVENT :
               (present & SERVER_JOINED) ? SYNCED_UP_EVENT :
               (present & SERVER_NDB) ? NDB_UP_EVENT :
               SERVER_UP_EVENT;
        break;

    case DOWN_EVENT:
        rval = (prev & SERVER_MASTER) ? MASTER_DOWN_EVENT :
               (prev & SERVER_SLAVE) ? SLAVE_DOWN_EVENT :
               (prev & SERVER_JOINED) ? SYNCED_DOWN_EVENT :
               (prev & SERVER_NDB) ? NDB_DOWN_EVENT :
               SERVER_DOWN_EVENT;
        break;

    case LOSS_EVENT:
        rval = (prev & SERVER_MASTER) ? LOST_MASTER_EVENT :
               (prev & SERVER_SLAVE) ? LOST_SLAVE_EVENT :
               (prev & SERVER_JOINED) ? LOST_SYNCED_EVENT :
               (prev & SERVER_NDB) ? LOST_NDB_EVENT :
               UNDEFINED_EVENT;
        break;

    case NEW_EVENT:
        rval = (present & SERVER_MASTER) ? NEW_MASTER_EVENT :
               (present & SERVER_SLAVE) ? NEW_SLAVE_EVENT :
               (present & SERVER_JOINED) ? NEW_SYNCED_EVENT :
               (present & SERVER_NDB) ? NEW_NDB_EVENT :
               UNDEFINED_EVENT;
        break;

    default:
        /* This should never happen */
        ss_dassert(false);
        break;
    }

    ss_dassert(rval != UNDEFINED_EVENT);
    return rval;
}

const char* mon_get_event_name(mxs_monitor_event_t event)
{
    for (int i = 0; mxs_monitor_event_enum_values[i].name; i++)
    {
        if (mxs_monitor_event_enum_values[i].enum_value & event)
        {
            return mxs_monitor_event_enum_values[i].name;
        }
    }

    ss_dassert(false);
    return "undefined_event";
}

/*
 * Given a monitor event (enum) provide a text string equivalent
 * @param   node    The monitor server data whose event is wanted
 * @result  string  The name of the monitor event for the server
 */
static const char* mon_get_event_name(MXS_MONITORED_SERVER* node)
{
    return mon_get_event_name((mxs_monitor_event_t)node->server->last_event);
}

enum credentials_approach_t
{
    CREDENTIALS_INCLUDE,
    CREDENTIALS_EXCLUDE,
};

/**
 * Create a list of running servers
 *
 * @param mon The monitor
 * @param dest Destination where the string is appended, must be null terminated
 * @param len Length of @c dest
 * @param approach Whether credentials should be included or not.
 */
static void mon_append_node_names(MXS_MONITOR* mon,
                                  char* dest,
                                  int len,
                                  int status,
                                  credentials_approach_t approach = CREDENTIALS_EXCLUDE)
{
    MXS_MONITORED_SERVER* servers = mon->monitored_servers;

    const char *separator = "";
    char arr[MAX_SERVER_MONUSER_LEN +
             MAX_SERVER_MONPW_LEN +
             MAX_SERVER_ADDRESS_LEN + 64]; // Some extra space for port and separator
    dest[0] = '\0';

    while (servers && len)
    {
        if (status == 0 || servers->server->status & status)
        {
            if (approach == CREDENTIALS_EXCLUDE)
            {
                snprintf(arr, sizeof(arr), "%s[%s]:%d", separator, servers->server->address,
                         servers->server->port);
            }
            else
            {
                const char* user;
                const char* password;
                if (*servers->server->monuser)
                {
                    user = servers->server->monuser;
                    password = servers->server->monpw;
                }
                else
                {
                    user = mon->user;
                    password = mon->password;
                }

                snprintf(arr, sizeof(arr), "%s%s:%s@[%s]:%d",
                         separator,
                         user,
                         password,
                         servers->server->address,
                         servers->server->port);
            }

            separator = ",";
            int arrlen = strlen(arr);

            if (arrlen < len)
            {
                strcat(dest, arr);
                len -= arrlen;
            }
        }
        servers = servers->next;
    }
}

/**
 * Check if current monitored server status has changed
 *
 * @param mon_srv       The monitored server
 * @return              true if status has changed or false
 */
bool mon_status_changed(MXS_MONITORED_SERVER* mon_srv)
{
    bool rval = false;

    /* Previous status is -1 if not yet set */
    if (mon_srv->mon_prev_status != static_cast<uint64_t>(-1))
    {

        uint64_t old_status = mon_srv->mon_prev_status & all_server_bits;
        uint64_t new_status = mon_srv->server->status & all_server_bits;

        /**
         * The state has changed if the relevant state bits are not the same,
         * the server is either running, stopping or starting and the server is
         * not going into maintenance or coming out of it
         */
        if (old_status != new_status &&
            ((old_status | new_status) & SERVER_MAINT) == 0 &&
            ((old_status | new_status) & SERVER_RUNNING) == SERVER_RUNNING)
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * Check if current monitored server has a loggable failure status
 *
 * @param mon_srv       The monitored server
 * @return              true if failed status can be logged or false
 */
bool
mon_print_fail_status(MXS_MONITORED_SERVER* mon_srv)
{
    return (SERVER_IS_DOWN(mon_srv->server) && mon_srv->mon_err_count == 0);
}

static MXS_MONITORED_SERVER* find_parent_node(MXS_MONITORED_SERVER* servers,
                                             MXS_MONITORED_SERVER* target)
{
    MXS_MONITORED_SERVER* rval = NULL;

    if (target->server->master_id > 0)
    {
        for (MXS_MONITORED_SERVER* node = servers; node; node = node->next)
        {
            if (node->server->node_id == target->server->master_id)
            {
                rval = node;
                break;
            }
        }
    }

    return rval;
}

static std::string child_nodes(MXS_MONITORED_SERVER* servers,
                               MXS_MONITORED_SERVER* parent)
{
    std::stringstream ss;

    if (parent->server->node_id > 0)
    {
        bool have_content = false;

        for (MXS_MONITORED_SERVER* node = servers; node; node = node->next)
        {
            if (node->server->master_id == parent->server->node_id)
            {
                if (have_content)
                {
                    ss << ",";
                }

                ss << "[" << node->server->address << "]:" << node->server->port;
                have_content = true;
            }
        }
    }

    return ss.str();
}

int monitor_launch_command(MXS_MONITOR* mon, MXS_MONITORED_SERVER* ptr, EXTERNCMD* cmd)
{
    if (externcmd_matches(cmd, "$INITIATOR"))
    {
        char initiator[strlen(ptr->server->address) + 24]; // Extra space for port
        snprintf(initiator, sizeof(initiator), "[%s]:%d", ptr->server->address, ptr->server->port);
        externcmd_substitute_arg(cmd, "[$]INITIATOR", initiator);
    }

    if (externcmd_matches(cmd, "$PARENT"))
    {
        std::stringstream ss;
        MXS_MONITORED_SERVER* parent = find_parent_node(mon->monitored_servers, ptr);

        if (parent)
        {
            ss << "[" << parent->server->address << "]:" << parent->server->port;
        }
        externcmd_substitute_arg(cmd, "[$]PARENT", ss.str().c_str());
    }

    if (externcmd_matches(cmd, "$CHILDREN"))
    {
        externcmd_substitute_arg(cmd, "[$]CHILDREN", child_nodes(mon->monitored_servers, ptr).c_str());
    }

    if (externcmd_matches(cmd, "$EVENT"))
    {
        externcmd_substitute_arg(cmd, "[$]EVENT", mon_get_event_name(ptr));
    }

    char nodelist[PATH_MAX + MON_ARG_MAX + 1] = {'\0'};

    if (externcmd_matches(cmd, "$CREDENTIALS"))
    {
        // We provide the credentials for _all_ servers.
        mon_append_node_names(mon, nodelist, sizeof(nodelist), 0, CREDENTIALS_INCLUDE);
        externcmd_substitute_arg(cmd, "[$]CREDENTIALS", nodelist);
    }

    if (externcmd_matches(cmd, "$NODELIST"))
    {
        mon_append_node_names(mon, nodelist, sizeof(nodelist), SERVER_RUNNING);
        externcmd_substitute_arg(cmd, "[$]NODELIST", nodelist);
    }

    if (externcmd_matches(cmd, "$LIST"))
    {
        mon_append_node_names(mon, nodelist, sizeof(nodelist), 0);
        externcmd_substitute_arg(cmd, "[$]LIST", nodelist);
    }

    if (externcmd_matches(cmd, "$MASTERLIST"))
    {
        mon_append_node_names(mon, nodelist, sizeof(nodelist), SERVER_MASTER);
        externcmd_substitute_arg(cmd, "[$]MASTERLIST", nodelist);
    }

    if (externcmd_matches(cmd, "$SLAVELIST"))
    {
        mon_append_node_names(mon, nodelist, sizeof(nodelist), SERVER_SLAVE);
        externcmd_substitute_arg(cmd, "[$]SLAVELIST", nodelist);
    }

    if (externcmd_matches(cmd, "$SYNCEDLIST"))
    {
        mon_append_node_names(mon, nodelist, sizeof(nodelist), SERVER_JOINED);
        externcmd_substitute_arg(cmd, "[$]SYNCEDLIST", nodelist);
    }

    int rv = externcmd_execute(cmd);

    if (rv)
    {
        if (rv == -1)
        {
            // Internal error
            MXS_ERROR("Failed to execute script '%s' on server state change event '%s'",
                      cmd->argv[0], mon_get_event_name(ptr));
        }
        else
        {
            // Script returned a non-zero value
            MXS_ERROR("Script '%s' returned %d on event '%s'",
                      cmd->argv[0], rv, mon_get_event_name(ptr));
        }
    }
    else
    {
        ss_dassert(cmd->argv != NULL && cmd->argv[0] != NULL);
        // Construct a string with the script + arguments
        char *scriptStr = NULL;
        int totalStrLen = 0;
        bool memError = false;
        for (int i = 0; cmd->argv[i]; i++)
        {
            totalStrLen += strlen(cmd->argv[i]) + 1; // +1 for space and one \0
        }
        int spaceRemaining = totalStrLen;
        if ((scriptStr = (char*)MXS_CALLOC(totalStrLen, sizeof(char))) != NULL)
        {
            char *currentPos = scriptStr;
            // The script name should not begin with a space
            int len = snprintf(currentPos, spaceRemaining, "%s", cmd->argv[0]);
            currentPos += len;
            spaceRemaining -= len;

            for (int i = 1; cmd->argv[i]; i++)
            {
                if ((cmd->argv[i])[0] == '\0')
                {
                    continue; // Empty argument, print nothing
                }
                len = snprintf(currentPos, spaceRemaining, " %s", cmd->argv[i]);
                currentPos += len;
                spaceRemaining -= len;
            }
            ss_dassert(spaceRemaining > 0);
            *currentPos = '\0';
        }
        else
        {
            memError = true;
            scriptStr = cmd->argv[0]; // print at least something
        }

        MXS_NOTICE("Executed monitor script '%s' on event '%s'",
                   scriptStr, mon_get_event_name(ptr));

        if (!memError)
        {
            MXS_FREE(scriptStr);
        }
    }

    return rv;
}

int monitor_launch_script(MXS_MONITOR* mon, MXS_MONITORED_SERVER* ptr, const char* script, uint32_t timeout)
{
    char arg[strlen(script) + 1];
    strcpy(arg, script);

    EXTERNCMD* cmd = externcmd_allocate(arg, timeout);

    if (cmd == NULL)
    {
        MXS_ERROR("Failed to initialize script '%s'. See previous errors for the "
                  "cause of this failure.", script);
        return -1;
    }

    int rv = monitor_launch_command(mon, ptr, cmd);

    externcmd_free(cmd);

    return rv;
}

/**
 * Ping or connect to a database. If connection does not exist or ping fails, a new connection is created.
 * This will always leave a valid database handle in the database->con pointer, allowing the user to call
 * MySQL C API functions to find out the reason of the failure.
 *
 * @param mon Monitor
 * @param database Monitored database
 * @return Connection status.
 */
mxs_connect_result_t mon_ping_or_connect_to_db(MXS_MONITOR* mon, MXS_MONITORED_SERVER *database)
{
    if (database->con)
    {
        /** Return if the connection is OK */
        if (mysql_ping(database->con) == 0)
        {
            return MONITOR_CONN_EXISTING_OK;
        }
        /** Otherwise close the handle. */
        mysql_close(database->con);
    }

    mxs_connect_result_t conn_result = MONITOR_CONN_REFUSED;
    if ((database->con = mysql_init(NULL)))
    {
        char *uname = mon->user;
        char *passwd = mon->password;

        if (database->server->monuser[0] && database->server->monpw[0])
        {
            uname = database->server->monuser;
            passwd = database->server->monpw;
        }

        char *dpwd = decrypt_password(passwd);

        mysql_optionsv(database->con, MYSQL_OPT_CONNECT_TIMEOUT, (void *) &mon->connect_timeout);
        mysql_optionsv(database->con, MYSQL_OPT_READ_TIMEOUT, (void *) &mon->read_timeout);
        mysql_optionsv(database->con, MYSQL_OPT_WRITE_TIMEOUT, (void *) &mon->write_timeout);
        mysql_optionsv(database->con, MYSQL_PLUGIN_DIR, get_connector_plugindir());

        time_t start = 0;
        time_t end = 0;
        for (int i = 0; i < mon->connect_attempts; i++)
        {
            start = time(NULL);
            bool result = (mxs_mysql_real_connect(database->con, database->server, uname, dpwd) != NULL);
            end = time(NULL);

            if (result)
            {
                conn_result = MONITOR_CONN_NEWCONN_OK;
                break;
            }
        }

        if (conn_result == MONITOR_CONN_REFUSED && (int)difftime(end, start) >= mon->connect_timeout)
        {
            conn_result = MONITOR_CONN_TIMEOUT;
        }
        MXS_FREE(dpwd);
    }

    return conn_result;
}

/**
 * Is the return value one of the 'OK' values.
 *
 * @param connect_result Return value of mon_ping_or_connect_to_db
 * @return True of connection is ok
 */
bool mon_connection_is_ok(mxs_connect_result_t connect_result)
{
    return (connect_result == MONITOR_CONN_EXISTING_OK || connect_result == MONITOR_CONN_NEWCONN_OK);
}

/**
 * Log an error about the failure to connect to a backend server and why it happened.
 *
 * @param database Backend database
 * @param rval Return value of mon_ping_or_connect_to_db
 */
void mon_log_connect_error(MXS_MONITORED_SERVER* database, mxs_connect_result_t rval)
{
    ss_dassert(!mon_connection_is_ok(rval) && database);
    const char TIMED_OUT[] = "Monitor timed out when connecting to server %s[%s:%d] : '%s'";
    const char REFUSED[] = "Monitor was unable to connect to server %s[%s:%d] : '%s'";
    auto srv = database->server;
    MXS_ERROR(rval == MONITOR_CONN_TIMEOUT ? TIMED_OUT : REFUSED,
              srv->name, srv->address, srv->port, mysql_error(database->con));
}

static void mon_log_state_change(MXS_MONITORED_SERVER *ptr)
{
    SERVER srv;
    srv.status = ptr->mon_prev_status;
    char *prev = server_status(&srv);
    char *next = server_status(ptr->server);
    MXS_NOTICE("Server changed state: %s[%s:%u]: %s. [%s] -> [%s]",
               ptr->server->name, ptr->server->address, ptr->server->port,
               mon_get_event_name(ptr), prev, next);
    MXS_FREE(prev);
    MXS_FREE(next);
}

MXS_MONITOR* monitor_server_in_use(const SERVER *server)
{
    MXS_MONITOR *rval = NULL;

    spinlock_acquire(&monLock);

    for (MXS_MONITOR *mon = allMonitors; mon && !rval; mon = mon->next)
    {
        spinlock_acquire(&mon->lock);

        if (mon->active)
        {
            for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db && !rval; db = db->next)
            {
                if (db->server == server)
                {
                    rval = mon;
                }
            }
        }

        spinlock_release(&mon->lock);
    }

    spinlock_release(&monLock);

    return rval;
}

static bool create_monitor_config(const MXS_MONITOR *monitor, const char *filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing monitor '%s': %d, %s",
                  filename, monitor->name, errno, mxs_strerror(errno));
        return false;
    }

    spinlock_acquire(&monitor->lock);

    dprintf(file, "[%s]\n", monitor->name);
    dprintf(file, "%s=monitor\n", CN_TYPE);
    dprintf(file, "%s=%s\n", CN_MODULE, monitor->module_name);
    dprintf(file, "%s=%s\n", CN_USER, monitor->user);
    dprintf(file, "%s=%s\n", CN_PASSWORD, monitor->password);
    dprintf(file, "%s=%lu\n", CN_MONITOR_INTERVAL, monitor->interval);
    dprintf(file, "%s=%d\n", CN_BACKEND_CONNECT_TIMEOUT, monitor->connect_timeout);
    dprintf(file, "%s=%d\n", CN_BACKEND_WRITE_TIMEOUT, monitor->write_timeout);
    dprintf(file, "%s=%d\n", CN_BACKEND_READ_TIMEOUT, monitor->read_timeout);
    dprintf(file, "%s=%d\n", CN_BACKEND_CONNECT_ATTEMPTS, monitor->connect_attempts);
    dprintf(file, "%s=%ld\n", CN_JOURNAL_MAX_AGE, monitor->journal_max_age);
    dprintf(file, "%s=%d\n", CN_SCRIPT_TIMEOUT, monitor->script_timeout);

    if (monitor->monitored_servers)
    {
        dprintf(file, "%s=", CN_SERVERS);
        for (MXS_MONITORED_SERVER *db = monitor->monitored_servers; db; db = db->next)
        {
            if (db != monitor->monitored_servers)
            {
                dprintf(file, ",");
            }
            dprintf(file, "%s", db->server->name);
        }
        dprintf(file, "\n");
    }

    const char* params[] =
    {
        CN_TYPE,
        CN_MODULE,
        CN_USER,
        CN_PASSWORD,
        "passwd", // TODO: Remove this
        CN_MONITOR_INTERVAL,
        CN_BACKEND_CONNECT_TIMEOUT,
        CN_BACKEND_WRITE_TIMEOUT,
        CN_BACKEND_READ_TIMEOUT,
        CN_BACKEND_CONNECT_ATTEMPTS,
        CN_JOURNAL_MAX_AGE,
        CN_SCRIPT_TIMEOUT,
        CN_SERVERS
    };

    std::set<std::string> param_set(params, params + sizeof(params) / sizeof(params[0]));

    for (MXS_CONFIG_PARAMETER* p = monitor->parameters; p; p = p->next)
    {
        if (param_set.find(p->name) == param_set.end())
        {
            dprintf(file, "%s=%s\n", p->name, p->value);
        }
    }

    spinlock_release(&monitor->lock);

    close(file);

    return true;
}

bool monitor_serialize(const MXS_MONITOR *monitor)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             monitor->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary monitor configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_monitor_config(monitor, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        ss_dassert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary monitor configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

void mon_hangup_failed_servers(MXS_MONITOR *monitor)
{
    for (MXS_MONITORED_SERVER *ptr = monitor->monitored_servers; ptr; ptr = ptr->next)
    {
        if (mon_status_changed(ptr) &&
            (!(SERVER_IS_RUNNING(ptr->server)) ||
             !(SERVER_IS_IN_CLUSTER(ptr->server))))
        {
            dcb_hangup_foreach(ptr->server);
        }
    }
}

void mon_report_query_error(MXS_MONITORED_SERVER* db)
{
    MXS_ERROR("Failed to execute query on server '%s' ([%s]:%d): %s",
              db->server->name, db->server->address,
              db->server->port, mysql_error(db->con));
}

/**
  * Check if admin is requesting setting or clearing maintenance status on the server and act accordingly.
  * Should be called at the beginning of a monitor loop.
  *
  * @param monitor The target monitor
  */
void monitor_check_maintenance_requests(MXS_MONITOR *monitor)
{
    /* In theory, the admin may be modifying the server maintenance status during this function. The overall
     * maintenance flag should be read-written atomically to prevent missing a value. */
    int flags_changed = atomic_exchange_int(&monitor->check_maintenance_flag, MAINTENANCE_FLAG_NOCHECK);
    if (flags_changed != MAINTENANCE_FLAG_NOCHECK)
    {
        MXS_MONITORED_SERVER *ptr = monitor->monitored_servers;
        while (ptr)
        {
            // The only server status bit the admin may change is the [Maintenance] bit.
            int admin_msg = atomic_exchange_int(&ptr->server->maint_request, MAINTENANCE_NO_CHANGE);
            if (admin_msg == MAINTENANCE_ON)
            {
                // TODO: Change to writing MONITORED_SERVER->pending status instead once cleanup done.
                server_set_status_nolock(ptr->server, SERVER_MAINT);
            }
            else if (admin_msg == MAINTENANCE_OFF)
            {
                server_clear_status_nolock(ptr->server, SERVER_MAINT);
            }
            ptr = ptr->next;
        }
    }
}

void mon_process_state_changes(MXS_MONITOR *monitor, const char *script, uint64_t events)
{
    bool master_down = false;
    bool master_up = false;

    for (MXS_MONITORED_SERVER *ptr = monitor->monitored_servers; ptr; ptr = ptr->next)
    {
        if (mon_status_changed(ptr))
        {
            /**
             * The last executed event will be needed if a passive MaxScale is
             * promoted to an active one and the last event that occurred on
             * a server was a master_down event.
             *
             * In this case, a failover script should be called if no master_up
             * or new_master events are triggered within a pre-defined time limit.
             */
            mxs_monitor_event_t event = mon_get_event_type(ptr);
            ptr->server->last_event = event;
            ptr->server->triggered_at = mxs_clock();
            ptr->server->active_event = !config_get_global_options()->passive;
            ptr->new_event = true;
            mon_log_state_change(ptr);

            if (event == MASTER_DOWN_EVENT)
            {
                master_down = true;
            }
            else if (event == MASTER_UP_EVENT || event == NEW_MASTER_EVENT)
            {
                master_up = true;
            }

            if (script && *script && (events & event))
            {
                monitor_launch_script(monitor, ptr, script, monitor->script_timeout);
            }
        }
    }

    if (master_down && master_up)
    {
        MXS_NOTICE("Master switch detected: lost a master and gained a new one");
    }
}

static const char* monitor_state_to_string(int state)
{
    switch (state)
    {
    case MONITOR_STATE_RUNNING:
        return "Running";

    case MONITOR_STATE_STOPPING:
        return "Stopping";

    case MONITOR_STATE_STOPPED:
        return "Stopped";

    case MONITOR_STATE_ALLOC:
        return "Allocated";

    default:
        ss_dassert(false);
        return "Unknown";
    }
}

json_t* monitor_parameters_to_json(const MXS_MONITOR* monitor)
{
    json_t* rval = json_object();

    json_object_set_new(rval, CN_USER, json_string(monitor->user));
    json_object_set_new(rval, CN_PASSWORD, json_string(monitor->password));
    json_object_set_new(rval, CN_MONITOR_INTERVAL, json_integer(monitor->interval));
    json_object_set_new(rval, CN_BACKEND_CONNECT_TIMEOUT, json_integer(monitor->connect_timeout));
    json_object_set_new(rval, CN_BACKEND_READ_TIMEOUT, json_integer(monitor->read_timeout));
    json_object_set_new(rval, CN_BACKEND_WRITE_TIMEOUT, json_integer(monitor->write_timeout));
    json_object_set_new(rval, CN_BACKEND_CONNECT_ATTEMPTS, json_integer(monitor->connect_attempts));
    json_object_set_new(rval, CN_JOURNAL_MAX_AGE, json_integer(monitor->journal_max_age));
    json_object_set_new(rval, CN_SCRIPT_TIMEOUT, json_integer(monitor->script_timeout));

    /** Add custom module parameters */
    const MXS_MODULE* mod = get_module(monitor->module_name, MODULE_MONITOR);
    config_add_module_params_json(mod, monitor->parameters, config_monitor_params, rval);

    /** Don't show the default value for events if no script is defined */
    if (json_object_get(rval, CN_SCRIPT) == NULL)
    {
        json_object_del(rval, CN_EVENTS);
    }

    return rval;
}

json_t* monitor_json_data(const MXS_MONITOR* monitor, const char* host)
{
    json_t* rval = json_object();

    spinlock_acquire(&monitor->lock);

    json_object_set_new(rval, CN_ID, json_string(monitor->name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_MONITORS));

    json_t* attr = json_object();

    json_object_set_new(attr, CN_MODULE, json_string(monitor->module_name));
    json_object_set_new(attr, CN_STATE, json_string(monitor_state_to_string(monitor->state)));

    /** Monitor parameters */
    json_object_set_new(attr, CN_PARAMETERS, monitor_parameters_to_json(monitor));

    if (monitor->instance && monitor->api->diagnostics_json)
    {
        json_t* diag = monitor->api->diagnostics_json(monitor->instance);

        if (diag)
        {
            json_object_set_new(attr, CN_MONITOR_DIAGNOSTICS, diag);
        }
    }

    json_t* rel = json_object();


    if (monitor->monitored_servers)
    {
        json_t* mon_rel = mxs_json_relationship(host, MXS_JSON_API_SERVERS);

        for (MXS_MONITORED_SERVER *db = monitor->monitored_servers; db; db = db->next)
        {
            mxs_json_add_relation(mon_rel, db->server->name, CN_SERVERS);
        }

        json_object_set_new(rel, CN_SERVERS, mon_rel);
    }

    spinlock_release(&monitor->lock);

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    json_object_set_new(rval, CN_ATTRIBUTES, attr);
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_MONITORS, monitor->name));

    return rval;
}

json_t* monitor_to_json(const MXS_MONITOR* monitor, const char* host)
{
    string self = MXS_JSON_API_MONITORS;
    self += monitor->name;
    return mxs_json_resource(host, self.c_str(), monitor_json_data(monitor, host));
}

json_t* monitor_list_to_json(const char* host)
{
    json_t* rval = json_array();

    spinlock_acquire(&monLock);

    for (MXS_MONITOR* mon = allMonitors; mon; mon = mon->next)
    {
        if (mon->active)
        {
            json_t *json = monitor_json_data(mon, host);

            if (json)
            {
                json_array_append_new(rval, json);
            }
        }
    }

    spinlock_release(&monLock);

    return mxs_json_resource(host, MXS_JSON_API_MONITORS, rval);
}

json_t* monitor_relations_to_server(const SERVER* server, const char* host)
{
    std::vector<std::string> names;
    spinlock_acquire(&monLock);

    for (MXS_MONITOR* mon = allMonitors; mon; mon = mon->next)
    {
        spinlock_acquire(&mon->lock);

        if (mon->active)
        {
            for (MXS_MONITORED_SERVER* db = mon->monitored_servers; db; db = db->next)
            {
                if (db->server == server)
                {
                    names.push_back(mon->name);
                    break;
                }
            }
        }

        spinlock_release(&mon->lock);
    }

    spinlock_release(&monLock);

    json_t* rel = NULL;

    if (!names.empty())
    {
        rel = mxs_json_relationship(host, MXS_JSON_API_MONITORS);

        for (std::vector<std::string>::iterator it = names.begin();
             it != names.end(); it++)
        {
            mxs_json_add_relation(rel, it->c_str(), CN_MONITORS);
        }
    }

    return rel;
}

static const char journal_name[]  = "monitor.dat";
static const char journal_template[] = "%s/%s/%s";

/**
 * @brief Remove .tmp suffix and rename file
 *
 * @param src File to rename
 * @return True if file was successfully renamed
 */
static bool rename_tmp_file(MXS_MONITOR* monitor, const char *src)
{
    bool rval = true;
    char dest[PATH_MAX + 1];
    snprintf(dest, sizeof(dest), journal_template, get_datadir(), monitor->name, journal_name);

    if (rename(src, dest) == -1)
    {
        rval = false;
        MXS_ERROR("Failed to rename journal file '%s' to '%s': %d, %s",
                  src, dest, errno, mxs_strerror(errno));
    }

    return rval;
}

/**
 * @brief Open temporary file
 *
 * @param monitor Monitor
 * @param path Output where the path is stored
 * @return Opened file or NULL on error
 */
static FILE* open_tmp_file(MXS_MONITOR *monitor, char *path)
{
    int nbytes = snprintf(path, PATH_MAX, journal_template, get_datadir(), monitor->name, "");
    int max_bytes = PATH_MAX - (int)sizeof(journal_name);
    FILE *rval = NULL;

    if (nbytes < max_bytes && mxs_mkdir_all(path, 0744))
    {
        strcat(path, journal_name);
        strcat(path, "XXXXXX");
        int fd = mkstemp(path);

        if (fd == -1)
        {
            MXS_ERROR("Failed to open file '%s': %d, %s", path, errno, mxs_strerror(errno));
        }
        else
        {
            rval = fdopen(fd, "w");
        }
    }
    else
    {
        MXS_ERROR("Path is too long: %d characters exceeds the maximum path "
                  "length of %d bytes", nbytes, max_bytes);
    }

    return rval;
}

/**
 * @brief Store server data to in-memory buffer
 *
 * @param monitor Monitor
 * @param data Pointer to in-memory buffer used for storage, should be at least
 *             PATH_MAX bytes long
 * @param size Size of @c data
 */
static void store_data(MXS_MONITOR *monitor, MXS_MONITORED_SERVER *master, uint8_t *data, uint32_t size)
{
    uint8_t* ptr = data;

    /** Store the data length */
    ss_dassert(sizeof(size) == MMB_LEN_BYTES);
    ptr = mxs_set_byte4(ptr, size);

    /** Then the schema version */
    *ptr++ = MMB_SCHEMA_VERSION;

    /** Store the states of all servers */
    for (MXS_MONITORED_SERVER* db = monitor->monitored_servers; db; db = db->next)
    {
        *ptr++ = (char)SVT_SERVER; // Value type
        memcpy(ptr, db->server->name, strlen(db->server->name)); // Name of the server
        ptr += strlen(db->server->name);
        *ptr++ = '\0'; // Null-terminate the string

        auto status = db->server->status;
        static_assert(sizeof(status) == MMB_LEN_SERVER_STATUS, "Status size should be MMB_LEN_SERVER_STATUS bytes");
        ptr = maxscale::set_byteN(ptr, status, MMB_LEN_SERVER_STATUS);
    }

    /** Store the current root master if we have one */
    if (master)
    {
        *ptr++ = (char)SVT_MASTER;
        memcpy(ptr, master->server->name, strlen(master->server->name));
        ptr += strlen(master->server->name);
        *ptr++ = '\0'; // Null-terminate the string
    }

    /** Calculate the CRC32 for the complete payload minus the CRC32 bytes */
    uint32_t crc = crc32(0L, NULL, 0);
    crc = crc32(crc, (uint8_t*)data + MMB_LEN_BYTES, size - MMB_LEN_CRC32);
    ss_dassert(sizeof(crc) == MMB_LEN_CRC32);

    ptr = mxs_set_byte4(ptr, crc);
    ss_dassert(ptr - data == size + MMB_LEN_BYTES);
}

static int get_data_file_path(MXS_MONITOR *monitor, char *path)
{
    int rv = snprintf(path, PATH_MAX, journal_template, get_datadir(), monitor->name, journal_name);
    return rv;
}

/**
 * @brief Open stored journal file
 *
 * @param monitor Monitor to reload
 * @param path Output where path is stored
 * @return Opened file or NULL on error
 */
static FILE* open_data_file(MXS_MONITOR *monitor, char *path)
{
    FILE *rval = NULL;
    int nbytes = get_data_file_path(monitor, path);

    if (nbytes < PATH_MAX)
    {
        if ((rval = fopen(path, "rb")) == NULL && errno != ENOENT)
        {
            MXS_ERROR("Failed to open journal file: %d, %s", errno, mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Path is too long: %d characters exceeds the maximum path "
                  "length of %d bytes", nbytes, PATH_MAX);
    }

    return rval;
}

/**
 * Check that memory area contains a null terminator
 */
static bool has_null_terminator(const char *data, const char *end)
{
    while (data < end)
    {
        if (*data == '\0')
        {
            return true;
        }
        data++;
    }

    return false;
}

/**
 * Process a generic server
 */
static const char* process_server(MXS_MONITOR *monitor, const char *data, const char *end)
{
    for (MXS_MONITORED_SERVER* db = monitor->monitored_servers; db; db = db->next)
    {
        if (strcmp(db->server->name, data) == 0)
        {
            const unsigned char *sptr = (unsigned char*)strchr(data, '\0');
            ss_dassert(sptr);
            sptr++;

            uint64_t status = maxscale::get_byteN(sptr, MMB_LEN_SERVER_STATUS);
            db->mon_prev_status = status;
            server_set_status_nolock(db->server, status);
            monitor_set_pending_status(db, status);
            break;
        }
    }

    data += strlen(data) + 1 + MMB_LEN_SERVER_STATUS;

    return data;
}

/**
 * Process a master
 */
static const char* process_master(MXS_MONITOR *monitor, MXS_MONITORED_SERVER **master, const char *data,
                                  const char *end)
{
    if (master)
    {
        for (MXS_MONITORED_SERVER* db = monitor->monitored_servers; db; db = db->next)
        {
            if (strcmp(db->server->name, data) == 0)
            {
                *master = db;
                break;
            }
        }
    }

    data += strlen(data) + 1;

    return data;
}

/**
 * Check that the calculated CRC32 matches the one stored on disk
 */
static bool check_crc32(const uint8_t *data, uint32_t size, const uint8_t *crc_ptr)
{
    uint32_t crc = mxs_get_byte4(crc_ptr);
    uint32_t calculated_crc = crc32(0L, NULL, 0);
    calculated_crc = crc32(calculated_crc, data, size);
    return calculated_crc == crc;
}

/**
 * Process the stored journal data
 */
static bool process_data_file(MXS_MONITOR *monitor, MXS_MONITORED_SERVER **master, const char *data,
                              const char *crc_ptr)
{
    const char *ptr = data;
    ss_debug(const char *prevptr = ptr);

    while (ptr < crc_ptr)
    {
        /** All values contain a null terminated string */
        if (!has_null_terminator(ptr, crc_ptr))
        {
            MXS_ERROR("Possible corrupted journal file (no null terminator found). Ignoring.");
            return false;
        }

        stored_value_type type = (stored_value_type)ptr[0];
        ptr += MMB_LEN_VALUE_TYPE;

        switch (type)
        {
        case SVT_SERVER:
            ptr = process_server(monitor, ptr, crc_ptr);
            break;

        case SVT_MASTER:
            ptr = process_master(monitor, master, ptr, crc_ptr);
            break;

        default:
            MXS_ERROR("Possible corrupted journal file (unknown stored value). Ignoring.");
            return false;
        }
        ss_dassert(prevptr != ptr);
        ss_debug(prevptr = ptr);
    }

    ss_dassert(ptr == crc_ptr);
    return true;
}

void store_server_journal(MXS_MONITOR *monitor, MXS_MONITORED_SERVER *master)
{
    /** Calculate how much memory we need to allocate */
    uint32_t size = MMB_LEN_SCHEMA_VERSION + MMB_LEN_CRC32;

    for (MXS_MONITORED_SERVER* db = monitor->monitored_servers; db; db = db->next)
    {
        /** Each server is stored as a type byte and a null-terminated string
         * followed by eight byte server status. */
        size += MMB_LEN_VALUE_TYPE + strlen(db->server->name) + 1 + MMB_LEN_SERVER_STATUS;
    }

    if (master)
    {
        /** The master server name is stored as a null terminated string */
        size += MMB_LEN_VALUE_TYPE + strlen(master->server->name) + 1;
    }

    /** 4 bytes for file length, 1 byte for schema version and 4 bytes for CRC32 */
    uint32_t buffer_size = size + MMB_LEN_BYTES;
    uint8_t *data = (uint8_t*)MXS_MALLOC(buffer_size);
    char path[PATH_MAX + 1];

    if (data)
    {
        /** Store the data in memory first and compare the current hash to
         * the hash of the last stored journal. This isn't a fool-proof
         * method of detecting changes but any failures are mainly of
         * theoretical nature. */
        store_data(monitor, master, data, size);
        uint8_t hash[SHA_DIGEST_LENGTH];
        SHA1(data, size, hash);

        if (memcmp(monitor->journal_hash, hash, sizeof(hash)) != 0)
        {
            FILE *file = open_tmp_file(monitor, path);

            if (file)
            {
                /** Write the data to a temp file and rename it to the final name */
                if (fwrite(data, 1, buffer_size, file) == buffer_size && fflush(file) == 0)
                {
                    if (!rename_tmp_file(monitor, path))
                    {
                        unlink(path);
                    }
                    else
                    {
                        memcpy(monitor->journal_hash, hash, sizeof(hash));
                    }
                }
                else
                {
                    MXS_ERROR("Failed to write journal data to disk: %d, %s",
                              errno, mxs_strerror(errno));
                }
                fclose(file);
            }
        }
    }
    MXS_FREE(data);
}

void load_server_journal(MXS_MONITOR *monitor, MXS_MONITORED_SERVER **master)
{
    char path[PATH_MAX];
    FILE *file = open_data_file(monitor, path);

    if (file)
    {
        uint32_t size = 0;
        size_t bytes = fread(&size, 1, MMB_LEN_BYTES, file);
        ss_dassert(sizeof(size) == MMB_LEN_BYTES);

        if (bytes == MMB_LEN_BYTES)
        {
            /** Payload contents:
             *
             * - One byte of schema version
             * - `size - 5` bytes of data
             * - Trailing 4 bytes of CRC32
             */
            char *data = (char*)MXS_MALLOC(size);

            if (data && (bytes = fread(data, 1, size, file)) == size)
            {
                if (*data == MMB_SCHEMA_VERSION)
                {
                    if (check_crc32((uint8_t*)data, size - MMB_LEN_CRC32,
                                    (uint8_t*)data + size - MMB_LEN_CRC32))
                    {
                        if (process_data_file(monitor, master,
                                              data + MMB_LEN_SCHEMA_VERSION,
                                              data + size - MMB_LEN_CRC32))
                        {
                            MXS_NOTICE("Loaded server states from journal file: %s", path);
                        }
                    }
                    else
                    {
                        MXS_ERROR("CRC32 mismatch in journal file. Ignoring.");
                    }
                }
                else
                {
                    MXS_ERROR("Unknown journal schema version: %d", (int)*data);
                }
            }
            else if (data)
            {
                if (ferror(file))
                {
                    MXS_ERROR("Failed to read journal file: %d, %s", errno, mxs_strerror(errno));
                }
                else
                {
                    MXS_ERROR("Failed to read journal file: Expected %u bytes, "
                              "read %lu bytes.", size, bytes);
                }
            }
            MXS_FREE(data);
        }
        else
        {
            if (ferror(file))
            {
                MXS_ERROR("Failed to read journal file length: %d, %s",
                          errno, mxs_strerror(errno));
            }
            else
            {
                MXS_ERROR("Failed to read journal file length: Expected %d bytes, "
                          "read %lu bytes.", MMB_LEN_BYTES, bytes);
            }
        }

        fclose(file);
    }
}

static void remove_server_journal(MXS_MONITOR *monitor)
{
    char path[PATH_MAX];

    if (get_data_file_path(monitor, path) < PATH_MAX)
    {
        unlink(path);
    }
    else
    {
        MXS_ERROR("Path to monitor journal directory is too long.");
    }
}

static bool journal_is_stale(MXS_MONITOR *monitor, time_t max_age)
{
    bool is_stale = true;
    char path[PATH_MAX];

    if (get_data_file_path(monitor, path) < PATH_MAX)
    {
        struct stat st;

        if (stat(path, &st) == 0)
        {
            time_t tdiff = time(NULL) - st.st_mtim.tv_sec;

            if (tdiff >= max_age)
            {
                MXS_WARNING("Journal file was created %ld seconds ago. Maximum journal "
                            "age is %ld seconds.", tdiff, max_age);
            }
            else
            {
                is_stale = false;
            }
        }
        else if (errno != ENOENT)
        {
            MXS_ERROR("Failed to inspect journal file: %d, %s", errno, mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Path to monitor journal directory is too long.");
    }

    return is_stale;
}

MXS_MONITORED_SERVER* mon_get_monitored_server(const MXS_MONITOR* mon, SERVER* search_server)
{
    ss_dassert(mon && search_server);
    for (MXS_MONITORED_SERVER* iter = mon->monitored_servers; iter != NULL; iter = iter->next)
    {
        if (iter->server == search_server)
        {
            return iter;
        }
    }
    return NULL;
}

int mon_config_get_servers(const MXS_CONFIG_PARAMETER* params, const char* key, const MXS_MONITOR* mon,
                           MXS_MONITORED_SERVER*** monitored_servers_out)
{
    ss_dassert(monitored_servers_out != NULL && *monitored_servers_out == NULL);
    SERVER** servers = NULL;
    int servers_size = config_get_server_list(params, key, &servers);
    int found = 0;
    // All servers in the array must be monitored by the given monitor.
    if (servers_size > 0)
    {
        MXS_MONITORED_SERVER** monitored_array =
            (MXS_MONITORED_SERVER**)MXS_CALLOC(servers_size, sizeof(MXS_MONITORED_SERVER*));
        for (int i = 0; i < servers_size; i++)
        {
            MXS_MONITORED_SERVER* mon_serv = mon_get_monitored_server(mon, servers[i]);
            if (mon_serv != NULL)
            {
                monitored_array[found++] = mon_serv;
            }
            else
            {
                MXS_WARNING("Server '%s' is not monitored by monitor '%s'.",
                            servers[i]->name, mon->name);
            }
        }
        MXS_FREE(servers);

        ss_dassert(found <= servers_size);
        if (found == 0)
        {
            MXS_FREE(monitored_array);
            monitored_array = NULL;
        }
        else if (found < servers_size)
        {
            monitored_array = (MXS_MONITORED_SERVER**)MXS_REALLOC(monitored_array, found);
        }
        *monitored_servers_out = monitored_array;
    }
    return found;
}

bool monitor_set_disk_space_threshold(MXS_MONITOR *monitor, const char *disk_space_threshold)
{
    bool rv = false;

    MxsDiskSpaceThreshold dst;

    rv = config_parse_disk_space_threshold(&dst, disk_space_threshold);

    if (rv)
    {
        if (!monitor->disk_space_threshold)
        {
            monitor->disk_space_threshold = new (std::nothrow) MxsDiskSpaceThreshold;
        }

        if (monitor->disk_space_threshold)
        {
            monitor->disk_space_threshold->swap(dst);
        }
        else
        {
            rv = false;
        }
    }

    return rv;
}

namespace maxscale
{

MonitorInstance::MonitorInstance(MXS_MONITOR* pMonitor)
    : m_monitor(pMonitor)
    , m_master(NULL)
    , m_state(MXS_MONITOR_STOPPED)
    , m_thread(0)
    , m_shutdown(0)
    , m_checked(false)
    , m_events(0)
{
}

MonitorInstance::~MonitorInstance()
{
    ss_dassert(!m_thread);
}

int32_t MonitorInstance::state() const
{
    return atomic_load_int32(&m_state);
}

void MonitorInstance::stop()
{
    // This is always called in single-thread context.
    ss_dassert(m_thread);
    ss_dassert(m_state == MXS_MONITOR_RUNNING);

    if (state() == MXS_MONITOR_RUNNING)
    {
        atomic_store_int32(&m_state, MXS_MONITOR_STOPPING);
        atomic_store_int32(&m_shutdown, 1);
        thread_wait(m_thread);
        atomic_store_int32(&m_state, MXS_MONITOR_STOPPED);

        m_thread = 0;
        m_shutdown = 0;
    }
    else
    {
        MXS_WARNING("An attempt was made to stop a monitor that is not running.");
    }
}

void MonitorInstance::diagnostics(DCB* pDcb) const
{
}

json_t* MonitorInstance::diagnostics_json() const
{
    json_t* pJson = json_object();

    if (!m_script.empty())
    {
        json_object_set_new(pJson, CN_SCRIPT, json_string(m_script.c_str()));

        string events;

        const MXS_ENUM_VALUE* pValue = mxs_monitor_event_enum_values;

        while (pValue->name)
        {
            if (pValue->enum_value & m_events)
            {
                if (!events.empty())
                {
                    events += ",";
                }

                events += pValue->enum_value;
            }

            ++pValue;
        }

        json_object_set_new(pJson, CN_EVENTS, json_string(events.c_str()));
    }

    return pJson;
}

bool MonitorInstance::start(const MXS_CONFIG_PARAMETER* pParams)
{
    bool started = false;

    ss_dassert(!m_shutdown);
    ss_dassert(!m_thread);
    ss_dassert(m_state == MXS_MONITOR_STOPPED);

    if (state() == MXS_MONITOR_STOPPED)
    {
        if (!m_checked)
        {
            if (!has_sufficient_permissions())
            {
                MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
            }
            else
            {
                m_checked = true;
            }
        }

        if (m_checked)
        {
            m_script = config_get_string(pParams, CN_SCRIPT);
            m_events = config_get_enum(pParams, CN_EVENTS, mxs_monitor_event_enum_values);
            m_master = NULL;

            if (configure(pParams))
            {
                if (thread_start(&m_thread, &maxscale::MonitorInstance::main, this, 0) == NULL)
                {
                    MXS_ERROR("Failed to start monitor thread for monitor '%s'.", m_monitor->name);
                }
                else
                {
                    // Ok, so the thread started. Let's wait until we can be certain the
                    // state has been updated.
                    m_semaphore.wait();

                    started = (atomic_load_int32(&m_state) == MXS_MONITOR_RUNNING);

                    if (!started)
                    {
                        // Ok, so the initialization failed and the thread will exit.
                        // We need to wait on it so that the thread resources will not leak.
                        thread_wait(m_thread);
                        m_thread = 0;
                    }
                }
            }
        }
    }
    else
    {
        MXS_WARNING("An attempt was made to start a monitor that is already running.");
        // Likely to cause the least amount of damage if we pretend the monitor
        // was started.
        started = true;
    }

    return started;
}

bool MonitorInstance::configure(const MXS_CONFIG_PARAMETER* pParams)
{
    return true;
}

bool MonitorInstance::has_sufficient_permissions() const
{
    return true;
}

void MonitorInstance::flush_server_status()
{
    for (MXS_MONITORED_SERVER *pMs = m_monitor->monitored_servers; pMs; pMs = pMs->next)
    {
        if (!SERVER_IN_MAINT(pMs->server))
        {
            pMs->server->status = pMs->pending_status;
        }
    }
}

void MonitorInstance::tick()
{
    for (MXS_MONITORED_SERVER *pMs = m_monitor->monitored_servers; pMs; pMs = pMs->next)
    {
        if (!SERVER_IN_MAINT(pMs->server))
        {
            pMs->mon_prev_status = pMs->server->status;
            pMs->pending_status = pMs->server->status;

            mxs_connect_result_t rval = mon_ping_or_connect_to_db(m_monitor, pMs);

            if (mon_connection_is_ok(rval))
            {
                monitor_clear_pending_status(pMs, SERVER_AUTH_ERROR);
                monitor_set_pending_status(pMs, SERVER_RUNNING);

                update_server_status(pMs);
            }
            else
            {
                monitor_clear_pending_status(pMs, SERVER_RUNNING);

                if (mysql_errno(pMs->con) == ER_ACCESS_DENIED_ERROR)
                {
                    monitor_set_pending_status(pMs, SERVER_AUTH_ERROR);
                }
                else
                {
                    monitor_clear_pending_status(pMs, SERVER_AUTH_ERROR);
                }

                if (mon_status_changed(pMs) && mon_print_fail_status(pMs))
                {
                    mon_log_connect_error(pMs, rval);
                }
            }

#if defined(SS_DEBUG)
            if (mon_status_changed(pMs) || mon_print_fail_status(pMs))
            {
                // The current status is still in pMs->pending_status.
                SERVER server = {};
                server.status = pMs->pending_status;
                MXS_DEBUG("Backend server [%s]:%d state : %s",
                          pMs->server->address,
                          pMs->server->port,
                          STRSRVSTATUS(&server));
            }
#endif

            if (SERVER_IS_DOWN(pMs->server))
            {
                pMs->mon_err_count += 1;
            }
            else
            {
                pMs->mon_err_count = 0;
            }
        }
    }

    flush_server_status();
}

void MonitorInstance::main()
{
    load_server_journal(m_monitor, &m_master);

    while (!m_shutdown)
    {
        monitor_check_maintenance_requests(m_monitor);

        tick();

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(m_monitor, m_script.empty() ? NULL : m_script.c_str(), m_events);

        mon_hangup_failed_servers(m_monitor);
        store_server_journal(m_monitor, m_master);

        /** Sleep until the next monitoring interval */
        unsigned int ms = 0;
        while (ms < m_monitor->interval && !m_shutdown)
        {
            if (atomic_load_int(&m_monitor->check_maintenance_flag) != MAINTENANCE_FLAG_NOCHECK)
            {
                // Admin has changed something, skip sleep
                break;
            }
            thread_millisleep(MXS_MON_BASE_INTERVAL_MS);
            ms += MXS_MON_BASE_INTERVAL_MS;
        }
    }
}

//static
void MonitorInstance::main(void* pArg)
{
    MonitorInstance* pThis = static_cast<MonitorInstance*>(pArg);

    if (mysql_thread_init() == 0)
    {
        atomic_store_int32(&pThis->m_state, MXS_MONITOR_RUNNING);
        pThis->m_semaphore.post();

        pThis->main();

        mysql_thread_end();
    }
    else
    {
        MXS_ERROR("mysql_thread_init() failed for %s. The monitor cannot start.",
                  pThis->m_monitor->name);
        pThis->m_semaphore.post();
    }
}

}
