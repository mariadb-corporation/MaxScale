/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once
#include "mariadbmon_common.hh"

#include <condition_variable>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <maxbase/stopwatch.hh>
#include <maxscale/monitor.hh>
#include "mariadbserver.hh"

// Used by multiple source files.
extern const char* const CN_AUTO_FAILOVER;
extern const char* const CN_SWITCHOVER_ON_LOW_DISK_SPACE;
extern const char* const CN_PROMOTION_SQL_FILE;
extern const char* const CN_DEMOTION_SQL_FILE;

// Map of base struct to MariaDBServer. Does not own the server objects.
typedef std::unordered_map<MXS_MONITORED_SERVER*, MariaDBServer*> ServerInfoMap;
// Map of server id:s to MariaDBServer. Useful when constructing the replication graph.
typedef std::unordered_map<int64_t, MariaDBServer*> IdToServerMap;
// Map of cycle number to cycle members. The elements should be ordered for predictability when iterating.
typedef std::map<int, ServerArray> CycleMap;

// MariaDB Monitor instance data
class MariaDBMonitor : public maxscale::MonitorInstance
{
private:
    MariaDBMonitor(const MariaDBMonitor&) = delete;
    MariaDBMonitor& operator=(const MariaDBMonitor&) = delete;
public:
    // Helper class used for testing.
    class Test;
    friend class Test;

    /**
     * Create the monitor instance and return the instance data.
     *
     * @param monitor Generic monitor data
     * @return MariaDBMonitor instance
     */
    static MariaDBMonitor* create(MXS_MONITOR* monitor);

    ~MariaDBMonitor();

    /**
     * Print diagnostics.
     *
     * @param dcb DCB to print to
     */
    void diagnostics(DCB* dcb) const;

    /**
     * Print diagnostics to json object.
     *
     * @return Diagnostics messages
     */
    json_t* diagnostics_json() const;

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

    /**
     * Perform user-activated reset-replication
     *
     * @param master_server The server to promote. If NULL, monitor will select the current master.
     * @param error_out Error output
     * @return True if operation completed successfully
     */
    bool run_manual_reset_replication(SERVER* master_server, json_t** error_out);

protected:
    void pre_loop();
    void tick();
    void process_state_changes();

private:
    // Some methods need a log on/off setting.
    enum class Log
    {
        OFF,
        ON
    };

    // Information about a multimaster group (replication cycle)
    struct CycleInfo
    {
        int         cycle_id = NodeData::CYCLE_NONE;
        ServerArray cycle_members;
    };

    /* Structure used to communicate commands and results between the MaxAdmin and monitor threads.
     * The monitor can only process one manual command at a time, which is already enforced by
     * the admin thread. */
    struct ManualCommand
    {
    public:
        std::mutex                mutex;        /* Mutex used by the condition variables */
        std::condition_variable   has_command;  /* Notified when a command is waiting execution */
        std::condition_variable   has_result;   /* Notified when the command has ran */
        std::function<void(void)> method;       /* The method to run when executing the command */

        bool command_waiting_exec = false;  /* Guard variable for has_command */
        bool result_waiting = false;        /* Guard variable for has_result */
    };

    ManualCommand m_manual_cmd;     /* Communicates manual commands and results */

    // Server containers, mostly constant.
    ServerArray   m_servers;        /* Servers of the monitor */
    ServerInfoMap m_server_info;    /* Map from server base struct to MariaDBServer */
    IdToServerMap m_servers_by_id;  /* Map from server id:s to MariaDBServer */

    // Topology related fields
    MariaDBServer* m_master = NULL;         /* The most "master-like" server in the cluster. Is the only
                                             * server which can get the Master status. */
    MariaDBServer* m_next_master = NULL;    /* When a cluster operation changes the master, the new master is
                                             * written here so the next monitor tick picks it up. */
    bool m_cluster_topology_changed = true; /* Has cluster topology changed since last monitor loop?
                                             * Causes a topology rebuild on the current tick. */
    bool m_cluster_modified = false;        /* Has a cluster operation been performed this loop? Prevents
                                             * other operations during this tick. */
    CycleMap  m_cycles;                     /* Map from cycle number to cycle member servers */
    CycleInfo m_master_cycle_status;        /* Info about master server cycle from previous round */

    // Miscellaneous info
    int64_t m_master_gtid_domain = GTID_DOMAIN_UNKNOWN;     /* gtid_domain_id most recently seen on
                                                             * the master */
    std::string m_external_master_host;                     /* External master host, for fail/switchover */
    int         m_external_master_port = PORT_UNKNOWN;      /* External master port */

    /* The default setting values given here may not be the actual defaults given by
     * the module configuration. */

    // Replication topology detection settings.
    bool m_detect_stale_master = true;      /* Allow stale masters. TODO: Remove this */
    bool m_detect_stale_slave = true;       /* Allow stale slaves: a running slave behind a downed
                                             * master/relay is still a valid slave */
    bool m_detect_standalone_master = true; /* Allow writes to a master without any slaves.
                                             * TODO: think about removing */
    bool m_ignore_external_masters = false; /* Ignore masters outside of the monitor configuration.
                                             * TODO: requires work */
    int m_failcount = 1;                    /* Number of ticks master must be down before it's considered
                                             * totally down, allowing failover or master change. */

    // Cluster operations activation settings
    bool m_auto_failover = false;                   /* Automatic master failover enabled? */
    bool m_auto_rejoin = false;                     /* Automatic rejoin enabled? */
    bool m_switchover_on_low_disk_space = false;    /* Automatically switch over a master low on disk space */
    bool m_maintenance_on_low_disk_space = false;   /* Automatically set slave and unreplicating servers low
                                                     * on disk space to maintenance. */
    bool m_enforce_read_only_slaves = false;        /* If true, the monitor checks and enforces every tick
                                                     * that all slaves are in read-only-mode. */
    // Cluster operations additional settings
    std::string m_replication_user;             /* Replication user for CHANGE MASTER TO-commands */
    std::string m_replication_password;         /* Replication password for CHANGE MASTER TO-commands */
    bool        m_handle_event_scheduler = true;/* Should failover/switchover enable/disable any scheduled
                                                 * events on the servers during promote/demote? */
    uint32_t    m_failover_timeout = 10;        /* Time limit in seconds for failover */
    uint32_t    m_switchover_timeout = 10;      /* Time limit in seconds for switchover */
    bool        m_verify_master_failure = true; /* Is master failure is verified via slaves? */
    int         m_master_failure_timeout = 10;  /* Master failure verification (via slaves) time in seconds */
    ServerArray m_excluded_servers;             /* Servers which cannot be autoselected when deciding which
                                                 * slave to promote during failover switchover. */
    std::string m_promote_sql_file;             /* File with sql commands which are ran to a server being
                                                 * promoted. */
    std::string m_demote_sql_file;              /* File with sql commands which are ran to a server being
                                                 * demoted. */

    // Fields controlling logging of various events. TODO: Check these
    bool m_log_no_master = true;                /* Should it be logged that there is no master? */
    bool m_warn_current_master_invalid = true;  /* Print warning if current master is not valid? */
    bool m_warn_have_better_master = true;      /* Print warning if the current master is not the best one? */
    bool m_warn_master_down = true;             /* Print warning that failover may happen soon? */
    bool m_warn_failover_precond = true;        /* Print failover preconditions error message? */
    bool m_warn_switchover_precond = true;      /* Print switchover preconditions error message? */
    bool m_warn_cannot_rejoin = true;           /* Print warning if auto_rejoin fails because of invalid
                                                 * gtid:s? */

    // Base methods
    MariaDBMonitor(MXS_MONITOR* monitor_base);
    bool configure(const MXS_CONFIG_PARAMETER* params);
    bool set_replication_credentials(const MXS_CONFIG_PARAMETER* params);
    void reset_server_info();
    void clear_server_info();
    void reset_node_index_info();
    bool execute_manual_command(std::function<void ()> command, json_t** error_out);

    std::string diagnostics_to_string() const;
    json_t*     to_json() const;

    MariaDBServer* get_server_info(MXS_MONITORED_SERVER* db);
    MariaDBServer* get_server(int64_t id);
    MariaDBServer* get_server(SERVER* server);

    // Cluster discovery and status assignment methods, top levels
    void update_server(MariaDBServer* server);
    void update_topology();
    void build_replication_graph();
    void assign_new_master(MariaDBServer* new_master);
    void find_graph_cycles();
    bool master_is_valid(std::string* reason_out);
    void assign_server_roles();
    void assign_slave_and_relay_master(MariaDBServer* start_node);
    void check_cluster_operations_support();

    MariaDBServer* find_topology_master_server(std::string* msg_out);
    MariaDBServer* find_master_inside_cycle(ServerArray& cycle_servers);
    MariaDBServer* find_best_reach_server(const ServerArray& candidates);

    // Cluster discovery and status assignment methods, low level
    void tarjan_scc_visit_node(MariaDBServer* node, ServerArray* stack, int* index, int* cycle);
    void calculate_node_reach(MariaDBServer* search_root);
    int  running_slaves(MariaDBServer* search_root);
    bool cycle_has_master_server(ServerArray& cycle_servers);
    void update_gtid_domain();
    void update_external_master();
    void update_master_cycle_info();

    // Cluster operation launchers
    bool manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out);
    bool manual_failover(json_t** output);
    bool manual_rejoin(SERVER* rejoin_server, json_t** output);
    void handle_low_disk_space_master();
    void handle_auto_failover();
    void handle_auto_rejoin();

    const MariaDBServer* slave_receiving_events(const MariaDBServer* demotion_target,
                                                maxbase::Duration*   event_age_out,
                                                maxbase::Duration*   delay_out);
    std::unique_ptr<ClusterOperation> switchover_prepare(SERVER* new_master, SERVER* current_master,
                                                         Log log_mode, json_t** error_out);
    std::unique_ptr<ClusterOperation> failover_prepare(Log log_mode, json_t** error_out);

    bool switchover_perform(ClusterOperation& operation);
    bool failover_perform(ClusterOperation& operation);

    // Methods used by failover/switchover/rejoin
    MariaDBServer* select_promotion_target(MariaDBServer* current_master, OperationType op,
                                           Log log_mode, json_t** error_out);
    bool is_candidate_better(const MariaDBServer* candidate, const MariaDBServer* current_best,
                             const MariaDBServer* demotion_target, uint32_t gtid_domain,
                             std::string* reason_out = NULL);
    bool server_is_excluded(const MariaDBServer* server);
    bool check_gtid_replication(Log log_mode, const MariaDBServer* demotion_target,
                                json_t** error_out);

    ServerArray get_redirectables(const MariaDBServer* promotion_target,
                                  const MariaDBServer* demotion_target);
    int redirect_slaves(MariaDBServer* new_master, const ServerArray& slaves,
                        ServerArray* redirected_slaves);
    int redirect_slaves_ex(ClusterOperation& op, const ServerArray& slaves,
                           ServerArray* redirected_slaves);
    bool        switchover_start_slave(MariaDBServer* old_master, MariaDBServer* new_master);
    bool        start_external_replication(MariaDBServer* new_master, json_t** err_out);
    std::string generate_change_master_cmd(const std::string& master_host, int master_port);
    void        wait_cluster_stabilization(ClusterOperation& op, const ServerArray& slaves);
    void        report_and_disable(const std::string& operation, const std::string& setting_name,
                                   bool* setting_var);

    // Rejoin methods
    bool     cluster_can_be_joined();
    bool     get_joinable_servers(ServerArray* output);
    bool     server_is_rejoin_suspect(MariaDBServer* rejoin_cand, json_t** output);
    uint32_t do_rejoin(const ServerArray& joinable_servers, json_t** output);

    // Other methods
    void disable_setting(const std::string& setting);
    bool check_sql_files();
    void enforce_read_only_on_slaves();
    void log_master_changes();
    void set_low_disk_slaves_maintenance();
    bool manual_reset_replication(SERVER* master_server, json_t** error_out);
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
