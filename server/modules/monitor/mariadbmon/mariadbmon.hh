#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "mariadbmon_common.hh"
#include <condition_variable>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <maxscale/monitor.hh>
#include <maxscale/thread.h>

#include "mariadbserver.hh"

extern const char * const CN_AUTO_FAILOVER;
extern const char * const CN_SWITCHOVER_ON_LOW_DISK_SPACE;
extern const char * const CN_PROMOTION_SQL_FILE;
extern const char * const CN_DEMOTION_SQL_FILE;

// Map of base struct to MariaDBServer. Does not own the server objects. May not be needed at the end.
typedef std::unordered_map<MXS_MONITORED_SERVER*, MariaDBServer*> ServerInfoMap;
// Map of server id:s to MariaDBServer. Useful when constructing the replication graph.
typedef std::unordered_map<int64_t, MariaDBServer*> IdToServerMap;
// Map of cycle number to cycle members. The elements should be in order for predictability when iterating.
typedef std::map<int, ServerArray> CycleMap;

// MariaDB Monitor instance data
class MariaDBMonitor : public maxscale::MonitorInstance
{
private:
    MariaDBMonitor(const MariaDBMonitor&);
    MariaDBMonitor& operator = (const MariaDBMonitor&);
public:
    // Helper class used for testing
    class Test;
    friend class Test;

    ~MariaDBMonitor();

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
     * Create the monitor instance and return the instance data.
     *
     * @param monitor General monitor data
     * @return A pointer to MariaDBMonitor specific data.
     */
    static MariaDBMonitor* create(MXS_MONITOR *monitor);

    /**
     * Perform user-activated switchover.
     *
     * @param new_master      The specified new master. If NULL, monitor will autoselect.
     * @param current_master  The specified current master. If NULL, monitor will autoselect.
     * @param error_out       Json error output
     * @return True if switchover was performed
     */
    bool run_manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out);

    /**
     * Perform user-activated failover.
     *
     * @param error_out Json error output
     * @return True if failover was performed
     */
    bool run_manual_failover(json_t** error_out);

    /**
     * Perform user-activated rejoin
     *
     * @param rejoin_server Server to join
     * @param error_out Json error output
     * @return True if rejoin was performed
     */
    bool run_manual_rejoin(SERVER* rejoin_server, json_t** error_out);

protected:
    void pre_loop();
    void tick();

    void process_state_changes();

private:

    struct CycleInfo
    {
        int cycle_id = NodeData::CYCLE_NONE;
        ServerArray cycle_members;
    };

    /* Structure used to communicate commands and results between the MaxAdmin and monitor threads.
     * The monitor can only process one manual command at a time, which is already enforced by
     * the admin thread. */
    struct ManualCommand
    {
    public:
        std::mutex mutex;                    /**< Mutex used by the condition variables */
        std::condition_variable has_command; /**< Notified when a command is waiting execution */
        bool command_waiting_exec = false;   /**< Guard variable for the above */
        std::function<void (void)> method;   /**< The method to run when executing the command */
        std::condition_variable has_result;  /**< Notified when the command has ran */
        bool result_waiting = false;         /**< Guard variable for the above */
    };

    unsigned long m_id;                  /**< Monitor ID */
    ServerArray m_servers;               /**< Servers of the monitor */
    ServerInfoMap m_server_info;         /**< Map from server base struct to MariaDBServer */
    ManualCommand m_manual_cmd;          /**< Communicates manual commands and results */

    // Values updated by monitor
    MariaDBServer* m_master;             /**< Master server for Master/Slave replication */
    MariaDBServer* m_next_master;        /**< When master changes because of a failover/switchover, the new
                                           *  master is written here so the next monitor loop picks it up. */
    IdToServerMap m_servers_by_id;       /**< Map from server id:s to MariaDBServer */
    int64_t m_master_gtid_domain;        /**< gtid_domain_id most recently seen on the master  */
    std::string m_external_master_host;  /**< External master host, for fail/switchover */
    int m_external_master_port;          /**< External master port */
    bool m_cluster_topology_changed;     /**< Has cluster topology changed since last monitor loop? */
    bool m_cluster_modified;             /**< Has a failover/switchover/rejoin been performed this loop? */
    CycleMap m_cycles;                   /**< Map from cycle number to cycle member servers */
    CycleInfo m_master_cycle_status;     /**< Info about master server cycle from previous round */

    // Replication topology detection settings
    bool m_detect_replication_lag;       /**< Monitor flag for MySQL replication heartbeat */
    bool m_detect_stale_master;          /**< Monitor flag for MySQL replication Stale Master detection */
    bool m_detect_stale_slave;           /**< Monitor flag for MySQL replication Stale Slave detection */
    bool m_detect_standalone_master;     /**< If standalone master are detected */
    bool m_ignore_external_masters;      /**< Ignore masters outside of the monitor configuration */

    // Failover, switchover and rejoin settings
    bool m_auto_failover;                /**< Is automatic master failover is enabled? */
    bool m_auto_rejoin;                  /**< Is automatic rejoin enabled? */
    int m_failcount;                     /**< Numer of cycles master must be down before auto-failover begins */
    std::string m_replication_user;      /**< Replication user for CHANGE MASTER TO-commands */
    std::string m_replication_password;  /**< Replication password for CHANGE MASTER TO-commands */
    uint32_t m_failover_timeout;         /**< Time limit in seconds for master failover */
    uint32_t m_switchover_timeout;       /**< Time limit in seconds for master switchover */
    bool m_verify_master_failure;        /**< Is master failure is verified via slaves? */
    int m_master_failure_timeout;        /**< Master failure verification (via slaves) time in seconds */
    ServerArray m_excluded_servers;      /**< Servers banned for master promotion during auto-failover or
                                          *   autoselect switchover. */
    std::string m_promote_sql_file;      /**< File with sql commands which are ran to a server being promoted. */
    std::string m_demote_sql_file;       /**< File with sql commands which are ran to a server being demoted. */
    bool m_enforce_read_only_slaves;     /**< Should the monitor set read-only=1 on any slave servers. */
    bool m_switchover_on_low_disk_space; /**< Should the monitor do a switchover on low disk space. */
    bool m_maintenance_on_low_disk_space; /**< Set slave and unreplicating servers with low disk space to
                                           *   maintenance. */

    // Other settings
    bool m_log_no_master;                /**< Should it be logged that there is no master */
    bool m_warn_no_valid_in_cycle;       /**< Log a warning when a replication cycle has no valid master */
    bool m_warn_no_valid_outside_cycle;  /**< Log a warning when a replication topology has no valid master
                                          *   outside of a cycle. */
    bool m_warn_failover_precond;        /**< Print failover preconditions error message? */
    bool m_warn_switchover_precond;      /**< Print switchover preconditions error message? */
    bool m_warn_cannot_rejoin;           /**< Print warning if auto_rejoin fails because of invalid gtid:s? */
    bool m_warn_current_master_invalid;  /**< Print warning if current master is not valid? */
    bool m_warn_have_better_master;      /**< Print warning if the current master is not the best one? */

    // Base methods
    MariaDBMonitor(MXS_MONITOR* monitor_base);
    void reset_server_info();
    void clear_server_info();
    void reset_node_index_info();
    bool configure(const MXS_CONFIG_PARAMETER* params);
    bool set_replication_credentials(const MXS_CONFIG_PARAMETER* params);
    MariaDBServer* get_server_info(MXS_MONITORED_SERVER* db);
    MariaDBServer* get_server(int64_t id);
    MariaDBServer* get_server(SERVER* server);
    bool execute_manual_command(GenericFunction command, json_t** error_out);
    std::string diagnostics_to_string() const;
    json_t* diagnostics_to_json() const;

    // Cluster discovery and status assignment methods
    void update_server(MariaDBServer& server);
    void find_graph_cycles();
    void update_topology();
    void log_master_changes();
    void update_gtid_domain();
    void update_external_master();
    void set_master_heartbeat(MariaDBServer*);
    void set_slave_heartbeat(MariaDBServer*);
    void measure_replication_lag();
    void check_maxscale_schema_replication();
    void build_replication_graph();
    void tarjan_scc_visit_node(MariaDBServer *node, ServerArray* stack, int *index, int *cycle);
    MariaDBServer* find_topology_master_server(std::string* msg_out);
    MariaDBServer* find_best_reach_server(const ServerArray& candidates);
    void calculate_node_reach(MariaDBServer* node);
    MariaDBServer* find_master_inside_cycle(ServerArray& cycle_servers);
    void assign_server_roles();
    void assign_slave_and_relay_master(MariaDBServer* start_node);
    bool master_is_valid(std::string* reason_out);
    bool cycle_has_master_server(ServerArray& cycle_servers);
    void update_master_cycle_info();
    void set_low_disk_slaves_maintenance();
    void assign_new_master(MariaDBServer* new_master);

    // Switchover methods
    bool manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out);
    bool switchover_prepare(SERVER* new_master, SERVER* current_master, Log log_mode,
                            MariaDBServer** new_master_out, MariaDBServer** current_master_out,
                            json_t** error_out);
    bool do_switchover(MariaDBServer* demotion_target, MariaDBServer* promotion_target, json_t** error_out);
    bool switchover_check_preferred_master(MariaDBServer* preferred, json_t** err_out);
    bool switchover_demote_master(MariaDBServer* current_master,
                                  json_t** err_out);
    bool switchover_wait_slaves_catchup(const ServerArray& slaves, const GtidList& gtid, int total_timeout,
                                        json_t** err_out);
    bool switchover_start_slave(MariaDBServer* old_master, MariaDBServer* new_master);
    void handle_low_disk_space_master();

    // Failover methods
    bool manual_failover(json_t** output);
    void handle_auto_failover();
    bool cluster_supports_failover(std::string* reasons_out);
    bool slave_receiving_events();
    bool failover_prepare(Log log_mode, MariaDBServer** promotion_target_out,
                          MariaDBServer** demotion_target_out, json_t** error_out);
    bool do_failover(MariaDBServer* promotion_target, MariaDBServer* demotion_target, json_t** err_out);

    // Rejoin methods
    bool manual_rejoin(SERVER* rejoin_server, json_t** output);
    bool cluster_can_be_joined();
    void handle_auto_rejoin();
    bool get_joinable_servers(ServerArray* output);
    bool server_is_rejoin_suspect(MariaDBServer* rejoin_cand, json_t** output);
    uint32_t do_rejoin(const ServerArray& joinable_servers, json_t** output);

    // Methods common to failover/switchover/rejoin
    MariaDBServer* select_promotion_target(MariaDBServer* current_master,
                                           ClusterOperation op, Log log_mode,
                                           json_t** error_out);
    bool server_is_excluded(const MariaDBServer* server);
    bool is_candidate_better(const MariaDBServer* candidate, const MariaDBServer* current_best,
                             uint32_t gtid_domain, std::string* reason_out = NULL);
    bool promote_new_master(MariaDBServer* new_master, json_t** err_out);
    int redirect_slaves(MariaDBServer* new_master, const ServerArray& slaves,
                        ServerArray* redirected_slaves);
    std::string generate_change_master_cmd(const std::string& master_host, int master_port);
    bool start_external_replication(MariaDBServer* new_master, json_t** err_out);
    bool wait_cluster_stabilization(MariaDBServer* new_master, const ServerArray& slaves,
                                    int seconds_remaining);
    void report_and_disable(const std::string& operation, const std::string& setting_name, bool* setting_var);
    bool check_gtid_replication(Log log_mode, const MariaDBServer* demotion_target, json_t** error_out);
    ServerArray get_redirectables(const MariaDBServer* promotion_target,
                                  const MariaDBServer* demotion_target);

    // Other methods
    void disable_setting(const std::string& setting);
    bool check_sql_files();
    void enforce_read_only_on_slaves();
};

/**
 * Generates a list of server names separated by ', '
 *
 * @param servers The servers
 * @return Server names
 */
std::string monitored_servers_to_string(const ServerArray& servers);

/**
 * Get MariaDB connection error strings from all the given servers, form one string.
 *
 * @param servers Servers with errors
 * @return Concatenated string.
 */
std::string get_connection_errors(const ServerArray& servers);
