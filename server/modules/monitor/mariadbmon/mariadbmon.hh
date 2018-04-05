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
#include <string>
#include <tr1/unordered_map>
#include <vector>

#include <maxscale/json_api.h>
#include <maxscale/monitor.h>
#include <maxscale/thread.h>

#include "mariadbserver.hh"
#include "utilities.hh"

using std::string;

extern const int PORT_UNKNOWN;
extern const char * const CN_AUTO_FAILOVER;

class MariaDBMonitor;

// Map of base struct to MariaDBServer. Does not own the server objects. May not be needed at the end.
typedef std::tr1::unordered_map<MXS_MONITORED_SERVER*, MariaDBServer*> ServerInfoMap;
// Server container, owns the server objects.
typedef std::vector<MariaDBServer> ServerContainer; // TODO: Rename/get rid of ServerVector typedef!

enum slave_down_setting_t
{
    ACCEPT_DOWN,
    REJECT_DOWN
};

// TODO: Most of following should be class methods
void print_redirect_errors(MXS_MONITORED_SERVER* first_server, const ServerVector& servers, json_t** err_out);
MXS_MONITORED_SERVER* getServerByNodeId(MXS_MONITORED_SERVER *, long);
MXS_MONITORED_SERVER* getSlaveOfNodeId(MXS_MONITORED_SERVER *, long, slave_down_setting_t);

void find_graph_cycles(MariaDBMonitor *handle, MXS_MONITORED_SERVER *database, int nservers);

// MariaDB Monitor instance data
class MariaDBMonitor
{
private:
    MariaDBMonitor(const MariaDBMonitor&);
    MariaDBMonitor& operator = (const MariaDBMonitor&);
public:
    // TODO: Once done refactoring, see which of these can be moved to private.

    /**
     * Print diagnostics.
     *
     * @param dcb DCB to print to
     */
    void diagnostics(DCB *dcb) const;

    /**
     * Print diagnostics to json object.
     *
     * @return Diagnostics messages
     */
    json_t* diagnostics_json() const;

    /**
     * Runs the main monitor loop. Called from the static monitorMain()-function.
     */
    void main_loop();

    /**
     * Start the monitor instance and return the instance data, creating it if starting for the first time.
     * This function creates a thread to execute the monitoring.
     *
     * @param monitor General monitor data
     * @param params Configuration parameters
     * @return A pointer to MariaDBMonitor specific data.
     */
    static MariaDBMonitor* start(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params);

    /**
     * Stop the monitor. Waits until monitor has stopped.
     */
    bool stop();

    /**
     * Handle switchover
     *
     * @new_master      The specified new master
     * @current_master  The specified current master. If NULL, monitor will autoselect.
     * @output          Pointer where to place output object
     *
     * @return True, if switchover was performed, false otherwise.
     */
    bool manual_switchover(MXS_MONITORED_SERVER* new_master, MXS_MONITORED_SERVER* current_master,
                           json_t** error_out);

    /**
     * Perform user-activated failover.
     *
     * @param output  Json error output
     * @return True on success
     */
    bool manual_failover(json_t** output);

    /**
     * Perform user-activated rejoin
     *
     * @param rejoin_server     Server to join
     * @param output            Json error output
     * @return True on success
     */
    bool manual_rejoin(SERVER* rejoin_server, json_t** output);

    /**
     * Check if server is using gtid replication.
     *
     * @param mon_server Server to check
     * @param error_out Error output
     * @return True if using gtid-replication. False if not, or if server is not a slave or otherwise does
     * not have a gtid_IO_Pos.
     */
    bool uses_gtid(MXS_MONITORED_SERVER* mon_server, json_t** error_out);

    /**
     * Get monitor-specific server info for the monitored server.
     *
     * @param handle
     * @param db Server to get info for. Must be a valid server or function crashes.
     * @return The server info.
     */
    MariaDBServer* get_server_info(MXS_MONITORED_SERVER* db);

    /**
     * Constant version of get_server_info().
     */
    const MariaDBServer* get_server_info(const MXS_MONITORED_SERVER* db) const;

    bool detectStaleMaster;          /**< Monitor flag for MySQL replication Stale Master detection */

private:
    MXS_MONITOR* m_monitor_base;     /**< Generic monitor object */
    THREAD m_thread;                 /**< Monitor thread */
    unsigned long m_id;              /**< Monitor ID */
    volatile int m_shutdown;         /**< Flag to shutdown the monitor thread. */
    volatile int m_status;           /**< Monitor status.  */
    ServerContainer m_servers;       /**< Servers of the monitor. */
    ServerInfoMap m_server_info;     /**< Contains server specific information */

    // Values updated by monitor
    int64_t m_master_gtid_domain;    /**< Gtid domain currently used by the master */
    string m_external_master_host;   /**< External master host, for fail/switchover */
    int m_external_master_port;      /**< External master port */
    MXS_MONITORED_SERVER *m_master;  /**< Master server for MySQL Master/Slave replication */

    // Replication topology detection settings
    bool m_mysql51_replication;      /**< Use MySQL 5.1 replication */
    bool m_detect_stale_slave;       /**< Monitor flag for MySQL replication Stale Slave detection */
    bool m_detect_multimaster;       /**< Detect and handle multi-master topologies */
    bool m_ignore_external_masters;  /**< Ignore masters outside of the monitor configuration */
    bool m_detect_standalone_master; /**< If standalone master are detected */
    bool m_allow_cluster_recovery;   /**< Allow failed servers to rejoin the cluster */
    bool m_warn_set_standalone_master; /**< Log a warning when setting standalone master */

    // Failover, switchover and rejoin settings
    string m_replication_user;       /**< Replication user for CHANGE MASTER TO-commands */
    string m_replication_password;   /**< Replication password for CHANGE MASTER TO-commands */
    int m_failcount;                 /**< How many monitoring cycles master must be down before auto-failover
                                      *   begins */
    uint32_t m_failover_timeout;     /**< Timeout in seconds for the master failover */
    uint32_t m_switchover_timeout;   /**< Timeout in seconds for the master switchover */
    bool m_verify_master_failure;    /**< Whether master failure is verified via slaves */
    int m_master_failure_timeout;    /**< Master failure verification (via slaves) time in seconds */
    bool m_auto_failover;            /**< If automatic master failover is enabled */
    bool m_auto_rejoin;              /**< Attempt to start slave replication on standalone servers or servers
                                      *   replicating from the wrong master automatically. */
    ServerVector m_excluded_servers; /**< Servers banned for master promotion during auto-failover. */

    // Other settings
    string m_script;                 /**< Script to call when state changes occur on servers */
    uint64_t m_events;               /**< enabled events */
    bool m_detect_replication_lag;   /**< Monitor flag for MySQL replication heartbeat */

    MariaDBMonitor(MXS_MONITOR* monitor_base);
    ~MariaDBMonitor();
    bool load_config_params(const MXS_CONFIG_PARAMETER* params);
    bool failover_wait_relay_log(MXS_MONITORED_SERVER* new_master, int seconds_remaining, json_t** err_out);
    bool switchover_demote_master(MXS_MONITORED_SERVER* current_master, MariaDBServer* info,
                                  json_t** err_out);
    bool switchover_wait_slaves_catchup(const ServerVector& slaves, const GtidList& gtid, int total_timeout,
                                        int read_timeout, json_t** err_out);
    bool wait_cluster_stabilization(MXS_MONITORED_SERVER* new_master, const ServerVector& slaves,
                                    int seconds_remaining);
    bool switchover_check_preferred_master(MXS_MONITORED_SERVER* preferred, json_t** err_out);
    bool promote_new_master(MXS_MONITORED_SERVER* new_master, json_t** err_out);
    MXS_MONITORED_SERVER* select_new_master(ServerVector* slaves_out, json_t** err_out);
    bool server_is_excluded(const MXS_MONITORED_SERVER* server);
    bool is_candidate_better(const MariaDBServer* current_best_info, const MariaDBServer* candidate_info,
                             uint32_t gtid_domain);
    MariaDBServer* update_slave_info(MXS_MONITORED_SERVER* server);
    void init_server_info();
    bool slave_receiving_events();
    void monitor_database(MariaDBServer* param_db);
    bool standalone_master_required(MXS_MONITORED_SERVER *db);
    bool set_standalone_master(MXS_MONITORED_SERVER *db);
    bool failover_not_possible();
    string generate_change_master_cmd(const string& master_host, int master_port);
    int redirect_slaves(MXS_MONITORED_SERVER* new_master, const ServerVector& slaves,
                        ServerVector* redirected_slaves);
    bool set_replication_credentials(const MXS_CONFIG_PARAMETER* params);
    bool start_external_replication(MXS_MONITORED_SERVER* new_master, json_t** err_out);
    bool switchover_start_slave(MXS_MONITORED_SERVER* old_master, SERVER* new_master);
    bool redirect_one_slave(MXS_MONITORED_SERVER* slave, const char* change_cmd);
    bool get_joinable_servers(ServerVector* output);
    bool join_cluster(MXS_MONITORED_SERVER* server, const char* change_cmd);
    void set_master_heartbeat(MXS_MONITORED_SERVER *);
    void set_slave_heartbeat(MXS_MONITORED_SERVER *);
    MXS_MONITORED_SERVER* build_mysql51_replication_tree();
    MXS_MONITORED_SERVER* get_replication_tree(int num_servers);
    void monitor_mysql_db(MariaDBServer *serv_info);
    bool do_switchover(MXS_MONITORED_SERVER* current_master, MXS_MONITORED_SERVER* new_master,
                       json_t** err_out);
    bool do_failover(json_t** err_out);
    uint32_t do_rejoin(const ServerVector& joinable_servers);
    bool mon_process_failover(bool* cluster_modified_out);
    bool server_is_rejoin_suspect(MXS_MONITORED_SERVER* server, MariaDBServer* master_info,
                                  json_t** output);
    bool cluster_can_be_joined();
    bool failover_check(json_t** error_out);
    void disable_setting(const char* setting);
    bool switchover_check_new(const MXS_MONITORED_SERVER* monitored_server, json_t** error);
    bool switchover_check_current(const MXS_MONITORED_SERVER* suggested_curr_master,
                                  json_t** error_out) const;
    bool can_replicate_from(MXS_MONITORED_SERVER* slave, MariaDBServer* slave_info,
                            MariaDBServer* master_info);
    void monitor_one_server(MariaDBServer& server);
    void find_root_master(MXS_MONITORED_SERVER** root_master);
    void update_gtid_domain();
    void update_external_master();
    void assign_relay_master(MariaDBServer& serv_info);
    void update_server_states(MariaDBServer& db_server, MXS_MONITORED_SERVER* root_master,
                              bool detect_stale_master);
    void log_master_changes(MXS_MONITORED_SERVER* root_master, int* log_no_master);
    void handle_auto_failover(bool* failover_performed);
    void measure_replication_lag(MXS_MONITORED_SERVER* root_master);
    void handle_auto_rejoin();
};
