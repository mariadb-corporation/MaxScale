/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
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

#include <maxbase/http.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/threadpool.hh>
#include <maxscale/monitor.hh>
#include "mariadbserver.hh"
#include "monitor_commands.hh"

// Used by multiple source files.
extern const char* const CN_AUTO_FAILOVER;
extern const char* const CN_SWITCHOVER_ON_LOW_DISK_SPACE;
extern const char* const CN_MAINTENANCE_ON_LOW_DISK_SPACE;
extern const char* const CN_PROMOTION_SQL_FILE;
extern const char* const CN_DEMOTION_SQL_FILE;

// Map of server id:s to MariaDBServer. Useful when constructing the replication graph.
typedef std::unordered_map<int64_t, MariaDBServer*> IdToServerMap;
// Map of cycle number to cycle members. The elements should be ordered for predictability when iterating.
typedef std::map<int, ServerArray> CycleMap;

// MariaDB Monitor instance data
class MariaDBMonitor : public maxscale::Monitor
{
public:
    MariaDBMonitor(const MariaDBMonitor&) = delete;
    MariaDBMonitor& operator=(const MariaDBMonitor&) = delete;

    class Test;
    friend class Test;
    friend class mon_op::BackupOperation;
    friend class mon_op::RebuildServer;
    friend class mon_op::CreateBackup;
    friend class mon_op::RestoreFromBackup;

    // Weakly-typed enums since cast to integer.
    enum RequireLocks
    {
        LOCKS_NONE = 0,
        LOCKS_MAJORITY_RUNNING,
        LOCKS_MAJORITY_ALL
    };

    /**
     * Create the monitor instance and return the instance data.
     *
     * @param name Monitor config name
     * @param module Module name
     * @return MariaDBMonitor instance
     */
    static MariaDBMonitor* create(const std::string& name, const std::string& module);

    /**
     * Print diagnostics to json object.
     *
     * @return Diagnostics messages
     */
    json_t* diagnostics() const override;

    /**
     * Return information about a single server
     */
    json_t* diagnostics(mxs::MonitorServer* server) const override;

    /**
     * Perform user-activated switchover.
     *
     * @param type            Normal or forced
     * @param new_master      The specified new master. If NULL, monitor will autoselect.
     * @param current_master  The specified current master. If NULL, monitor will autoselect.
     * @param error_out       Json error output
     * @return True if switchover was performed
     */
    bool run_manual_switchover(SwitchoverType type, SERVER* new_master, SERVER* current_master,
                               json_t** error_out);

    /**
     * Perform user-activated switchover. Does not wait for results, which should be fetched separately.
     *
     * @param new_master      The specified new master. If NULL, monitor will autoselect.
     * @param current_master  The specified current master. If NULL, monitor will autoselect.
     * @param error_out       Json error output
     * @return True if switchover was scheduled
     */
    bool schedule_async_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out);

    /**
     * Perform user-activated failover.
     *
     * @param error_out Json error output
     * @return True if failover was performed
     */
    bool run_manual_failover(json_t** error_out);

    /**
     * Perform user-activated failover. Does not wait for results, which should be fetched separately.
     *
     * @param error_out Json error output
     * @return True if failover was scheduled
     */
    bool schedule_async_failover(json_t** error_out);

    /**
     * Perform user-activated rejoin
     *
     * @param rejoin_server Server to join
     * @param error_out Json error output
     * @return True if rejoin was performed
     */
    bool run_manual_rejoin(SERVER* rejoin_server, json_t** error_out);

    /**
     * Perform user-activated rejoin. Does not wait for results, which should be fetched separately.
     *
     * @param rejoin_server Server to join
     * @param error_out Json error output
     * @return True if rejoin was scheduled
     */
    bool schedule_async_rejoin(SERVER* rejoin_server, json_t** error_out);

    /**
     * Perform user-activated reset-replication
     *
     * @param master_server The server to promote. If NULL, monitor will select the current master.
     * @param error_out Error output
     * @return True if operation completed successfully
     */
    bool run_manual_reset_replication(SERVER* master_server, json_t** error_out);

    /**
     * Perform user-activated reset-replication. Does not wait for results, which should be fetched
     * separately.
     *
     * @param master_server The server to promote. If NULL, monitor will select the current master.
     * @param error_out Error output
     * @return True if operation was scheduled
     */
    bool schedule_reset_replication(SERVER* master_server, json_t** error_out);

    /**
     * Perform user-activated lock release.
     *
     * @param error_out Error output
     * @return True if locks are in use, even if none were released
     */
    bool run_release_locks(json_t** error_out);

    /**
     * Perform user-activated lock release. Does not wait for results, which should be fetched
     * separately.
     *
     * @param error_out Error output
     * @return True if locks are in use, even if none were released
     */
    bool schedule_release_locks(json_t** error_out);

    /**
     * Fetch results of last asynchronous command. If an async command is still running, outputs
     * "<command> in still pending/running".
     *
     * @param output Output
     * @return True if results of a manual command were available.
     */
    bool fetch_cmd_result(json_t** output);

    /**
     * Cancel currently pending or running manual command.
     *
     * @param output Output
     * @return True if an operation was pending or running. Even if true is returned, the operation may
     * have managed to complete before cancelling.
     */
    bool cancel_cmd(json_t** output);

    /**
     * Perform user-activated ColumnStore add node. Does not wait for results, which should be fetched
     * separately.
     *
     * @param host The host to add
     * @param timeout Timeout in seconds
     * @param error_out Error output
     * @return True if operation was scheduled
     */
    bool schedule_cs_add_node(const std::string& host, std::chrono::seconds timeout, json_t** error_out);

    /**
     * Perform user-activated ColumnStore remove node. Does not wait for results, which should be fetched
     * separately.
     *
     * @param host The host to remove
     * @param timeout Timeout in seconds
     * @param error_out Error output
     * @return True if operation was scheduled
     */
    bool schedule_cs_remove_node(const std::string& host, std::chrono::seconds timeout, json_t** error_out);

    /**
     * Get ColumnStore cluster status.
     *
     * @param output Output
     * @return True if status was fetched
     */
    bool run_cs_get_status(json_t** output);

    /**
     * Get ColumnStore cluster status. Does not wait for results, which should be fetched separately.
     *
     * @param output Output
     * @return True if operation was scheduled
     */
    bool schedule_cs_get_status(json_t** output);

    /**
     * Perform user-activated ColumnStore start/stop cluster. Does not wait for results, which should be
     * fetched
     * separately.
     *
     * @param timeout Timeout in seconds
     * @param error_out Error output
     * @return True if operation was scheduled
     */
    bool schedule_cs_start_cluster(std::chrono::seconds timeout, json_t** error_out);
    bool schedule_cs_stop_cluster(std::chrono::seconds timeout, json_t** error_out);

    /**
     * Set ColumnStore cluster read-only/readwrite. Does not wait for results, which should be fetched
     * separately.
     *
     * @param timeout Timeout in seconds
     * @param error_out Error output
     * @return True if operation was scheduled
     */
    bool schedule_cs_set_readonly(std::chrono::seconds timeout, json_t** error_out);
    bool schedule_cs_set_readwrite(std::chrono::seconds timeout, json_t** error_out);

    bool schedule_rebuild_server(SERVER* target, SERVER* source, const std::string& datadir,
                                 json_t** error_out);
    bool schedule_create_backup(SERVER* source, const std::string& bu_name, json_t** error_out);
    bool schedule_restore_from_backup(SERVER* target, const std::string& bu_name, const std::string& datadir,
                                      json_t** error_out);
    bool is_cluster_owner() const override;

    mxs::config::Configuration& configuration() override final;

protected:
    bool can_be_disabled(const mxs::MonitorServer& server, DisableType type,
                         std::string* errmsg_out) const override;

    void        tick() override;
    void        process_state_changes();
    void        flush_mdb_server_status();
    std::string annotate_state_change(mxs::MonitorServer* server) override final;

private:
    using ServerFunction = std::function<void (MariaDBServer*)>;

    // Some methods need a log on/off setting.
    enum class Log
    {
        OFF,
        ON
    };

    enum class RequireRunning
    {
        REQUIRED,
        OPTIONAL
    };

    enum class State
    {
        IDLE,
        MONITOR,
        EXECUTE_SCRIPTS,
        DEMOTE,
        WAIT_FOR_TARGET_CATCHUP,
        PROMOTE_TARGET,
        REJOIN,
        CONFIRM_REPLICATION,
        RESET_REPLICATION,
    };

    class SwitchoverParams
    {
    public:
        ServerOperation promotion;
        ServerOperation demotion;
        GeneralOpData   general;
        SwitchoverType  type {SwitchoverType::NORMAL};

        SwitchoverParams(ServerOperation promotion, ServerOperation demotion,
                         const GeneralOpData& general, SwitchoverType type);
    };

    class FailoverParams
    {
    public:
        ServerOperation            promotion;   // Required by MariaDBServer->promote()
        const MariaDBServer* const demotion_target;
        GeneralOpData              general;

        FailoverParams(ServerOperation promotion, const MariaDBServer* demotion_target,
                       const GeneralOpData& general);
    };

    // Information about a multimaster group (replication cycle)
    struct CycleInfo
    {
        int         cycle_id = NodeData::CYCLE_NONE;
        ServerArray cycle_members;
    };

    class DNSResolver
    {
    public:
        using StringSet = std::unordered_set<std::string>;
        StringSet resolve_server(const std::string& host);

    private:
        struct MapElement
        {
            StringSet      addresses;   // A hostname can map to multiple addresses
            mxb::TimePoint timestamp;
        };

        std::unordered_map<std::string, MapElement> m_mapping;      // hostname -> address cache
    };

    mon_op::OperationInfo m_op_info;    /* Manual/automatic op info */
    mon_op::SOperation    m_running_op; /* Currently running op */

    IdToServerMap               m_servers_by_id;    /**< Map from server id:s to MariaDBServer */
    std::vector<MariaDBServer*> m_servers;          /**< All monitored servers */

    /* The current state of a cluster modifying operation */
    std::atomic<State> m_state {State::IDLE};

    mxb::ThreadPool m_threadpool;   /* Threads used in concurrent operations */

    // Topology related fields
    MariaDBServer* m_master = NULL;         /* The most "master-like" server in the cluster. Is the only
                                             * server which can get the Master status. */
    MariaDBServer* m_next_master = NULL;    /* When a cluster operation changes the master, the new master is
                                             * written here so the next monitor tick picks it up. */
    bool m_cluster_topology_changed = true; /* Has cluster topology changed since last monitor loop?
                                             * Causes a topology rebuild on the current tick. */
    bool m_cluster_modified = false;        /* Has a cluster operation been performed this loop? Prevents
                                             * other operations during this tick. */

    DNSResolver m_resolver;                 /* DNS-resolver with cache */

    /* Counter for temporary automatic cluster operation disabling. */
    int cluster_operation_disable_timer = 0;

    CycleMap  m_cycles;                     /* Map from cycle number to cycle member servers */
    CycleInfo m_master_cycle_status;        /* Info about master server cycle from previous round */

    // Miscellaneous info
    int64_t m_master_gtid_domain = GTID_DOMAIN_UNKNOWN;     /* gtid_domain_id most recently seen on
                                                             * the master */

    bool m_warn_current_master_invalid {true};  /* Print warning if current master is not valid? */
    bool m_warn_cannot_find_master {true};      /* Print warning if a master cannot be found? */
    bool m_warn_master_down {true};             /* Print warning that failover may happen soon? */
    bool m_warn_failover_precond {true};        /* Print failover preconditions error message? */
    bool m_warn_switchover_precond {true};      /* Print switchover preconditions error message? */
    bool m_warn_cannot_rejoin {true};           /* Print warning if auto_rejoin fails because of invalid
                                                 * gtid:s? */

    struct ClusterLocksInfo
    {
        bool time_to_update() const;

        std::atomic_bool have_lock_majority {false};/* Is this the primary monitor for the cluster? */
        mxb::StopWatch   last_locking_attempt;      /* Time since last server lock attempt */

        /* Time until next locking attempt. Initialized to zero to allow an attempt during first loop. */
        mxb::Duration next_lock_attempt_delay {0};

        void reset()
        {
            have_lock_majority = false;
            last_locking_attempt = mxb::StopWatch();
            next_lock_attempt_delay = mxb::Duration(0);
        }
    };
    ClusterLocksInfo m_locks_info;

    mxb::XorShiftRandom m_random_gen;

    // MariaDB-Monitor specific settings. These are only written to when configuring the monitor.
    class Settings : public mxs::config::Configuration
    {
    public:
        Settings(const std::string& name, MariaDBMonitor* monitor);

        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

        /* The default setting values given here may not be the actual defaults given by
         * the module configuration. */

        // Replication topology detection settings.

        bool assume_unique_hostnames;   /* Are server hostnames consistent between MaxScale and servers */

        int64_t failcount;      /* Number of ticks master must be down before it's considered
                                 * totally down, allowing failover or master change. */

        // Cluster operations activation settings

        bool auto_failover;                 /* Automatic master failover enabled? */
        bool auto_rejoin;                   /* Automatic rejoin enabled? */
        bool switchover_on_low_disk_space;  /* Automatically switch over a master low on disk space */
        bool maintenance_on_low_disk_space; /* Automatically set slave and unreplicating servers low
                                             * on disk space to maintenance. */
        bool enforce_read_only_slaves;      /* If true, the monitor checks and enforces every tick
                                             * that all slaves are in read-only-mode. */
        bool enforce_writable_master;       /* If true, set master writable if it's read-only. */
        bool enforce_simple_topology;       /* Can the monitor assume and enforce a simple, 1-master
                                             * and N slaves topology? Also allows unsafe failover */

        /* Should all cluster modification commands require a majority of server locks?
         * Used in multi-Maxscale situations. */
        RequireLocks require_server_locks;

        // Cluster operations additional settings
        using seconds = std::chrono::seconds;
        seconds failover_timeout;           /* Time limit in seconds for failover */
        bool    verify_master_failure;      /* Is master failure is verified via slaves? */
        seconds master_failure_timeout;     /* Master failure verification (via slaves) time in seconds */

        std::vector<SERVER*> servers_no_promotion;      /* Servers which cannot be autoselected when deciding
                                                         * which slave to promote during failover switchover.
                                                         */

        int64_t script_max_rlag;            /* Repl. lag limit for triggering custom event for script */

        MariaDBServer::SharedSettings shared;   /* Settings required by MariaDBServer objects */

        int64_t     cs_admin_port;      /* ColumnStore admin port. Assumed same on all servers. */
        std::string cs_admin_base_path; /* ColumnStore rest-api base path */
        std::string cs_admin_api_key;   /* ColumnStore rest-api key */

        std::string          ssh_user;              /**< SSH username for accessing servers */
        std::string          ssh_keyfile;           /**< SSH keyfile for accessing server */
        bool                 ssh_host_check {true}; /**< Check that host is in known_hosts */
        std::chrono::seconds ssh_timeout;           /**< SSH connection and command timeout */
        int64_t              ssh_port {0};          /**< SSH port on all servers */
        int64_t              rebuild_port {0};      /**< Listen port for server backup transfer */
        std::string          mbu_use_memory;        /**< Given to mariabackup --prepare --use-memory=<val> */
        int64_t              mbu_parallel {1};      /**< Given to mariabackup --parallel=<val> */
        std::string          backup_storage_addr;   /**< Backup storage host */
        std::string          backup_storage_path;   /**< Backup storage directory */

    private:
        MariaDBMonitor* m_monitor;
    };

    Settings          m_settings;
    ServerArray       m_excluded_servers;
    mxb::http::Config m_http_config;    /* Http-configuration. Used for ColumnStore commands. */

    // Base methods
    MariaDBMonitor(const std::string& name, const std::string& module);
    std::tuple<bool, std::string> prepare_to_stop() override;
    void                          pre_loop() override;
    void                          post_loop() override;

    void reset_server_info();
    void reset_node_index_info();
    bool execute_manual_command(mon_op::CmdMethod command, const std::string& cmd_name,
                                json_t** error_out);
    bool schedule_manual_command(mon_op::CmdMethod command, const std::string& cmd_name,
                                 json_t** error_out);
    bool schedule_manual_command(mon_op::SOperation op, const std::string& cmd_name,
                                 json_t** error_out);
    bool start_long_running_op(mon_op::SOperation op, const std::string& cmd_name);
    bool server_locks_in_use() const;
    void execute_task_all_servers(const ServerFunction& task);
    void execute_task_on_servers(const ServerFunction& task, const ServerArray& servers);

    json_t*        to_json() const;
    static json_t* to_json(State op);

    MariaDBServer* get_server_by_addr(const EndPoint& srv_addr);
    MariaDBServer* get_server(int64_t id);
    MariaDBServer* get_server(mxs::MonitorServer* mon_server) const;
    MariaDBServer* get_server(SERVER* server) const;

    void save_monitor_specific_journal_data(mxb::Json& data) override;
    void load_monitor_specific_journal_data(const mxb::Json& data) override;

    // Cluster discovery and status assignment methods, top levels
    void update_topology();
    void build_replication_graph();
    void update_master();
    void assign_new_master(MariaDBServer* new_master);
    void find_graph_cycles();
    bool master_is_valid(std::string* reason_out);
    void assign_server_roles();
    void assign_slave_and_relay_master();
    void check_cluster_operations_support();
    bool try_acquire_locks_this_tick();
    void update_cluster_lock_status();
    int  get_free_locks();
    bool is_slave_maxscale() const;

    MariaDBServer* find_topology_master_server(RequireRunning req_running, std::string* msg_out = nullptr);
    MariaDBServer* find_best_reach_server(const ServerArray& candidates);

    // Cluster discovery and status assignment methods, low level
    void tarjan_scc_visit_node(MariaDBServer* node, ServerArray* stack, int* index, int* cycle);
    void calculate_node_reach(MariaDBServer* search_root);
    int  running_slaves(MariaDBServer* search_root);
    bool cycle_has_master_server(ServerArray& cycle_servers);
    void update_gtid_domain();
    void check_acquire_masterlock();
    void update_master_cycle_info();
    bool is_candidate_valid(MariaDBServer* cand, RequireRunning req_running, std::string* why_not = nullptr);

    // Cluster operation launchers
    mon_op::Result manual_switchover(SwitchoverType type, SERVER* new_master, SERVER* current_master);
    mon_op::Result manual_failover();
    mon_op::Result manual_rejoin(SERVER* rejoin_cand_srv);
    mon_op::Result manual_reset_replication(SERVER* master_server);
    mon_op::Result manual_release_locks();
    void           handle_low_disk_space_master();
    void           handle_auto_failover();
    void           handle_auto_rejoin();

    // ColumnStore operations
    mon_op::Result manual_cs_add_node(const std::string& node_host, std::chrono::seconds timeout);
    mon_op::Result manual_cs_remove_node(const std::string& node_host, std::chrono::seconds timeout);
    mon_op::Result manual_cs_get_status();
    mon_op::Result manual_cs_start_cluster(std::chrono::seconds timeout);
    mon_op::Result manual_cs_stop_cluster(std::chrono::seconds timeout);
    mon_op::Result manual_cs_set_readonly(std::chrono::seconds timeout);
    mon_op::Result manual_cs_set_readwrite(std::chrono::seconds timeout);

    enum class HttpCmd
    {
        GET, PUT, DELETE
    };
    using RestDataFields = std::vector<std::pair<std::string, std::string>>;
    using CsRestResult = std::tuple<bool, std::string, mxb::Json>;
    CsRestResult run_cs_rest_cmd(HttpCmd httcmd, const std::string& rest_cmd, const RestDataFields& data,
                                 std::chrono::seconds cs_timeout);
    static CsRestResult check_cs_rest_result(const mxb::http::Response& resp);

    const MariaDBServer* slave_receiving_events(const MariaDBServer* demotion_target,
                                                maxbase::Duration* event_age_out,
                                                maxbase::Duration* delay_out) const;
    std::unique_ptr<SwitchoverParams>
    switchover_prepare(SwitchoverType type, SERVER* new_master, SERVER* current_master,
                       Log log_mode, OpStart start, mxb::Json& error_out);
    std::unique_ptr<FailoverParams> failover_prepare(Log log_mode, OpStart start, mxb::Json& error_out);

    bool switchover_perform(SwitchoverParams& operation);
    bool failover_perform(FailoverParams& op);

    void delay_auto_cluster_ops(Log log = Log::ON);
    bool can_perform_cluster_ops();
    bool cluster_ops_configured() const;
    bool cluster_operations_disabled_short() const;
    bool lock_status_is_ok() const;

    // Methods used by failover/switchover/rejoin
    MariaDBServer* select_promotion_target(MariaDBServer* demotion_target, OperationType op, Log log_mode,
                                           int64_t* gtid_domain_out, mxb::Json& error_out);
    bool is_candidate_better(const MariaDBServer* candidate, const MariaDBServer* current_best,
                             const MariaDBServer* demotion_target, uint32_t gtid_domain,
                             std::string* reason_out = nullptr);
    bool server_is_excluded(const MariaDBServer* server);
    bool check_gtid_replication(Log log_mode, const MariaDBServer* demotion_target,
                                int64_t cluster_gtid_domain, mxb::Json& error_out);
    int64_t guess_gtid_domain(MariaDBServer* demotion_target, const ServerArray& candidates,
                              int* id_missing_out) const;

    ServerArray get_redirectables(const MariaDBServer* old_master, const MariaDBServer* ignored_slave);

    int redirect_slaves_ex(GeneralOpData& op,
                           OperationType type,
                           const MariaDBServer* promotion_target,
                           const MariaDBServer* demotion_target,
                           ServerArray* redirected_to_promo,
                           ServerArray* redirected_to_demo);

    void wait_cluster_stabilization(GeneralOpData& op, const ServerArray& slaves,
                                    const MariaDBServer* new_master);

    // Rejoin methods
    bool     cluster_can_be_joined();
    bool     get_joinable_servers(GeneralOpData& op, ServerArray* output);
    bool     server_is_rejoin_suspect(GeneralOpData& op, MariaDBServer* rejoin_cand);
    uint32_t do_rejoin(GeneralOpData& op, const ServerArray& joinable_servers);

    void enforce_read_only_on_slaves();
    void enforce_writable_on_master();
    void set_low_disk_slaves_maintenance();

    bool        post_configure();
    friend bool Settings::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params);
    void        configured_servers_updated(const std::vector<SERVER*>& servers) override;
};

/**
 * Generates a list of server names separated by ', '
 *
 * @param servers The servers
 * @return Server names
 */
std::string monitored_servers_to_string(const ServerArray& servers);
