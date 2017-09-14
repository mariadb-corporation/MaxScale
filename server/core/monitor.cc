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
#include <maxscale/monitor.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <set>
#include <zlib.h>
#include <sys/stat.h>

#include <maxscale/alloc.h>
#include <mysqld_error.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>
#include <maxscale/log_manager.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/pcre2.h>
#include <maxscale/secrets.h>
#include <maxscale/spinlock.h>
#include <maxscale/json_api.h>

#include "maxscale/config.h"
#include "maxscale/externcmd.h"
#include "maxscale/monitor.h"
#include "maxscale/modules.h"
#include "maxscale/json_api.h"

/** Schema version, journals must have a matching version */
#define MMB_SCHEMA_VERSION     1

/** Constants for byte lengths of the values */
#define MMB_LEN_BYTES          4
#define MMB_LEN_SCHEMA_VERSION 1
#define MMB_LEN_CRC32          4
#define MMB_LEN_VALUE_TYPE     1
#define MMB_LEN_SERVER_STATUS  4

/** Type of the stored value */
enum stored_value_type
{
    SVT_SERVER = 1, // Generic server state information
    SVT_MASTER = 2, // The master server name
};

using std::string;
using std::set;

const char CN_BACKEND_CONNECT_ATTEMPTS[] = "backend_connect_attempts";
const char CN_BACKEND_READ_TIMEOUT[]     = "backend_read_timeout";
const char CN_BACKEND_WRITE_TIMEOUT[]    = "backend_write_timeout";
const char CN_BACKEND_CONNECT_TIMEOUT[]  = "backend_connect_timeout";
const char CN_MONITOR_INTERVAL[]         = "monitor_interval";
const char CN_JOURNAL_MAX_AGE[]          = "journal_max_age";
const char CN_SCRIPT_TIMEOUT[]           = "script_timeout";
const char CN_SCRIPT[]                   = "script";
const char CN_EVENTS[]                   = "events";

static MXS_MONITOR  *allMonitors = NULL;
static SPINLOCK monLock = SPINLOCK_INIT;

static void monitor_server_free_all(MXS_MONITOR_SERVERS *servers);
static void remove_server_journal(MXS_MONITOR *monitor);
static bool journal_is_stale(MXS_MONITOR *monitor, time_t max_age);

/** Server type specific bits */
static unsigned int server_type_bits = SERVER_MASTER | SERVER_SLAVE |
                                       SERVER_JOINED | SERVER_NDB;

/** All server bits */
static unsigned int all_server_bits = SERVER_RUNNING |  SERVER_MAINT |
                                      SERVER_MASTER | SERVER_SLAVE |
                                      SERVER_JOINED | SERVER_NDB;

/**
 * Allocate a new monitor, load the associated module for the monitor
 * and start execution on the monitor.
 *
 * @param name          The name of the monitor module to load
 * @param module        The module to load
 * @return      The newly created monitor
 */
MXS_MONITOR* monitor_alloc(const char *name, const char *module)
{
    char* my_name = MXS_STRDUP(name);
    char *my_module = MXS_STRDUP(module);
    MXS_MONITOR *mon = (MXS_MONITOR *)MXS_MALLOC(sizeof(MXS_MONITOR));

    if (!my_name || !mon || !my_module)
    {
        MXS_FREE(my_name);
        MXS_FREE(mon);
        MXS_FREE(my_module);
        return NULL;
    }

    if ((mon->module = (MXS_MONITOR_OBJECT*)load_module(module, MODULE_MONITOR)) == NULL)
    {
        MXS_ERROR("Unable to load monitor module '%s'.", my_name);
        MXS_FREE(my_name);
        MXS_FREE(mon);
        return NULL;
    }
    mon->active = true;
    mon->state = MONITOR_STATE_ALLOC;
    mon->name = my_name;
    mon->module_name = my_module;
    mon->handle = NULL;
    mon->databases = NULL;
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
    mon->server_pending_changes = false;
    spinlock_init(&mon->lock);
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
monitor_free(MXS_MONITOR *mon)
{
    MXS_MONITOR *ptr;

    mon->module->stopMonitor(mon);
    mon->state = MONITOR_STATE_FREED;
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
    config_parameter_free(mon->parameters);
    monitor_server_free_all(mon->databases);
    MXS_FREE(mon->name);
    MXS_FREE(mon->module_name);
    MXS_FREE(mon);
}


/**
 * Start an individual monitor that has previously been stopped.
 *
 * @param monitor The Monitor that should be started
 */
void
monitorStart(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    if (monitor)
    {
        spinlock_acquire(&monitor->lock);

        if (journal_is_stale(monitor, monitor->journal_max_age))
        {
            MXS_WARNING("Removing stale journal file for monitor '%s'.", monitor->name);
            remove_server_journal(monitor);
        }

        if ((monitor->handle = (*monitor->module->startMonitor)(monitor, params)))
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
void monitorStartAll()
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (ptr->active)
        {
            monitorStart(ptr, ptr->parameters);
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
monitorStop(MXS_MONITOR *monitor)
{
    if (monitor)
    {
        spinlock_acquire(&monitor->lock);

        /** Only stop the monitor if it is running */
        if (monitor->state == MONITOR_STATE_RUNNING)
        {
            monitor->state = MONITOR_STATE_STOPPING;
            monitor->module->stopMonitor(monitor);
            monitor->state = MONITOR_STATE_STOPPED;

            MXS_MONITOR_SERVERS* db = monitor->databases;
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

void monitorDestroy(MXS_MONITOR* monitor)
{
    spinlock_acquire(&monLock);
    monitor->active = false;
    spinlock_release(&monLock);
}

/**
 * Shutdown all running monitors
 */
void
monitorStopAll()
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (ptr->active)
        {
            monitorStop(ptr);
        }
        ptr = ptr->next;
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
bool monitorAddServer(MXS_MONITOR *mon, SERVER *server)
{
    bool rval = false;

    if (monitor_server_in_use(server))
    {
        MXS_ERROR("Server '%s' is already monitored.", server->unique_name);
    }
    else
    {
        rval = true;
        MXS_MONITOR_SERVERS *db = (MXS_MONITOR_SERVERS *)MXS_MALLOC(sizeof(MXS_MONITOR_SERVERS));
        MXS_ABORT_IF_NULL(db);

        db->server = server;
        db->con = NULL;
        db->next = NULL;
        db->mon_err_count = 0;
        db->log_version_err = true;
        /** Server status is uninitialized */
        db->mon_prev_status = -1;
        /* pending status is updated by get_replication_tree */
        db->pending_status = 0;

        monitor_state_t old_state = mon->state;

        if (old_state == MONITOR_STATE_RUNNING)
        {
            monitorStop(mon);
        }

        spinlock_acquire(&mon->lock);

        if (mon->databases == NULL)
        {
            mon->databases = db;
        }
        else
        {
            MXS_MONITOR_SERVERS *ptr = mon->databases;
            while (ptr->next != NULL)
            {
                ptr = ptr->next;
            }
            ptr->next = db;
        }
        spinlock_release(&mon->lock);

        if (old_state == MONITOR_STATE_RUNNING)
        {
            monitorStart(mon, mon->parameters);
        }
    }

    return rval;
}

static void monitor_server_free(MXS_MONITOR_SERVERS *tofree)
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
static void monitor_server_free_all(MXS_MONITOR_SERVERS *servers)
{
    while (servers)
    {
        MXS_MONITOR_SERVERS *tofree = servers;
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
void monitorRemoveServer(MXS_MONITOR *mon, SERVER *server)
{
    monitor_state_t old_state = mon->state;

    if (old_state == MONITOR_STATE_RUNNING)
    {
        monitorStop(mon);
    }

    spinlock_acquire(&mon->lock);

    MXS_MONITOR_SERVERS *ptr = mon->databases;

    if (ptr && ptr->server == server)
    {
        mon->databases = mon->databases->next;
    }
    else
    {
        MXS_MONITOR_SERVERS *prev = ptr;

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
        monitorStart(mon, mon->parameters);
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
monitorAddUser(MXS_MONITOR *mon, const char *user, const char *passwd)
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
monitorShowAll(DCB *dcb)
{
    MXS_MONITOR *ptr;

    spinlock_acquire(&monLock);
    ptr = allMonitors;
    while (ptr)
    {
        if (ptr->active)
        {
            monitorShow(dcb, ptr);
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
monitorShow(DCB *dcb, MXS_MONITOR *monitor)
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

    dcb_printf(dcb, "Monitor:           %p\n", monitor);
    dcb_printf(dcb, "Name:              %s\n", monitor->name);
    dcb_printf(dcb, "State:             %s\n", state);
    dcb_printf(dcb, "Sampling interval: %lu milliseconds\n", monitor->interval);
    dcb_printf(dcb, "Connect Timeout:   %i seconds\n", monitor->connect_timeout);
    dcb_printf(dcb, "Read Timeout:      %i seconds\n", monitor->read_timeout);
    dcb_printf(dcb, "Write Timeout:     %i seconds\n", monitor->write_timeout);
    dcb_printf(dcb, "Connect attempts:  %i \n", monitor->connect_attempts);
    dcb_printf(dcb, "Monitored servers: ");

    const char *sep = "";

    for (MXS_MONITOR_SERVERS *db = monitor->databases; db; db = db->next)
    {
        dcb_printf(dcb, "%s[%s]:%d", sep, db->server->name, db->server->port);
        sep = ", ";
    }

    dcb_printf(dcb, "\n");

    if (monitor->handle)
    {
        if (monitor->module->diagnostics)
        {
            monitor->module->diagnostics(dcb, monitor);
        }
        else
        {
            dcb_printf(dcb, "\t(no diagnostics)\n");
        }
    }
    else
    {
        dcb_printf(dcb, "\tMonitor failed\n");
    }
    dcb_printf(dcb, "\n");
}

/**
 * List all the monitors
 *
 * @param dcb   DCB for printing output
 */
void
monitorList(DCB *dcb)
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
monitorSetInterval(MXS_MONITOR *mon, unsigned long interval)
{
    mon->interval = interval;
}

/**
 * Set the maximum age of the monitor journal
 *
 * @param mon           The monitor instance
 * @param interval      The journal age in seconds
 */
void monitorSetJournalMaxAge(MXS_MONITOR *mon, time_t value)
{
    mon->journal_max_age = value;
}

/**
 * Set the maximum age of the monitor journal
 *
 * @param mon           The monitor instance
 * @param interval      The journal age in seconds
 */
void monitorSetScriptTimeout(MXS_MONITOR *mon, uint32_t value)
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
bool
monitorSetNetworkTimeout(MXS_MONITOR *mon, int type, int value)
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
            rval = false;
            break;
        }
    }
    else
    {
        MXS_ERROR("Negative value for monitor timeout.");
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
monitorGetList()
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
    if (monitor->databases == NULL || // No servers to check
        config_get_global_options()->skip_permission_checks)
    {
        return true;
    }

    char *user = monitor->user;
    char *dpasswd = decrypt_password(monitor->password);
    MXS_CONFIG* cnf = config_get_global_options();
    bool rval = false;

    for (MXS_MONITOR_SERVERS *mondb = monitor->databases; mondb; mondb = mondb->next)
    {
        if (mon_ping_or_connect_to_db(monitor, mondb) != MONITOR_CONN_OK)
        {
            MXS_ERROR("[%s] Failed to connect to server '%s' ([%s]:%d) when"
                      " checking monitor user credentials and permissions: %s",
                      monitor->name, mondb->server->unique_name, mondb->server->name,
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
        else if (mysql_query(mondb->con, query) != 0)
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
void monitorAddParameters(MXS_MONITOR *monitor, MXS_CONFIG_PARAMETER *params)
{
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
}

bool monitorRemoveParameter(MXS_MONITOR *monitor, const char *key)
{
    MXS_CONFIG_PARAMETER *prev = NULL;

    for (MXS_CONFIG_PARAMETER *p = monitor->parameters; p; p = p->next)
    {
        if (strcmp(p->name, key) == 0)
        {
            if (p == monitor->parameters)
            {
                monitor->parameters = monitor->parameters->next;
                p->next = NULL;
            }
            else
            {
                prev->next = p->next;
                p->next = NULL;
            }
            config_parameter_free(p);
            return true;
        }
        prev = p;
    }
    return false;
}

/**
 * Set a pending status bit in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void
monitor_set_pending_status(MXS_MONITOR_SERVERS *ptr, int bit)
{
    ptr->pending_status |= bit;
}

/**
 * Clear a pending status bit in the monitor server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void
monitor_clear_pending_status(MXS_MONITOR_SERVERS *ptr, int bit)
{
    ptr->pending_status &= ~bit;
}

/*
 * Determine a monitor event, defined by the difference between the old
 * status of a server and the new status.
 *
 * @param   node                The monitor server data for a particular server
 * @result  monitor_event_t     A monitor event (enum)
 */
static mxs_monitor_event_t mon_get_event_type(MXS_MONITOR_SERVERS* node)
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

    unsigned int prev = node->mon_prev_status & all_server_bits;
    unsigned int present = node->server->status & all_server_bits;

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
            unsigned int prev_bits = prev & (SERVER_MASTER | SERVER_SLAVE);
            unsigned int present_bits = present & (SERVER_MASTER | SERVER_SLAVE);

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

/*
 * Given a monitor event (enum) provide a text string equivalent
 * @param   node    The monitor server data whose event is wanted
 * @result  string  The name of the monitor event for the server
 */
static const char* mon_get_event_name(MXS_MONITOR_SERVERS* node)
{
    mxs_monitor_event_t event = mon_get_event_type(node);

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

/**
 * Create a list of running servers
 *
 * @param servers Monitored servers
 * @param dest Destination where the string is appended, must be null terminated
 * @param len Length of @c dest
 */
static void mon_append_node_names(MXS_MONITOR_SERVERS* servers, char* dest, int len, int status)
{
    const char *separator = "";
    char arr[MAX_SERVER_ADDRESS_LEN + 64]; // Some extra space for port and separator
    dest[0] = '\0';

    while (servers && len)
    {
        if (status == 0 || servers->server->status & status)
        {
            snprintf(arr, sizeof(arr), "%s[%s]:%d", separator, servers->server->name,
                     servers->server->port);
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
bool mon_status_changed(MXS_MONITOR_SERVERS* mon_srv)
{
    bool rval = false;

    /* Previous status is -1 if not yet set */
    if (mon_srv->mon_prev_status != static_cast<uint32_t>(-1))
    {

        unsigned int old_status = mon_srv->mon_prev_status & all_server_bits;
        unsigned int new_status = mon_srv->server->status & all_server_bits;

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
mon_print_fail_status(MXS_MONITOR_SERVERS* mon_srv)
{
    return (SERVER_IS_DOWN(mon_srv->server) && mon_srv->mon_err_count == 0);
}

/**
 * Launch a script
 * @param mon Owning monitor
 * @param ptr The server which has changed state
 * @param script Script to execute
 */
void
monitor_launch_script(MXS_MONITOR* mon, MXS_MONITOR_SERVERS* ptr, const char* script)
{
    char arg[strlen(script) + 1];
    strcpy(arg, script);

    EXTERNCMD* cmd = externcmd_allocate(arg, mon->script_timeout);

    if (cmd == NULL)
    {
        MXS_ERROR("Failed to initialize script '%s'. See previous errors for the "
                  "cause of this failure.", script);
        return;
    }

    if (externcmd_matches(cmd, "$INITIATOR"))
    {
        char initiator[strlen(ptr->server->name) + 24]; // Extra space for port
        snprintf(initiator, sizeof(initiator), "[%s]:%d", ptr->server->name, ptr->server->port);
        externcmd_substitute_arg(cmd, "[$]INITIATOR", initiator);
    }

    if (externcmd_matches(cmd, "$EVENT"))
    {
        externcmd_substitute_arg(cmd, "[$]EVENT", mon_get_event_name(ptr));
    }

    char nodelist[PATH_MAX + MON_ARG_MAX + 1] = {'\0'};

    if (externcmd_matches(cmd, "$NODELIST"))
    {
        mon_append_node_names(mon->databases, nodelist, sizeof(nodelist), SERVER_RUNNING);
        externcmd_substitute_arg(cmd, "[$]NODELIST", nodelist);
    }

    if (externcmd_matches(cmd, "$LIST"))
    {
        mon_append_node_names(mon->databases, nodelist, sizeof(nodelist), 0);
        externcmd_substitute_arg(cmd, "[$]LIST", nodelist);
    }

    if (externcmd_matches(cmd, "$MASTERLIST"))
    {
        mon_append_node_names(mon->databases, nodelist, sizeof(nodelist), SERVER_MASTER);
        externcmd_substitute_arg(cmd, "[$]MASTERLIST", nodelist);
    }

    if (externcmd_matches(cmd, "$SLAVELIST"))
    {
        mon_append_node_names(mon->databases, nodelist, sizeof(nodelist), SERVER_SLAVE);
        externcmd_substitute_arg(cmd, "[$]SLAVELIST", nodelist);
    }

    if (externcmd_matches(cmd, "$SYNCEDLIST"))
    {
        mon_append_node_names(mon->databases, nodelist, sizeof(nodelist), SERVER_JOINED);
        externcmd_substitute_arg(cmd, "[$]SYNCEDLIST", nodelist);
    }

    char* out = NULL;
    std::string str;
    int rv = externcmd_execute(cmd, &out);

    if (out)
    {
        str = trim(out);
        MXS_FREE(out);
    }

    if (rv)
    {
        if (rv == -1)
        {
            // Internal error
            MXS_ERROR("Failed to execute script '%s' on server state change event '%s': %s",
                      script, mon_get_event_name(ptr), str.c_str());
        }
        else
        {
            // Script returned a non-zero value
            MXS_ERROR("Script '%s' returned %d on event '%s': %s",
                      script, rv, mon_get_event_name(ptr), str.c_str());
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

        MXS_NOTICE("Executed monitor script '%s' on event '%s': %s",
                   scriptStr, mon_get_event_name(ptr), str.c_str());

        if (!memError)
        {
            MXS_FREE(scriptStr);
        }
    }

    externcmd_free(cmd);
}

/**
 * Ping or, if connection does not exist or ping fails, connect to a database. This
 * will always leave a valid database handle in the database->con pointer, allowing
 * the user to call MySQL C API functions to find out the reason of the failure.
 *
 * @param mon Monitor
 * @param database Monitored database
 * @return MONITOR_CONN_OK if the connection is OK, else the reason for the failure
 */
mxs_connect_result_t
mon_ping_or_connect_to_db(MXS_MONITOR* mon, MXS_MONITOR_SERVERS *database)
{
    /** Return if the connection is OK */
    if (database->con && mysql_ping(database->con) == 0)
    {
        return MONITOR_CONN_OK;
    }

    if (database->con)
    {
        mysql_close(database->con);
    }

    mxs_connect_result_t rval = MONITOR_CONN_REFUSED;
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
                rval = MONITOR_CONN_OK;
                break;
            }
        }

        if (rval == MONITOR_CONN_REFUSED &&
            (int)difftime(end, start) >= mon->connect_timeout)
        {
            rval = MONITOR_CONN_TIMEOUT;
        }
        MXS_FREE(dpwd);
    }

    return rval;
}

/**
 * Log an error about the failure to connect to a backend server
 * and why it happened.
 * @param database Backend database
 * @param rval Return value of mon_connect_to_db
 */
void
mon_log_connect_error(MXS_MONITOR_SERVERS* database, mxs_connect_result_t rval)
{
    MXS_ERROR(rval == MONITOR_CONN_TIMEOUT ?
              "Monitor timed out when connecting to server [%s]:%d : \"%s\"" :
              "Monitor was unable to connect to server [%s]:%d : \"%s\"",
              database->server->name, database->server->port,
              mysql_error(database->con));
}

static void mon_log_state_change(MXS_MONITOR_SERVERS *ptr)
{
    SERVER srv;
    srv.status = ptr->mon_prev_status;
    char *prev = server_status(&srv);
    char *next = server_status(ptr->server);
    MXS_NOTICE("Server changed state: %s[%s:%u]: %s. [%s] -> [%s]",
               ptr->server->unique_name, ptr->server->name, ptr->server->port,
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
            for (MXS_MONITOR_SERVERS *db = mon->databases; db && !rval; db = db->next)
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

    if (monitor->databases)
    {
        dprintf(file, "%s=", CN_SERVERS);
        for (MXS_MONITOR_SERVERS *db = monitor->databases; db; db = db->next)
        {
            if (db != monitor->databases)
            {
                dprintf(file, ",");
            }
            dprintf(file, "%s", db->server->unique_name);
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
    for (MXS_MONITOR_SERVERS *ptr = monitor->databases; ptr; ptr = ptr->next)
    {
        if (mon_status_changed(ptr) &&
            (!(SERVER_IS_RUNNING(ptr->server)) ||
             !(SERVER_IS_IN_CLUSTER(ptr->server))))
        {
            dcb_hangup_foreach(ptr->server);
        }
    }
}

void mon_report_query_error(MXS_MONITOR_SERVERS* db)
{
    MXS_ERROR("Failed to execute query on server '%s' ([%s]:%d): %s",
              db->server->unique_name, db->server->name,
              db->server->port, mysql_error(db->con));
}

/**
  * Acquire locks on all servers monitored by this monitor. There should
  * only be max 1 monitor per server.
  * @param monitor The target monitor
  */
void lock_monitor_servers(MXS_MONITOR *monitor)
{
    MXS_MONITOR_SERVERS *ptr = monitor->databases;
    while (ptr)
    {
        spinlock_acquire(&ptr->server->lock);
        ptr = ptr->next;
    }
}
/**
  * Release locks on all servers monitored by this monitor. There should
  * only be max 1 monitor per server.
  * @param monitor The target monitor
  */
void release_monitor_servers(MXS_MONITOR *monitor)
{
    MXS_MONITOR_SERVERS *ptr = monitor->databases;
    while (ptr)
    {
        spinlock_release(&ptr->server->lock);
        ptr = ptr->next;
    }
}
/**
  * Sets the current status of all servers monitored by this monitor to
  * the pending status. This should only be called at the beginning of
  * a monitor loop, after the servers are locked.
  * @param monitor The target monitor
  */
void servers_status_pending_to_current(MXS_MONITOR *monitor)
{
    MXS_MONITOR_SERVERS *ptr = monitor->databases;
    while (ptr)
    {
        ptr->server->status = ptr->server->status_pending;
        ptr = ptr->next;
    }
    monitor->server_pending_changes = false;
}
/**
  *  Sets the pending status of all servers monitored by this monitor to
  *  the current status. This should only be called at the end of
  *  a monitor loop, before the servers are released.
  *  @param monitor The target monitor
  */
void servers_status_current_to_pending(MXS_MONITOR *monitor)
{
    MXS_MONITOR_SERVERS *ptr = monitor->databases;
    while (ptr)
    {
        ptr->server->status_pending = ptr->server->status;
        ptr = ptr->next;
    }
}

void mon_process_state_changes(MXS_MONITOR *monitor, const char *script, uint64_t events)
{
    for (MXS_MONITOR_SERVERS *ptr = monitor->databases; ptr; ptr = ptr->next)
    {
        if (mon_status_changed(ptr))
        {
            mon_log_state_change(ptr);

            if (script && (events & mon_get_event_type(ptr)))
            {
                monitor_launch_script(monitor, ptr, script);
            }
        }
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

    if (monitor->handle && monitor->module->diagnostics)
    {
        json_t* diag = monitor->module->diagnostics_json(monitor);

        if (diag)
        {
            json_object_set_new(attr, "monitor_diagnostics", diag);
        }
    }

    json_t* rel = json_object();


    if (monitor->databases)
    {
        json_t* mon_rel = mxs_json_relationship(host, MXS_JSON_API_SERVERS);

        for (MXS_MONITOR_SERVERS *db = monitor->databases; db; db = db->next)
        {
            mxs_json_add_relation(mon_rel, db->server->unique_name, CN_SERVERS);
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
    json_t* rel = mxs_json_relationship(host, MXS_JSON_API_MONITORS);

    spinlock_acquire(&monLock);

    for (MXS_MONITOR* mon = allMonitors; mon; mon = mon->next)
    {
        spinlock_acquire(&mon->lock);

        if (mon->active)
        {
            for (MXS_MONITOR_SERVERS* db = mon->databases; db; db = db->next)
            {
                if (db->server == server)
                {
                    mxs_json_add_relation(rel, mon->name, CN_MONITORS);
                    break;
                }
            }
        }

        spinlock_release(&mon->lock);
    }

    spinlock_release(&monLock);

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
static void store_data(MXS_MONITOR *monitor, MXS_MONITOR_SERVERS *master, uint8_t *data, uint32_t size)
{
    uint8_t* ptr = data;

    /** Store the data length */
    ss_dassert(sizeof(size) == MMB_LEN_BYTES);
    ptr = mxs_set_byte4(ptr, size);

    /** Then the schema version */
    *ptr++ = MMB_SCHEMA_VERSION;

    /** Store the states of all servers */
    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        *ptr++ = (char)SVT_SERVER; // Value type
        memcpy(ptr, db->server->unique_name, strlen(db->server->unique_name)); // Name of the server
        ptr += strlen(db->server->unique_name);
        *ptr++ = '\0'; // Null-terminate the string

        uint32_t status = db->server->status; // Server status as 4 byte integer
        ss_dassert(sizeof(status) == MMB_LEN_SERVER_STATUS);
        ptr = mxs_set_byte4(ptr, status);
    }

    /** Store the current root master if we have one */
    if (master)
    {
        *ptr++ = (char)SVT_MASTER;
        memcpy(ptr, master->server->unique_name, strlen(master->server->unique_name));
        ptr += strlen(master->server->unique_name);
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
    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        if (strcmp(db->server->unique_name, data) == 0)
        {
            const unsigned char *sptr = (unsigned char*)strchr(data, '\0');
            ss_dassert(sptr);
            sptr++;

            uint32_t state = mxs_get_byte4(sptr);
            db->mon_prev_status = state;
            db->server->status_pending = state;
            server_set_status_nolock(db->server, state);
            monitor_set_pending_status(db, state);
            break;
        }
    }

    data += strlen(data) + 1 + MMB_LEN_SERVER_STATUS;

    return data;
}

/**
 * Process a master
 */
static const char* process_master(MXS_MONITOR *monitor, MXS_MONITOR_SERVERS **master, const char *data,
                                  const char *end)
{
    if (master)
    {
        for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
        {
            if (strcmp(db->server->unique_name, data) == 0)
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
static bool process_data_file(MXS_MONITOR *monitor, MXS_MONITOR_SERVERS **master, const char *data,
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

void store_server_journal(MXS_MONITOR *monitor, MXS_MONITOR_SERVERS *master)
{
    /** Calculate how much memory we need to allocate */
    uint32_t size = MMB_LEN_SCHEMA_VERSION + MMB_LEN_CRC32;

    for (MXS_MONITOR_SERVERS* db = monitor->databases; db; db = db->next)
    {
        /** Each server is stored as a type byte and a null-terminated string
         * followed by eight byte server status. */
        size += MMB_LEN_VALUE_TYPE + strlen(db->server->unique_name) + 1 + MMB_LEN_SERVER_STATUS;
    }

    if (master)
    {
        /** The master server name is stored as a null terminated string */
        size += MMB_LEN_VALUE_TYPE + strlen(master->server->unique_name) + 1;
    }

    /** 4 bytes for file length, 1 byte for schema version and 4 bytes for CRC32 */
    uint32_t buffer_size = size + MMB_LEN_BYTES;
    uint8_t *data = (uint8_t*)MXS_MALLOC(buffer_size);
    char path[PATH_MAX + 1];

    if (data)
    {
        /** Store the data in memory first */
        store_data(monitor, master, data, size);

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
            }
            else
            {
                MXS_ERROR("Failed to write journal data to disk: %d, %s",
                          errno, mxs_strerror(errno));
            }
            fclose(file);
        }
    }
    MXS_FREE(data);
}

void load_server_journal(MXS_MONITOR *monitor, MXS_MONITOR_SERVERS **master)
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
