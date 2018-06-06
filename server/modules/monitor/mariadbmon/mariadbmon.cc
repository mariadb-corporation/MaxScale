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
// TODO: For monitor_add_parameters
#include "../../../core/internal/monitor.h"

using std::string;

// Config parameter names
const char * const CN_AUTO_FAILOVER       = "auto_failover";
const char * const CN_PROMOTION_SQL_FILE  = "promotion_sql_file";
const char * const CN_DEMOTION_SQL_FILE   = "demotion_sql_file";

static const char CN_AUTO_REJOIN[]        = "auto_rejoin";
static const char CN_FAILCOUNT[]          = "failcount";
static const char CN_ENFORCE_READONLY[]   = "enforce_read_only_slaves";
static const char CN_NO_PROMOTE_SERVERS[] = "servers_no_promotion";
static const char CN_FAILOVER_TIMEOUT[]   = "failover_timeout";
static const char CN_SWITCHOVER_TIMEOUT[] = "switchover_timeout";

// Parameters for master failure verification and timeout
static const char CN_VERIFY_MASTER_FAILURE[]    = "verify_master_failure";
static const char CN_MASTER_FAILURE_TIMEOUT[]   = "master_failure_timeout";
// Replication credentials parameters for failover/switchover/join
static const char CN_REPLICATION_USER[]     = "replication_user";
static const char CN_REPLICATION_PASSWORD[] = "replication_password";

MariaDBMonitor::MariaDBMonitor(MXS_MONITOR* monitor)
    : maxscale::MonitorInstance(monitor)
    , m_id(config_get_global_options()->id)
    , m_master_gtid_domain(GTID_DOMAIN_UNKNOWN)
    , m_external_master_port(PORT_UNKNOWN)
    , m_cluster_modified(true)
    , m_warn_set_standalone_master(true)
{}

MariaDBMonitor::~MariaDBMonitor()
{
    clear_server_info();
}

/**
 * Reset and initialize server arrays and related data.
 */
void MariaDBMonitor::reset_server_info()
{
    // If this monitor is being restarted, the server data needs to be freed.
    clear_server_info();

    // Next, initialize the data.
    for (auto mon_server = m_monitor->monitored_servers; mon_server; mon_server = mon_server->next)
    {
        m_servers.push_back(new MariaDBServer(mon_server));
    }
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        auto mon_server = (*iter)->m_server_base;
        ss_dassert(m_server_info.count(mon_server) == 0);
        ServerInfoMap::value_type new_val(mon_server, *iter);
        m_server_info.insert(new_val);
    }
}

void MariaDBMonitor::clear_server_info()
{
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        delete *iter;
    }
    // All MariaDBServer*:s are now invalid, as well as any dependant data.
    m_servers.clear();
    m_server_info.clear();
    m_servers_by_id.clear();
    m_excluded_servers.clear();
    m_master = NULL;
    m_master_gtid_domain = GTID_DOMAIN_UNKNOWN;
    m_external_master_host.clear();
    m_external_master_port = PORT_UNKNOWN;
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

MariaDBServer* MariaDBMonitor::get_server(int64_t id)
{
    auto found = m_servers_by_id.find(id);
    return (found != m_servers_by_id.end()) ? (*found).second : NULL;
}

bool MariaDBMonitor::set_replication_credentials(const MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    string repl_user = config_get_string(params, CN_REPLICATION_USER);
    string repl_pw = config_get_string(params, CN_REPLICATION_PASSWORD);

    if (repl_user.empty() && repl_pw.empty())
    {
        // No replication credentials defined, use monitor credentials
        repl_user = m_monitor->user;
        repl_pw = m_monitor->password;
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

MariaDBMonitor* MariaDBMonitor::create(MXS_MONITOR *monitor)
{
    return new MariaDBMonitor(monitor);
}

/**
 * Load config parameters
 *
 * @param params Config parameters
 * @return True if settings are ok
 */
bool MariaDBMonitor::configure(const MXS_CONFIG_PARAMETER* params)
{
    /* Reset all monitored state info. The server dependent values must be reset as servers could have been
     * added, removed and modified. */
    reset_server_info();

    m_detect_stale_master = config_get_bool(params, "detect_stale_master");
    m_detect_stale_slave = config_get_bool(params, "detect_stale_slave");
    m_detect_replication_lag = config_get_bool(params, "detect_replication_lag");
    m_detect_multimaster = config_get_bool(params, "multimaster");
    m_ignore_external_masters = config_get_bool(params, "ignore_external_masters");
    m_detect_standalone_master = config_get_bool(params, "detect_standalone_master");
    m_failcount = config_get_integer(params, CN_FAILCOUNT);
    m_allow_cluster_recovery = config_get_bool(params, "allow_cluster_recovery");
    m_script = config_get_string(params, "script");
    m_events = config_get_enum(params, "events", mxs_monitor_event_enum_values);
    m_failover_timeout = config_get_integer(params, CN_FAILOVER_TIMEOUT);
    m_switchover_timeout = config_get_integer(params, CN_SWITCHOVER_TIMEOUT);
    m_auto_failover = config_get_bool(params, CN_AUTO_FAILOVER);
    m_auto_rejoin = config_get_bool(params, CN_AUTO_REJOIN);
    m_enforce_read_only_slaves = config_get_bool(params, CN_ENFORCE_READONLY);
    m_verify_master_failure = config_get_bool(params, CN_VERIFY_MASTER_FAILURE);
    m_master_failure_timeout = config_get_integer(params, CN_MASTER_FAILURE_TIMEOUT);
    m_promote_sql_file = config_get_string(params, CN_PROMOTION_SQL_FILE);
    m_demote_sql_file = config_get_string(params, CN_DEMOTION_SQL_FILE);

    m_excluded_servers.clear();
    MXS_MONITORED_SERVER** excluded_array = NULL;
    int n_excluded = mon_config_get_servers(params, CN_NO_PROMOTE_SERVERS, m_monitor, &excluded_array);
    for (int i = 0; i < n_excluded; i++)
    {
        m_excluded_servers.push_back(get_server_info(excluded_array[i]));
    }
    MXS_FREE(excluded_array);

    bool settings_ok = true;
    if (!check_sql_files())
    {
        settings_ok = false;
    }
    if (!set_replication_credentials(params))
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", CN_REPLICATION_USER, CN_REPLICATION_PASSWORD);
        settings_ok = false;
    }
    return settings_ok;
}

void MariaDBMonitor::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Automatic failover:     %s\n", m_auto_failover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Failcount:              %d\n", m_failcount);
    dcb_printf(dcb, "Failover timeout:       %u\n", m_failover_timeout);
    dcb_printf(dcb, "Switchover timeout:     %u\n", m_switchover_timeout);
    dcb_printf(dcb, "Automatic rejoin:       %s\n", m_auto_rejoin ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Enforce read-only:      %s\n", m_enforce_read_only_slaves ?
               "Enabled" : "Disabled");
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
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        string server_info = (*iter)->diagnostics(m_detect_multimaster) + "\n";
        dcb_printf(dcb, "%s", server_info.c_str());
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
    json_object_set_new(rval, CN_AUTO_FAILOVER, json_boolean(m_auto_failover));
    json_object_set_new(rval, CN_FAILOVER_TIMEOUT, json_integer(m_failover_timeout));
    json_object_set_new(rval, CN_SWITCHOVER_TIMEOUT, json_integer(m_switchover_timeout));
    json_object_set_new(rval, CN_AUTO_REJOIN, json_boolean(m_auto_rejoin));
    json_object_set_new(rval, CN_ENFORCE_READONLY, json_boolean(m_enforce_read_only_slaves));

    if (!m_script.empty())
    {
        json_object_set_new(rval, "script", json_string(m_script.c_str()));
    }
    if (m_excluded_servers.size() > 0)
    {
        string list = monitored_servers_to_string(m_excluded_servers);
        json_object_set_new(rval, CN_NO_PROMOTE_SERVERS, json_string(list.c_str()));
    }

    if (!m_servers.empty())
    {
        json_t* arr = json_array();
        for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
        {
            json_array_append_new(arr, (*iter)->diagnostics_json(m_detect_multimaster));
        }
        json_object_set_new(rval, "server_info", arr);
    }

    return rval;
}

void MariaDBMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    auto i = m_server_info.find(monitored_server);
    ss_dassert(i != m_server_info.end());

    (*i).second->update_server(m_monitor);
}

void MariaDBMonitor::main()
{
    MariaDBServer* root_master = NULL;
    int log_no_master = 1;

    load_journal();

    if (m_detect_replication_lag)
    {
        check_maxscale_schema_replication();
    }

    /* Check monitor permissions. Failure won't cause the monitor to stop. Afterwards, close connections so
     * that update_server() reconnects and checks server version. TODO: check permissions when checking
     * server version. */
    check_monitor_permissions(m_monitor, "SHOW SLAVE STATUS");
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        mysql_close((*iter)->m_server_base->con);
        (*iter)->m_server_base->con = NULL;
    }

    while (!should_shutdown())
    {
        atomic_add_uint64(&m_monitor->ticks, 1);
        timespec loop_start;
        /* Coarse time has resolution ~1ms (as opposed to 1ns) but this is enough. */
        clock_gettime(CLOCK_MONOTONIC_COARSE, &loop_start);

        /* Read any admin status changes from SERVER->status_pending. */
        monitor_check_maintenance_requests(m_monitor);
        /* Update MXS_MONITORED_SERVER->pending_status. This is where the monitor loop writes it's findings.
         * Also, backup current status so that it can be compared to any deduced state. */
        for (auto mon_srv = m_monitor->monitored_servers; mon_srv; mon_srv = mon_srv->next)
        {
            auto status = mon_srv->server->status;
            mon_srv->pending_status = status;
            mon_srv->mon_prev_status = status;
        }

        // Query all servers for their status. Update the server id array.
        m_servers_by_id.clear();
        for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
        {
            MariaDBServer* server = *iter;
            update_server_status(server->m_server_base);

            if (server->m_server_id != SERVER_ID_UNKNOWN)
            {
                IdToServerMap::value_type new_val(server->m_server_id, server);
                m_servers_by_id.insert(new_val);
            }
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
            if ((*iter)->m_version == MariaDBServer::version::BINLOG_ROUTER)
            {
                monitor_clear_pending_status((*iter)->m_server_base, SERVER_SLAVE);
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
            if (standalone_master_required())
            {
                // Other servers have died, set last remaining server as master
                if (set_standalone_master())
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
            // Clear slave and stale slave status bits from current master
            root_master->clear_status(SERVER_SLAVE | SERVER_WAS_SLAVE);

            /**
             * Clear external slave status from master if configured to do so.
             * This allows parts of a multi-tiered replication setup to be used
             * in MaxScale.
             */
            if (m_ignore_external_masters)
            {
                root_master->clear_status(SERVER_SLAVE_OF_EXT_MASTER);
            }
        }

        ss_dassert(root_master == NULL || root_master == m_master);
        ss_dassert(root_master == NULL ||
                   ((root_master->m_server_base->pending_status & (SERVER_SLAVE | SERVER_MASTER)) !=
                    (SERVER_SLAVE | SERVER_MASTER)));

        /* Generate the replication heartbeat event by performing an update */
        if (m_detect_replication_lag && root_master &&
            (root_master->is_master() || root_master->is_relay_server()))
        {
            measure_replication_lag(root_master);
        }

        // Update shared status. The next functions read the shared status. TODO: change the following
        // functions to read "pending_status" instead.
        for (auto mon_srv = m_monitor->monitored_servers; mon_srv; mon_srv = mon_srv->next)
        {
            mon_srv->server->status = mon_srv->pending_status;
        }

        /* Check if monitor events need to be launched. */
        mon_process_state_changes(m_monitor, m_script.c_str(), m_events);
        m_cluster_modified = false;
        if (m_auto_failover)
        {
            m_cluster_modified = handle_auto_failover();
        }

         /* log master detection failure of first master becomes available after failure */
        log_master_changes(root_master, &log_no_master);

        // Do not auto-join servers on this monitor loop if a failover (or any other cluster modification)
        // has been performed, as server states have not been updated yet. It will happen next iteration.
        if (!config_get_global_options()->passive && m_auto_rejoin && !m_cluster_modified &&
            cluster_can_be_joined())
        {
            // Check if any servers should be autojoined to the cluster and try to join them.
            handle_auto_rejoin();
        }

        /* Check if any slave servers have read-only off and turn it on if user so wishes. Again, do not
         * perform this if cluster has been modified this loop since it may not be clear which server
         * should be a slave. */
        if (!config_get_global_options()->passive && m_enforce_read_only_slaves && !m_cluster_modified)
        {
            enforce_read_only_on_slaves();
        }

        mon_hangup_failed_servers(m_monitor);
        store_server_journal(m_monitor, m_master ? m_master->m_server_base : NULL);

        // Check how much the monitor should sleep to get one full monitor interval.
        timespec loop_end;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &loop_end);
        int64_t time_elapsed_ms = (loop_end.tv_sec - loop_start.tv_sec) * 1000 +
                                  (loop_end.tv_nsec - loop_start.tv_nsec) / 1000000;
        int64_t sleep_time_remaining = m_monitor->interval - time_elapsed_ms;
        // Sleep at least one full interval.
        sleep_time_remaining = MXS_MAX(MXS_MON_BASE_INTERVAL_MS, sleep_time_remaining);
        /* Sleep in small increments to react faster to some events. This should ideally use some type of
         * notification mechanism. */
        while (sleep_time_remaining > 0 && !should_shutdown() &&
               atomic_load_int(&m_monitor->check_maintenance_flag) == MAINTENANCE_FLAG_NOCHECK)
        {
            int small_sleep_ms = (sleep_time_remaining >= MXS_MON_BASE_INTERVAL_MS) ?
                MXS_MON_BASE_INTERVAL_MS : sleep_time_remaining;
            thread_millisleep(small_sleep_ms);
            sleep_time_remaining -= small_sleep_ms;
        }
    }
}

void MariaDBMonitor::update_gtid_domain()
{
    int64_t domain = m_master->m_gtid_domain_id;
    if (m_master_gtid_domain != GTID_DOMAIN_UNKNOWN && domain != m_master_gtid_domain)
    {
        MXS_NOTICE("Gtid domain id of master has changed: %" PRId64 " -> %" PRId64 ".",
                   m_master_gtid_domain, domain);
    }
    m_master_gtid_domain = domain;
}

void MariaDBMonitor::update_external_master()
{
    if (SERVER_IS_SLAVE_OF_EXT_MASTER(m_master->m_server_base->server))
    {
        ss_dassert(!m_master->m_slave_status.empty());
        if (m_master->m_slave_status[0].master_host != m_external_master_host ||
            m_master->m_slave_status[0].master_port != m_external_master_port)
        {
            const string new_ext_host =  m_master->m_slave_status[0].master_host;
            const int new_ext_port = m_master->m_slave_status[0].master_port;
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

void MariaDBMonitor::measure_replication_lag(MariaDBServer* root_master)
{
    ss_dassert(root_master);
    MXS_MONITORED_SERVER* mon_root_master = root_master->m_server_base;
    set_master_heartbeat(root_master);
    for (auto iter = m_servers.begin(); iter != m_servers.end(); iter++)
    {
        MariaDBServer* server = *iter;
        MXS_MONITORED_SERVER* ptr = server->m_server_base;
        if ((!server->is_in_maintenance()) && server->is_running())
        {
            if (ptr->server->node_id != mon_root_master->server->node_id &&
                (server->is_slave() || server->is_relay_server()) &&
                (server->m_version == MariaDBServer::version::MARIADB_MYSQL_55 ||
                 server->m_version == MariaDBServer::version::MARIADB_100)) // No select lag for Binlog Server
            {
                set_slave_heartbeat(server);
            }
        }
    }
}

void MariaDBMonitor::log_master_changes(MariaDBServer* root_master_server, int* log_no_master)
{
    MXS_MONITORED_SERVER* root_master = root_master_server ? root_master_server->m_server_base : NULL;
    if (root_master && mon_status_changed(root_master) &&
        !(root_master->pending_status & SERVER_WAS_MASTER))
    {
        if ((root_master->pending_status & SERVER_MASTER) && root_master_server->is_running())
        {
            if (!(root_master->mon_prev_status & SERVER_WAS_MASTER) &&
                !(root_master->pending_status & SERVER_MAINT))
            {
                MXS_NOTICE("A Master Server is now available: %s:%i",
                           root_master->server->address,
                           root_master->server->port);
            }
        }
        else
        {
            MXS_ERROR("No Master can be determined. Last known was %s:%i",
                      root_master->server->address,
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
        uint32_t joins = do_rejoin(joinable_servers, NULL);
        if (joins > 0)
        {
            MXS_NOTICE("%d server(s) redirected or rejoined the cluster.", joins);
            m_cluster_modified = true;
        }
        if (joins < joinable_servers.size())
        {
            MXS_ERROR("A cluster join operation failed, disabling automatic rejoining. "
                      "To re-enable, manually set '%s' to 'true' for monitor '%s' via MaxAdmin or "
                      "the REST API.", CN_AUTO_REJOIN, m_monitor->name);
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
 * @param server      The server to write the heartbeat to
 */
void MariaDBMonitor::set_master_heartbeat(MariaDBServer* server)
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

    MXS_MONITORED_SERVER* database = server->m_server_base;
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
            heartbeat, m_master->m_server_base->server->node_id, m_id);

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
                    m_master->m_server_base->server->node_id, m_id, heartbeat);

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
                          database->server->address, database->server->port);
            }
        }
        else
        {
            /* Set replication lag as 0 for the master */
            database->server->rlag = 0;

            MXS_DEBUG("heartbeat table updated for Master %s:%i",
                      database->server->address, database->server->port);
        }
    }
}

/*
 * This function gets the replication heartbeat from the maxscale_schema.replication_heartbeat table in
 * the current slave and stores the timestamp and replication lag in the slave server struct.
 *
 * @param server      The slave to measure lag at
 */
void MariaDBMonitor::set_slave_heartbeat(MariaDBServer* server)
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
            m_id, m_master->m_server_base->server->node_id);

    MXS_MONITORED_SERVER* database = server->m_server_base;
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
                database->server->rlag = ((unsigned int)rlag > (m_monitor->interval / 1000)) ? rlag : 0;
            }
            else
            {
                database->server->rlag = MAX_RLAG_NOT_AVAILABLE;
            }

            MXS_DEBUG("Slave %s:%i has %i seconds lag",
                      database->server->address,
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

        if (m_master->m_server_base->server->node_id < 0)
        {
            MXS_ERROR("error: replication heartbeat: "
                      "master_server_id NOT available for %s:%i",
                      database->server->address,
                      database->server->port);
        }
        else
        {
            MXS_ERROR("error: replication heartbeat: "
                      "failed selecting from hearthbeat table of %s:%i : [%s], %s",
                      database->server->address,
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
    monitor_add_parameters(m_monitor, &p);
}

/**
 * Loads saved server states. Should only be called once at the beginning of the main loop after server
 * creation.
 */
void MariaDBMonitor::load_journal()
{
    MXS_MONITORED_SERVER* master_output = NULL;
    load_server_journal(m_monitor, &master_output);
    m_master = master_output ? get_server_info(master_output) : NULL;
}

/**
 * Check sql text file parameters. A parameter should either be empty or a valid file which can be opened.
 *
 * @return True if no errors occurred when opening the files
 */
bool MariaDBMonitor::check_sql_files()
{
    const char ERRMSG[] = "%s ('%s') does not exist or cannot be accessed for reading: '%s'.";

    bool rval = true;
    if (!m_promote_sql_file.empty() && access(m_promote_sql_file.c_str(), R_OK) != 0)
    {
        rval = false;
        MXS_ERROR(ERRMSG, CN_PROMOTION_SQL_FILE, m_promote_sql_file.c_str(), mxs_strerror(errno));
    }

    if (!m_demote_sql_file.empty() && access(m_demote_sql_file.c_str(), R_OK) != 0)
    {
        rval = false;
        MXS_ERROR(ERRMSG, CN_DEMOTION_SQL_FILE, m_demote_sql_file.c_str(), mxs_strerror(errno));
    }
    return rval;
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
        const char* const MSG = "Switchover requested but not performed, as MaxScale is in passive mode.";
        PRINT_MXS_JSON_ERROR(error_out, MSG);
    }
    else
    {
        MXS_MONITOR* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon->instance);
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
        auto handle = static_cast<MariaDBMonitor*>(mon->instance);
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
        auto handle = static_cast<MariaDBMonitor*>(mon->instance);
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
        const char* error = mysql_error((*iter)->m_server_base->con);
        ss_dassert(*error); // Every connection should have an error.
        rval += separator + (*iter)->name() + ": '" + error + "'";
        separator = ", ";
    }
    return rval;
}

/**
 * The module entry point routine. This routine populates the module object structure.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
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

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_GA,
        MXS_MONITOR_VERSION,
        "A MariaDB Master/Slave replication monitor",
        "V1.5.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<MariaDBMonitor>::s_api,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"detect_replication_lag", MXS_MODULE_PARAM_BOOL, "false"},
            {"detect_stale_master", MXS_MODULE_PARAM_BOOL, "true"},
            {"detect_stale_slave",  MXS_MODULE_PARAM_BOOL, "true"},
            {"mysql51_replication", MXS_MODULE_PARAM_BOOL, "false", MXS_MODULE_OPT_DEPRECATED},
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
            {CN_ENFORCE_READONLY, MXS_MODULE_PARAM_BOOL, "false"},
            {CN_NO_PROMOTE_SERVERS, MXS_MODULE_PARAM_SERVERLIST},
            {CN_PROMOTION_SQL_FILE, MXS_MODULE_PARAM_PATH},
            {CN_DEMOTION_SQL_FILE, MXS_MODULE_PARAM_PATH},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &info;
}
