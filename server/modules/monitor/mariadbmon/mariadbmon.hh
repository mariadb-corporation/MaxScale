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
#include "mariadbmon_common.hh"
#include <string>
#include <tr1/unordered_map>
#include <vector>

#include <maxscale/monitor.hh>
#include <maxscale/thread.h>

#include "mariadbserver.hh"

extern const char * const CN_AUTO_FAILOVER;
extern const char * const CN_PROMOTION_SQL_FILE;
extern const char * const CN_DEMOTION_SQL_FILE;

class MariaDBMonitor;

// Map of base struct to MariaDBServer. Does not own the server objects. May not be needed at the end.
typedef std::tr1::unordered_map<MXS_MONITORED_SERVER*, MariaDBServer*> ServerInfoMap;
// Server pointer array
typedef std::vector<MariaDBServer*> ServerArray;

// MariaDB Monitor instance data
class MariaDBMonitor : public maxscale::MonitorInstance
{
private:
    MariaDBMonitor(const MariaDBMonitor&);
    MariaDBMonitor& operator = (const MariaDBMonitor&);
public:
    // TODO: Once done refactoring, see which of these can be moved to private.

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
     * Handle switchover
     *
     * @new_master      The specified new master
     * @current_master  The specified current master. If NULL, monitor will autoselect.
     * @output          Pointer where to place output object
     *
     * @return True, if switchover was performed, false otherwise.
     */
    bool manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out);

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

protected:
    void update_server_status(MXS_MONITORED_SERVER* pMonitored_server);
    void main();

private:
    unsigned long m_id;              /**< Monitor ID */
    ServerArray m_servers;           /**< Servers of the monitor */
    ServerInfoMap m_server_info;     /**< Map from server base struct to MariaDBServer */

    // Values updated by monitor
    MariaDBServer* m_master;         /**< Master server for Master/Slave replication */
    int64_t m_master_gtid_domain;    /**< gtid_domain_id most recently seen on the master  */
    std::string m_external_master_host; /**< External master host, for fail/switchover */
    int m_external_master_port;      /**< External master port */
    bool m_cluster_modified;         /**< Has an automatic failover/rejoin been performed this loop? */

    // Replication topology detection settings
    bool m_allow_cluster_recovery;   /**< Allow failed servers to rejoin the cluster */
    bool m_detect_replication_lag;   /**< Monitor flag for MySQL replication heartbeat */
    bool m_detect_multimaster;       /**< Detect and handle multi-master topologies */
    bool m_detect_stale_master;      /**< Monitor flag for MySQL replication Stale Master detection */
    bool m_detect_stale_slave;       /**< Monitor flag for MySQL replication Stale Slave detection */
    bool m_detect_standalone_master; /**< If standalone master are detected */
    bool m_ignore_external_masters;  /**< Ignore masters outside of the monitor configuration */
    bool m_mysql51_replication;      /**< Use MySQL 5.1 replication */

    // Failover, switchover and rejoin settings
    bool m_auto_failover;            /**< Is automatic master failover is enabled? */
    bool m_auto_rejoin;              /**< Is automatic rejoin enabled? */
    int m_failcount;                 /**< Numer of cycles master must be down before auto-failover begins */
    std::string m_replication_user;  /**< Replication user for CHANGE MASTER TO-commands */
    std::string m_replication_password; /**< Replication password for CHANGE MASTER TO-commands */
    uint32_t m_failover_timeout;     /**< Time limit in seconds for master failover */
    uint32_t m_switchover_timeout;   /**< Time limit in seconds for master switchover */
    bool m_verify_master_failure;    /**< Is master failure is verified via slaves? */
    int m_master_failure_timeout;    /**< Master failure verification (via slaves) time in seconds */
    ServerArray m_excluded_servers;  /**< Servers banned for master promotion during auto-failover or
                                      *   autoselect switchover. */
    std::string m_promote_sql_file;  /**< File with sql commands which are ran to a server being promoted. */
    std::string m_demote_sql_file;   /**< File with sql commands which are ran to a server being demoted. */
    bool m_enforce_read_only_slaves; /**< Should the monitor set read-only=1 on any slave servers. */

    // Other settings
    std::string m_script;            /**< Script to call when state changes occur on servers */
    uint64_t m_events;               /**< enabled events */
    bool m_warn_set_standalone_master; /**< Log a warning when setting standalone master */

    enum slave_down_setting_t
    {
        ACCEPT_DOWN,
        REJECT_DOWN
    };

    // Base methods
    MariaDBMonitor(MXS_MONITOR* monitor_base);
    void reset_server_info();
    void clear_server_info();
    bool configure(const MXS_CONFIG_PARAMETER* params);
    bool set_replication_credentials(const MXS_CONFIG_PARAMETER* params);
    MariaDBServer* get_server_info(MXS_MONITORED_SERVER* db);

    // Cluster discovery and status assignment methods
    MariaDBServer* find_root_master();
    MXS_MONITORED_SERVER* get_replication_tree();
    MXS_MONITORED_SERVER* build_mysql51_replication_tree();
    void find_graph_cycles();
    void update_server_states(MariaDBServer& db_server, MariaDBServer* root_master);
    bool standalone_master_required();
    bool set_standalone_master();
    void assign_relay_master(MariaDBServer& serv_info);
    void log_master_changes(MariaDBServer* root_master, int* log_no_master);
    void update_gtid_domain();
    void update_external_master();
    void set_master_heartbeat(MariaDBServer*);
    void set_slave_heartbeat(MariaDBServer*);
    void measure_replication_lag(MariaDBServer* root_master);
    void check_maxscale_schema_replication();
    MXS_MONITORED_SERVER* getServerByNodeId(long);
    MXS_MONITORED_SERVER* getSlaveOfNodeId(long, slave_down_setting_t);

    // Switchover methods
    bool switchover_check(SERVER* new_master, SERVER* current_master,
                          MariaDBServer** new_master_out, MariaDBServer** current_master_out,
                          json_t** error_out);
    bool switchover_check_new(const MariaDBServer* new_master_cand, json_t** error);
    bool switchover_check_current(const MXS_MONITORED_SERVER* suggested_curr_master,
                                  json_t** error_out) const;
    bool do_switchover(MariaDBServer** current_master, MariaDBServer** new_master, json_t** err_out);
    bool switchover_check_preferred_master(MariaDBServer* preferred, json_t** err_out);
    bool switchover_demote_master(MariaDBServer* current_master,
                                  json_t** err_out);
    bool switchover_wait_slaves_catchup(const ServerArray& slaves, const GtidList& gtid, int total_timeout,
                                        json_t** err_out);
    bool switchover_start_slave(MariaDBServer* old_master, MariaDBServer* new_master);

    // Failover methods
    bool handle_auto_failover();
    bool failover_not_possible();
    bool slave_receiving_events();
    bool failover_check(json_t** error_out);
    bool do_failover(json_t** err_out);

    // Rejoin methods
    bool cluster_can_be_joined();
    void handle_auto_rejoin();
    bool get_joinable_servers(ServerArray* output);
    bool server_is_rejoin_suspect(MariaDBServer* rejoin_cand, json_t** output);
    uint32_t do_rejoin(const ServerArray& joinable_servers, json_t** output);

    // Methods common to failover/switchover/rejoin
    MariaDBServer* select_new_master(ServerArray* slaves_out, json_t** err_out);
    bool server_is_excluded(const MariaDBServer* server);
    bool is_candidate_better(const MariaDBServer* current_best, const MariaDBServer* candidate,
                             uint32_t gtid_domain);
    bool promote_new_master(MariaDBServer* new_master, json_t** err_out);
    int redirect_slaves(MariaDBServer* new_master, const ServerArray& slaves,
                        ServerArray* redirected_slaves);
    std::string generate_change_master_cmd(const std::string& master_host, int master_port);
    bool start_external_replication(MariaDBServer* new_master, json_t** err_out);
    bool wait_cluster_stabilization(MariaDBServer* new_master, const ServerArray& slaves,
                                    int seconds_remaining);

    // Other methods
    void disable_setting(const char* setting);
    void load_journal();
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
