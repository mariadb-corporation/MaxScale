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
 * @file A MariaDB replication cluster monitor
 */
#include "mariadbmon.hh"
#include <inttypes.h>
#include <sstream>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/debug.h>
#include <maxscale/modulecmd.h>

#include <maxscale/mysql_utils.h>
#include <maxscale/secrets.h>
#include <maxscale/utils.h>
// TODO: For monitorAddParameters
#include "../../../core/internal/monitor.h"

using std::string;

static void monitorMain(void *);

// Config parameter names
const char * const CN_AUTO_FAILOVER       = "auto_failover";
static const char CN_AUTO_REJOIN[]        = "auto_rejoin";
static const char CN_FAILCOUNT[]          = "failcount";
static const char CN_NO_PROMOTE_SERVERS[] = "servers_no_promotion";
static const char CN_FAILOVER_TIMEOUT[]   = "failover_timeout";
static const char CN_SWITCHOVER_TIMEOUT[] = "switchover_timeout";
// Parameters for master failure verification and timeout
static const char CN_VERIFY_MASTER_FAILURE[]    = "verify_master_failure";
static const char CN_MASTER_FAILURE_TIMEOUT[]   = "master_failure_timeout";
// Replication credentials parameters for failover/switchover/join
static const char CN_REPLICATION_USER[]     = "replication_user";
static const char CN_REPLICATION_PASSWORD[] = "replication_password";

/** Default port */
const int PORT_UNKNOWN = 0;

MariaDBMonitor::MariaDBMonitor(MXS_MONITOR* monitor_base)
    : m_monitor_base(monitor_base)
    , m_id(config_get_global_options()->id)
    , m_master_gtid_domain(-1)
    , m_external_master_port(PORT_UNKNOWN)
    , m_warn_set_standalone_master(true)
{}

MariaDBMonitor::~MariaDBMonitor()
{}

/**
 * Initialize the server info hashtable.
 */
void MariaDBMonitor::init_server_info()
{
    // If this monitor is being restarted, the server data needs to be freed.
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        delete *iter;
    }
    m_servers.clear();
    for (auto mon_server = m_monitor_base->monitored_servers; mon_server; mon_server = mon_server->next)
    {
        m_servers.push_back(new MariaDBServer(mon_server));
    }

    m_server_info.clear();
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        auto mon_server = (*iter)->server_base;
        ss_dassert(m_server_info.count(mon_server) == 0);
        ServerInfoMap::value_type new_val(mon_server, *iter);
        m_server_info.insert(new_val);
    }
}

/**
     * Get monitor-specific server info for the monitored server.
     *
     * @param handle
     * @param db Server to get info for. Must be a valid server or function crashes.
     * @return The server info.
     */
MariaDBServer* MariaDBMonitor::get_server_info(MXS_MONITORED_SERVER* db)
{
    ss_dassert(m_server_info.count(db) == 1); // Should always exist in the map
    return m_server_info[db];
}

/**
 * Constant version of get_server_info().
 */
const MariaDBServer* MariaDBMonitor::get_server_info(const MXS_MONITORED_SERVER* db) const
{
    return const_cast<MariaDBMonitor*>(this)->get_server_info(const_cast<MXS_MONITORED_SERVER*>(db));
}

bool MariaDBMonitor::set_replication_credentials(const MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    string repl_user = config_get_string(params, CN_REPLICATION_USER);
    string repl_pw = config_get_string(params, CN_REPLICATION_PASSWORD);

    if (repl_user.empty() && repl_pw.empty())
    {
        // No replication credentials defined, use monitor credentials
        repl_user = m_monitor_base->user;
        repl_pw = m_monitor_base->password;
    }

    if (!repl_user.empty() && !repl_pw.empty())
    {
        m_replication_user = repl_user;
        char* decrypted = decrypt_password(repl_pw.c_str());
        m_replication_password = decrypted;
        MXS_FREE(decrypted);
        rval = true;
    }

    return rval;
}

MariaDBMonitor* MariaDBMonitor::start(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(monitor->handle);
    if (handle == NULL)
    {
        handle = new MariaDBMonitor(monitor);
    }

    /* Always reset these values. The server dependent values must be reset as servers could have been
     * added and removed. */
    handle->m_keep_running = true;
    handle->m_master = NULL;
    handle->init_server_info();

    if (!handle->load_config_params(params))
    {
        error = true;
    }

    if (!check_monitor_permissions(monitor, "SHOW SLAVE STATUS"))
    {
        error = true;
    }

    if (!error)
    {
        if (thread_start(&handle->m_thread, monitorMain, handle, 0) == NULL)
        {
            MXS_ERROR("Failed to start monitor thread for monitor '%s'.", monitor->name);
            error = true;
        }
    }

    if (error)
    {
        MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        delete handle;
        handle = NULL;
    }
    return handle;
}

/**
 * Load config parameters
 *
 * @param params Config parameters
 * @return True if settings are ok
 */
bool MariaDBMonitor::load_config_params(const MXS_CONFIG_PARAMETER* params)
{
    m_detect_stale_master = config_get_bool(params, "detect_stale_master");
    m_detect_stale_slave = config_get_bool(params, "detect_stale_slave");
    m_detect_replication_lag = config_get_bool(params, "detect_replication_lag");
    m_detect_multimaster = config_get_bool(params, "multimaster");
    m_ignore_external_masters = config_get_bool(params, "ignore_external_masters");
    m_detect_standalone_master = config_get_bool(params, "detect_standalone_master");
    m_failcount = config_get_integer(params, CN_FAILCOUNT);
    m_allow_cluster_recovery = config_get_bool(params, "allow_cluster_recovery");
    m_mysql51_replication = config_get_bool(params, "mysql51_replication");
    m_script = config_get_string(params, "script");
    m_events = config_get_enum(params, "events", mxs_monitor_event_enum_values);
    m_failover_timeout = config_get_integer(params, CN_FAILOVER_TIMEOUT);
    m_switchover_timeout = config_get_integer(params, CN_SWITCHOVER_TIMEOUT);
    m_auto_failover = config_get_bool(params, CN_AUTO_FAILOVER);
    m_auto_rejoin = config_get_bool(params, CN_AUTO_REJOIN);
    m_verify_master_failure = config_get_bool(params, CN_VERIFY_MASTER_FAILURE);
    m_master_failure_timeout = config_get_integer(params, CN_MASTER_FAILURE_TIMEOUT);

    m_excluded_servers.clear();
    MXS_MONITORED_SERVER** excluded_array = NULL;
    int n_excluded = mon_config_get_servers(params, CN_NO_PROMOTE_SERVERS, m_monitor_base, &excluded_array);
    for (int i = 0; i < n_excluded; i++)
    {
        m_excluded_servers.push_back(get_server_info(excluded_array[i]));
    }
    MXS_FREE(excluded_array);

    bool settings_ok = true;
    if (!set_replication_credentials(params))
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", CN_REPLICATION_USER, CN_REPLICATION_PASSWORD);
        settings_ok = false;
    }
    return settings_ok;
}

/**
 * Stop the monitor.
 *
 * @return True, if the monitor had to be stopped. False, if the monitor already was stopped.
 */
bool MariaDBMonitor::stop()
{
    // There should be no race here as long as admin operations are performed
    // with the single admin lock locked.
    bool actually_stopped = false;
    if (m_status == MXS_MONITOR_RUNNING)
    {
        m_keep_running = false;
        thread_wait(m_thread);
        actually_stopped = true;
    }
    return actually_stopped;
}

void MariaDBMonitor::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Automatic failover:     %s\n", m_auto_failover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Failcount:              %d\n", m_failcount);
    dcb_printf(dcb, "Failover timeout:       %u\n", m_failover_timeout);
    dcb_printf(dcb, "Switchover timeout:     %u\n", m_switchover_timeout);
    dcb_printf(dcb, "Automatic rejoin:       %s\n", m_auto_rejoin ? "Enabled" : "Disabled");
    dcb_printf(dcb, "MaxScale monitor ID:    %lu\n", m_id);
    dcb_printf(dcb, "Detect replication lag: %s\n", (m_detect_replication_lag) ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Detect stale master:    %s\n", (m_detect_stale_master == 1) ?
               "Enabled" : "Disabled");
    if (m_excluded_servers.size() > 0)
    {
        dcb_printf(dcb, "Non-promotable servers (failover): ");
        dcb_printf(dcb, "%s\n", monitored_servers_to_string(m_excluded_servers).c_str());
    }

    dcb_printf(dcb, "\nServer information:\n-------------------\n\n");
    for (MXS_MONITORED_SERVER *db = m_monitor_base->monitored_servers; db; db = db->next)
    {
        const MariaDBServer* serv_info = get_server_info(db);
        dcb_printf(dcb, "Server:                 %s\n", db->server->unique_name);
        dcb_printf(dcb, "Server ID:              %" PRId64 "\n", serv_info->server_id);
        dcb_printf(dcb, "Read only:              %s\n", serv_info->read_only ? "YES" : "NO");
        dcb_printf(dcb, "Slave configured:       %s\n", serv_info->slave_configured ? "YES" : "NO");
        if (serv_info->slave_configured)
        {
            dcb_printf(dcb, "Slave IO running:       %s\n",
                       serv_info->slave_status.slave_io_running ? "YES" : "NO");
            dcb_printf(dcb, "Slave SQL running:      %s\n",
                       serv_info->slave_status.slave_sql_running ? "YES" : "NO");
            dcb_printf(dcb, "Master ID:              %" PRId64 "\n",
                       serv_info->slave_status.master_server_id);
            dcb_printf(dcb, "Master binlog file:     %s\n",
                       serv_info->slave_status.master_log_file.c_str());
            dcb_printf(dcb, "Master binlog position: %lu\n",
                       serv_info->slave_status.read_master_log_pos);
        }
        if (!serv_info->gtid_current_pos.empty())
        {
            dcb_printf(dcb, "Gtid current position:  %s\n",
                       serv_info->gtid_current_pos.to_string().c_str());
        }
        if (!serv_info->gtid_binlog_pos.empty())
        {
            dcb_printf(dcb, "Gtid binlog position:   %s\n",
                       serv_info->gtid_current_pos.to_string().c_str());
        }
        if (!serv_info->slave_status.gtid_io_pos.empty())
        {
            dcb_printf(dcb, "Gtid slave IO position: %s\n",
                       serv_info->slave_status.gtid_io_pos.to_string().c_str());
        }
        if (m_detect_multimaster)
        {
            dcb_printf(dcb, "Master group:           %d\n", serv_info->group);
        }

        dcb_printf(dcb, "\n");
    }
}

json_t* MariaDBMonitor::diagnostics_json() const
{
    json_t* rval = json_object();
    json_object_set_new(rval, "monitor_id", json_integer(m_id));
    json_object_set_new(rval, "detect_stale_master", json_boolean(m_detect_stale_master));
    json_object_set_new(rval, "detect_stale_slave", json_boolean(m_detect_stale_slave));
    json_object_set_new(rval, "detect_replication_lag", json_boolean(m_detect_replication_lag));
    json_object_set_new(rval, "multimaster", json_boolean(m_detect_multimaster));
    json_object_set_new(rval, "detect_standalone_master", json_boolean(m_detect_standalone_master));
    json_object_set_new(rval, CN_FAILCOUNT, json_integer(m_failcount));
    json_object_set_new(rval, "allow_cluster_recovery", json_boolean(m_allow_cluster_recovery));
    json_object_set_new(rval, "mysql51_replication", json_boolean(m_mysql51_replication));
    json_object_set_new(rval, CN_AUTO_FAILOVER, json_boolean(m_auto_failover));
    json_object_set_new(rval, CN_FAILOVER_TIMEOUT, json_integer(m_failover_timeout));
    json_object_set_new(rval, CN_SWITCHOVER_TIMEOUT, json_integer(m_switchover_timeout));
    json_object_set_new(rval, CN_AUTO_REJOIN, json_boolean(m_auto_rejoin));

    if (!m_script.empty())
    {
        json_object_set_new(rval, "script", json_string(m_script.c_str()));
    }
    if (m_excluded_servers.size() > 0)
    {
        string list = monitored_servers_to_string(m_excluded_servers);
        json_object_set_new(rval, CN_NO_PROMOTE_SERVERS, json_string(list.c_str()));
    }
    if (m_monitor_base->monitored_servers)
    {
        json_t* arr = json_array();

        for (MXS_MONITORED_SERVER *db = m_monitor_base->monitored_servers; db; db = db->next)
        {
            json_t* srv = json_object();
            const MariaDBServer* serv_info = get_server_info(db);
            json_object_set_new(srv, "name", json_string(db->server->unique_name));
            json_object_set_new(srv, "server_id", json_integer(serv_info->server_id));
            json_object_set_new(srv, "master_id", json_integer(serv_info->slave_status.master_server_id));

            json_object_set_new(srv, "read_only", json_boolean(serv_info->read_only));
            json_object_set_new(srv, "slave_configured", json_boolean(serv_info->slave_configured));
            json_object_set_new(srv, "slave_io_running",
                                json_boolean(serv_info->slave_status.slave_io_running));
            json_object_set_new(srv, "slave_sql_running",
                                json_boolean(serv_info->slave_status.slave_sql_running));

            json_object_set_new(srv, "master_binlog_file",
                                json_string(serv_info->slave_status.master_log_file.c_str()));
            json_object_set_new(srv, "master_binlog_position",
                                json_integer(serv_info->slave_status.read_master_log_pos));
            json_object_set_new(srv, "gtid_current_pos",
                                json_string(serv_info->gtid_current_pos.to_string().c_str()));
            json_object_set_new(srv, "gtid_binlog_pos",
                                json_string(serv_info->gtid_binlog_pos.to_string().c_str()));
            json_object_set_new(srv, "gtid_io_pos",
                                json_string(serv_info->slave_status.gtid_io_pos.to_string().c_str()));
            if (m_detect_multimaster)
            {
                json_object_set_new(srv, "master_group", json_integer(serv_info->group));
            }

            json_array_append_new(arr, srv);
        }

        json_object_set_new(rval, "server_info", arr);
    }

    return rval;
}

void MariaDBMonitor::main_loop()
{
    m_status = MXS_MONITOR_RUNNING;
    MariaDBServer* root_master = NULL;
    int log_no_master = 1;

    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in monitor module. Exiting.");
        m_status = MXS_MONITOR_STOPPED;
        return;
    }

    load_journal();

    if (m_detect_replication_lag)
    {
        check_maxscale_schema_replication();
    }

    while (m_keep_running)
    {
        timespec loop_start;
        /* Coarse time has resolution ~1ms (as opposed to 1ns) but this is enough. */
        clock_gettime(CLOCK_MONOTONIC_COARSE, &loop_start);

        lock_monitor_servers(m_monitor_base);
        servers_status_pending_to_current(m_monitor_base);

        // Query all servers for their status.
        for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
        {
            monitor_one_server(**iter);
        }

        // Use the information to find the so far best master server.
        root_master = find_root_master();

        if (m_master != NULL && m_master->is_master())
        {
            // Update cluster-wide values dependant on the current master.
            update_gtid_domain();
            update_external_master();
        }

        // Assign relay masters, clear SERVER_SLAVE from binlog relays
        for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
        {
            assign_relay_master(**iter);

            /* Remove SLAVE status if this server is a Binlog Server relay */
            if ((*iter)->binlog_relay)
            {
                monitor_clear_pending_status((*iter)->server_base, SERVER_SLAVE);
            }
        }

        /* Update server status from monitor pending status on that server*/
        for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
        {
            update_server_states(**iter, root_master);
        }

        /** Now that all servers have their status correctly set, we can check
            if we need to use standalone master. */
        if (m_detect_standalone_master)
        {
            if (standalone_master_required(m_monitor_base->monitored_servers))
            {
                // Other servers have died, set last remaining server as master
                if (set_standalone_master(m_monitor_base->monitored_servers))
                {
                    // Update the root_master to point to the standalone master
                    root_master = m_master;
                }
            }
            else
            {
                m_warn_set_standalone_master = true;
            }
        }

        if (root_master && root_master->is_master())
        {
            SERVER* root_master_server = root_master->server_base->server;
            // Clear slave and stale slave status bits from current master
            server_clear_status_nolock(root_master_server, SERVER_SLAVE | SERVER_STALE_SLAVE);
            monitor_clear_pending_status(root_master->server_base, SERVER_SLAVE | SERVER_STALE_SLAVE);

            /**
             * Clear external slave status from master if configured to do so.
             * This allows parts of a multi-tiered replication setup to be used
             * in MaxScale.
             */
            if (m_ignore_external_masters)
            {
                monitor_clear_pending_status(root_master->server_base, SERVER_SLAVE_OF_EXTERNAL_MASTER);
                server_clear_status_nolock(root_master_server, SERVER_SLAVE_OF_EXTERNAL_MASTER);
            }
        }

        ss_dassert(root_master == NULL || root_master == m_master);
        ss_dassert(root_master == NULL ||
                   ((root_master->server_base->server->status & (SERVER_SLAVE | SERVER_MASTER)) !=
                    (SERVER_SLAVE | SERVER_MASTER)));

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(m_monitor_base, m_script.c_str(), m_events);
        bool failover_performed = false; // Has an automatic failover been performed this loop?

        if (m_auto_failover)
        {
            handle_auto_failover(&failover_performed);
        }

        /* log master detection failure of first master becomes available after failure */
        log_master_changes(root_master, &log_no_master);

        /* Generate the replication heartbeat event by performing an update */
        if (m_detect_replication_lag && root_master &&
            (root_master->is_master() || SERVER_IS_RELAY_SERVER(root_master->server_base->server)))
        {
            measure_replication_lag(root_master);
        }

        // Do not auto-join servers on this monitor loop if a failover (or any other cluster modification)
        // has been performed, as server states have not been updated yet. It will happen next iteration.
        if (!config_get_global_options()->passive && m_auto_rejoin &&
            !failover_performed && cluster_can_be_joined())
        {
            // Check if any servers should be autojoined to the cluster and try to join them.
            handle_auto_rejoin();
        }

        mon_hangup_failed_servers(m_monitor_base);
        servers_status_current_to_pending(m_monitor_base);
        store_server_journal(m_monitor_base, m_master->server_base);
        release_monitor_servers(m_monitor_base);

        // Check how much the monitor should sleep to get one full monitor interval.
        timespec loop_end;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &loop_end);
        int64_t time_elapsed_ms = (loop_end.tv_sec - loop_start.tv_sec) * 1000 +
                                  (loop_end.tv_nsec - loop_start.tv_nsec) / 1000000;

        /* Sleep in small increments to react faster to some events. This should ideally use some type of
         * notification mechanism. */
        int sleep_cycles = ((m_monitor_base->interval - time_elapsed_ms) / MXS_MON_BASE_INTERVAL_MS);
        sleep_cycles = MXS_MAX(1, sleep_cycles); // Sleep at least once.
        for (int i = 0; i < sleep_cycles; i++)
        {
            thread_millisleep(MXS_MON_BASE_INTERVAL_MS);
            if (!m_keep_running || m_monitor_base->server_pending_changes)
            {
                break;
            }
        }
    }

    m_status = MXS_MONITOR_STOPPING;
    mysql_thread_end();
    m_status = MXS_MONITOR_STOPPED;
}

void MariaDBMonitor::update_gtid_domain()
{
    int64_t domain = m_master->gtid_domain_id;
    if (m_master_gtid_domain >= 0 && domain != m_master_gtid_domain)
    {
        MXS_NOTICE("Gtid domain id of master has changed: %" PRId64 " -> %" PRId64 ".",
                   m_master_gtid_domain, domain);
    }
    m_master_gtid_domain = domain;
}

void MariaDBMonitor::update_external_master()
{
    if (SERVER_IS_SLAVE_OF_EXTERNAL_MASTER(m_master->server_base->server))
    {
        if (m_master->slave_status.master_host != m_external_master_host ||
            m_master->slave_status.master_port != m_external_master_port)
        {
            const string new_ext_host =  m_master->slave_status.master_host;
            const int new_ext_port = m_master->slave_status.master_port;
            if (m_external_master_port == PORT_UNKNOWN)
            {
                MXS_NOTICE("Cluster master server is replicating from an external master: %s:%d",
                           new_ext_host.c_str(), new_ext_port);
            }
            else
            {
                MXS_NOTICE("The external master of the cluster has changed: %s:%d -> %s:%d.",
                           m_external_master_host.c_str(), m_external_master_port,
                           new_ext_host.c_str(), new_ext_port);
            }
            m_external_master_host = new_ext_host;
            m_external_master_port = new_ext_port;
        }
    }
    else
    {
        if (m_external_master_port != PORT_UNKNOWN)
        {
            MXS_NOTICE("Cluster lost the external master.");
        }
        m_external_master_host.clear();
        m_external_master_port = PORT_UNKNOWN;
    }
}

void MariaDBMonitor::measure_replication_lag(MariaDBServer* root_master_server)
{
    MXS_MONITORED_SERVER* root_master = root_master_server ? root_master_server->server_base : NULL;
    set_master_heartbeat(root_master);
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MXS_MONITORED_SERVER* ptr = (*iter)->server_base;
        if ((!SERVER_IN_MAINT(ptr->server)) && SERVER_IS_RUNNING(ptr->server))
        {
            if (ptr->server->node_id != root_master->server->node_id &&
                (SERVER_IS_SLAVE(ptr->server) ||
                 SERVER_IS_RELAY_SERVER(ptr->server)) &&
                !(*iter)->binlog_relay)  // No select lag for Binlog Server
            {
                set_slave_heartbeat(ptr);
            }
        }
    }
}

void MariaDBMonitor::handle_auto_failover(bool* failover_performed)
{
    const char RE_ENABLE_FMT[] = "%s To re-enable failover, manually set '%s' to 'true' for monitor "
                                 "'%s' via MaxAdmin or the REST API, or restart MaxScale.";
    if (failover_not_possible())
    {
        const char PROBLEMS[] = "Failover is not possible due to one or more problems in the "
                                "replication configuration, disabling automatic failover. Failover "
                                "should only be enabled after the replication configuration has been "
                                "fixed.";
        MXS_ERROR(RE_ENABLE_FMT, PROBLEMS, CN_AUTO_FAILOVER, m_monitor_base->name);
        m_auto_failover = false;
        disable_setting(CN_AUTO_FAILOVER);
    }
    // If master seems to be down, check if slaves are receiving events.
    else if (m_verify_master_failure && m_master && m_master->is_down() && slave_receiving_events())
    {
        MXS_INFO("Master failure not yet confirmed by slaves, delaying failover.");
    }
    else if (!mon_process_failover(failover_performed))
    {
        const char FAILED[] = "Failed to perform failover, disabling automatic failover.";
        MXS_ERROR(RE_ENABLE_FMT, FAILED, CN_AUTO_FAILOVER, m_monitor_base->name);
        m_auto_failover = false;
        disable_setting(CN_AUTO_FAILOVER);
    }
}

void MariaDBMonitor::log_master_changes(MariaDBServer* root_master_server, int* log_no_master)
{
    MXS_MONITORED_SERVER* root_master = root_master_server ? root_master_server->server_base : NULL;
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
        *log_no_master = 1;
    }
    else
    {
        if (!root_master && *log_no_master)
        {
            MXS_ERROR("No Master can be determined");
            *log_no_master = 0;
        }
    }
}

void MariaDBMonitor::handle_auto_rejoin()
{
    ServerArray joinable_servers;
    if (get_joinable_servers(&joinable_servers))
    {
        uint32_t joins = do_rejoin(joinable_servers);
        if (joins > 0)
        {
            MXS_NOTICE("%d server(s) redirected or rejoined the cluster.", joins);
        }
        if (joins < joinable_servers.size())
        {
            MXS_ERROR("A cluster join operation failed, disabling automatic rejoining. "
                      "To re-enable, manually set '%s' to 'true' for monitor '%s' via MaxAdmin or "
                      "the REST API.", CN_AUTO_REJOIN, m_monitor_base->name);
            m_auto_rejoin = false;
            disable_setting(CN_AUTO_REJOIN);
        }
    }
    else
    {
        MXS_ERROR("Query error to master '%s' prevented a possible rejoin operation.", m_master->name());
    }
}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg   The handle of the monitor. Must be the object returned by startMonitor.
 */
static void monitorMain(void *arg)
{
    MariaDBMonitor* handle  = static_cast<MariaDBMonitor*>(arg);
    handle->main_loop();
}

/**
 * Simple wrapper for mxs_mysql_query and mysql_num_rows
 *
 * @param database Database connection
 * @param query    Query to execute
 *
 * @return Number of rows or -1 on error
 */
static int get_row_count(MXS_MONITORED_SERVER *database, const char* query)
{
    int returned_rows = -1;

    if (mxs_mysql_query(database->con, query) == 0)
    {
        MYSQL_RES* result = mysql_store_result(database->con);

        if (result)
        {
            returned_rows = mysql_num_rows(result);
            mysql_free_result(result);
        }
    }

    return returned_rows;
}

/**
 * Write the replication heartbeat into the maxscale_schema.replication_heartbeat table in the current master.
 * The inserted value will be seen from all slaves replicating from this master.
 *
 * @param database      The number database server
 */
void MariaDBMonitor::set_master_heartbeat(MXS_MONITORED_SERVER *database)
{
    time_t heartbeat;
    time_t purge_time;
    char heartbeat_insert_query[512] = "";
    char heartbeat_purge_query[512] = "";

    if (m_master == NULL)
    {
        MXS_ERROR("set_master_heartbeat called without an available Master server");
        return;
    }

    int n_db = get_row_count(database, "SELECT schema_name FROM information_schema.schemata "
                             "WHERE schema_name = 'maxscale_schema'");
    int n_tbl = get_row_count(database, "SELECT table_name FROM information_schema.tables "
                              "WHERE table_schema = 'maxscale_schema' "
                              "AND table_name = 'replication_heartbeat'");

    if (n_db == -1 || n_tbl == -1 ||
        (n_db == 0 && mxs_mysql_query(database->con, "CREATE DATABASE maxscale_schema")) ||
        (n_tbl == 0 && mxs_mysql_query(database->con, "CREATE TABLE IF NOT EXISTS "
                                       "maxscale_schema.replication_heartbeat "
                                       "(maxscale_id INT NOT NULL, "
                                       "master_server_id INT NOT NULL, "
                                       "master_timestamp INT UNSIGNED NOT NULL, "
                                       "PRIMARY KEY ( master_server_id, maxscale_id ) )")))
    {
        MXS_ERROR("Error creating maxscale_schema.replication_heartbeat "
                  "table in Master server: %s", mysql_error(database->con));
        database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
        return;
    }

    /* auto purge old values after 48 hours*/
    purge_time = time(0) - (3600 * 48);

    sprintf(heartbeat_purge_query,
            "DELETE FROM maxscale_schema.replication_heartbeat WHERE master_timestamp < %lu", purge_time);

    if (mxs_mysql_query(database->con, heartbeat_purge_query))
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
            "UPDATE maxscale_schema.replication_heartbeat "
            "SET master_timestamp = %lu WHERE master_server_id = %li AND maxscale_id = %lu",
            heartbeat, m_master->server_base->server->node_id, m_id);

    /* Try to insert MaxScale timestamp into master */
    if (mxs_mysql_query(database->con, heartbeat_insert_query))
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
                    "REPLACE INTO maxscale_schema.replication_heartbeat "
                    "(master_server_id, maxscale_id, master_timestamp ) VALUES ( %li, %lu, %lu)",
                    m_master->server_base->server->node_id, m_id, heartbeat);

            if (mxs_mysql_query(database->con, heartbeat_insert_query))
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

/*
 * This function gets the replication heartbeat from the maxscale_schema.replication_heartbeat table in
 * the current slave and stores the timestamp and replication lag in the slave server struct.
 *
 * @param database      The number database server
 */
void MariaDBMonitor::set_slave_heartbeat(MXS_MONITORED_SERVER *database)
{
    time_t heartbeat;
    char select_heartbeat_query[256] = "";
    MYSQL_ROW row;
    MYSQL_RES *result;

    if (m_master == NULL)
    {
        MXS_ERROR("set_slave_heartbeat called without an available Master server");
        return;
    }

    /* Get the master_timestamp value from maxscale_schema.replication_heartbeat table */

    sprintf(select_heartbeat_query, "SELECT master_timestamp "
            "FROM maxscale_schema.replication_heartbeat "
            "WHERE maxscale_id = %lu AND master_server_id = %li",
            m_id, m_master->server_base->server->node_id);

    /* if there is a master then send the query to the slave with master_id */
    if (m_master != NULL && (mxs_mysql_query(database->con, select_heartbeat_query) == 0
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

            if ((errno == ERANGE && (slave_read == LONG_MAX || slave_read == LONG_MIN)) ||
                (errno != 0 && slave_read == 0))
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
                database->server->rlag = ((unsigned int)rlag > (m_monitor_base->interval / 1000)) ? rlag : 0;
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

        if (m_master->server_base->server->node_id < 0)
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

/**
 * Set a monitor config parameter to "false". The effect persists over stopMonitor/startMonitor but not
 * MaxScale restart. Only use on boolean config settings.
 *
 * @param setting_name Setting to disable
 */
void MariaDBMonitor::disable_setting(const char* setting)
{
    MXS_CONFIG_PARAMETER p = {};
    p.name = const_cast<char*>(setting);
    p.value = const_cast<char*>("false");
    monitorAddParameters(m_monitor_base, &p);
}

/**
 * Loads saved server states. Should only be called once at the beginning of the main loop after server
 * creation.
 */
void MariaDBMonitor::load_journal()
{
    MXS_MONITORED_SERVER* master_output = NULL;
    load_server_journal(m_monitor_base, &master_output);
    m_master = master_output ? get_server_info(master_output) : NULL;
}

/**
 * Start the monitor instance and return the instance data. This function creates a thread to
 * execute the monitoring. Use stopMonitor() to stop the thread.
 *
 * @param monitor General monitor data
 * @param params Configuration parameters
 * @return A pointer to MariaDBMonitor specific data. Should be stored in MXS_MONITOR's "handle"-field.
 */
static void* startMonitor(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    return MariaDBMonitor::start(monitor, params);
}

/**
 * Stop a running monitor
 *
 * @param mon  The monitor that should be stopped.
 */
static void stopMonitor(MXS_MONITOR *mon)
{
    auto handle = static_cast<MariaDBMonitor*>(mon->handle);
    handle->stop();
}

/**
 * Daignostic interface
 *
 * @param dcb   DCB to print diagnostics
 * @param arg   The monitor handle
 */
static void diagnostics(DCB *dcb, const MXS_MONITOR *mon)
{
    const MariaDBMonitor* handle = static_cast<const MariaDBMonitor*>(mon->handle);
    handle->diagnostics(dcb);
}

/**
 * Diagnostic interface
 *
 * @param arg   The monitor handle
 */
static json_t* diagnostics_json(const MXS_MONITOR *mon)
{
    const MariaDBMonitor *handle = (const MariaDBMonitor *)mon->handle;
    return handle->diagnostics_json();
}

/**
 * Command handler for 'switchover'
 *
 * @param args    The provided arguments.
 * @param output  Pointer where to place output object.
 *
 * @return True, if the command was executed, false otherwise.
 */
bool handle_manual_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    ss_dassert((args->argc >= 1) && (args->argc <= 3));
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    ss_dassert((args->argc < 2) || (MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER));
    ss_dassert((args->argc < 3) || (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER));

    bool rval = false;
    if (config_get_global_options()->passive)
    {
        const char MSG[] = "Switchover requested but not performed, as MaxScale is in passive mode.";
        PRINT_MXS_JSON_ERROR(error_out, MSG);
    }
    else
    {
        MXS_MONITOR* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon->handle);
        SERVER* new_master = (args->argc >= 2) ? args->argv[1].value.server : NULL;
        SERVER* current_master = (args->argc == 3) ? args->argv[2].value.server : NULL;
        rval = handle->manual_switchover(new_master, current_master, error_out);
    }
    return rval;
}

/**
 * Command handler for 'failover'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool handle_manual_failover(const MODULECMD_ARG* args, json_t** output)
{
    ss_dassert(args->argc == 1);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    bool rv = false;

    if (config_get_global_options()->passive)
    {
        PRINT_MXS_JSON_ERROR(output, "Failover requested but not performed, as MaxScale is in passive mode.");
    }
    else
    {
        MXS_MONITOR* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon->handle);
        rv = handle->manual_failover(output);
    }
    return rv;
}

/**
 * Command handler for 'rejoin'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool handle_manual_rejoin(const MODULECMD_ARG* args, json_t** output)
{
    ss_dassert(args->argc == 2);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    bool rv = false;
    if (config_get_global_options()->passive)
    {
        PRINT_MXS_JSON_ERROR(output, "Rejoin requested but not performed, as MaxScale is in passive mode.");
    }
    else
    {
        MXS_MONITOR* mon = args->argv[0].value.monitor;
        SERVER* server = args->argv[1].value.server;
        auto handle = static_cast<MariaDBMonitor*>(mon->handle);
        rv = handle->manual_rejoin(server, output);
    }
    return rv;
}

string monitored_servers_to_string(const ServerArray& servers)
{
    string rval;
    size_t array_size = servers.size();
    if (array_size > 0)
    {
        const char* separator = "";
        for (size_t i = 0; i < array_size; i++)
        {
            rval += separator;
            rval += servers[i]->name();
            separator = ",";
        }
    }
    return rval;
}

string get_connection_errors(const ServerArray& servers)
{
    // Get errors from all connections, form a string.
    string rval;
    string separator;
    for (auto iter = servers.begin(); iter != servers.end(); iter++)
    {
        const char* error = mysql_error((*iter)->server_base->con);
        ss_dassert(*error); // Every connection should have an error.
        rval += separator + (*iter)->name() + ": '" + error + "'";
        separator = ", ";
    }
    return rval;
}

MXS_BEGIN_DECLS
/**
 * The module entry point routine. This routine populates the module object structure.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MariaDB Monitor module.");
    static const char ARG_MONITOR_DESC[] = "Monitor name (from configuration file)";
    static modulecmd_arg_type_t switchover_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            ARG_MONITOR_DESC
        },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "New master (optional)" },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Current master (optional)" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "switchover", MODULECMD_TYPE_ACTIVE,
                               handle_manual_switchover, MXS_ARRAY_NELEMS(switchover_argv),
                               switchover_argv, "Perform master switchover");

    static modulecmd_arg_type_t failover_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            ARG_MONITOR_DESC
        },
    };

    modulecmd_register_command(MXS_MODULE_NAME, "failover", MODULECMD_TYPE_ACTIVE,
                               handle_manual_failover, MXS_ARRAY_NELEMS(failover_argv),
                               failover_argv, "Perform master failover");

    static modulecmd_arg_type_t rejoin_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            ARG_MONITOR_DESC
        },
        { MODULECMD_ARG_SERVER, "Joining server" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "rejoin", MODULECMD_TYPE_ACTIVE,
                               handle_manual_rejoin, MXS_ARRAY_NELEMS(rejoin_argv),
                               rejoin_argv, "Rejoin server to a cluster");

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
        "A MariaDB Master/Slave replication monitor",
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
            {"detect_standalone_master", MXS_MODULE_PARAM_BOOL, "true"},
            {CN_FAILCOUNT, MXS_MODULE_PARAM_COUNT, "5"},
            {"allow_cluster_recovery", MXS_MODULE_PARAM_BOOL, "true"},
            {"ignore_external_masters", MXS_MODULE_PARAM_BOOL, "false"},
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
            {CN_AUTO_FAILOVER, MXS_MODULE_PARAM_BOOL, "false"},
            {CN_FAILOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, "90"},
            {CN_SWITCHOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, "90"},
            {CN_REPLICATION_USER, MXS_MODULE_PARAM_STRING},
            {CN_REPLICATION_PASSWORD, MXS_MODULE_PARAM_STRING},
            {CN_VERIFY_MASTER_FAILURE, MXS_MODULE_PARAM_BOOL, "true"},
            {CN_MASTER_FAILURE_TIMEOUT, MXS_MODULE_PARAM_COUNT, "10"},
            {CN_AUTO_REJOIN, MXS_MODULE_PARAM_BOOL, "false"},
            {CN_NO_PROMOTE_SERVERS, MXS_MODULE_PARAM_SERVERLIST},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &info;
}

MXS_END_DECLS
