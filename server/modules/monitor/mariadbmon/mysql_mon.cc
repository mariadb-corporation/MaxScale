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

#define MXS_MODULE_NAME "mariadbmon"

#include "../mysqlmon.h"
#include <inttypes.h>
#include <limits>
#include <string>
#include <sstream>
#include <vector>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/debug.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/json_api.h>
#include <maxscale/modulecmd.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/utils.h>
// TODO: For monitorAddParameters
#include "../../../core/internal/monitor.h"

/** Column positions for SHOW SLAVE STATUS */
#define MYSQL55_STATUS_MASTER_LOG_POS 5
#define MYSQL55_STATUS_MASTER_LOG_FILE 6
#define MYSQL55_STATUS_IO_RUNNING 10
#define MYSQL55_STATUS_SQL_RUNNING 11
#define MYSQL55_STATUS_MASTER_ID 39

/** Column positions for SHOW SLAVE STATUS */
#define MARIA10_STATUS_MASTER_LOG_FILE 7
#define MARIA10_STATUS_MASTER_LOG_POS 8
#define MARIA10_STATUS_IO_RUNNING 12
#define MARIA10_STATUS_SQL_RUNNING 13
#define MARIA10_STATUS_MASTER_ID 41
#define MARIA10_STATUS_HEARTBEATS 55
#define MARIA10_STATUS_HEARTBEAT_PERIOD 56
#define MARIA10_STATUS_SLAVE_GTID 57

/** Column positions for SHOW SLAVE HOSTS */
#define SLAVE_HOSTS_SERVER_ID 0
#define SLAVE_HOSTS_HOSTNAME 1
#define SLAVE_HOSTS_PORT 2

/** Utility macro for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(err_out, format, ...)\
    do {\
       MXS_ERROR(format, ##__VA_ARGS__);\
       if (err_out)\
       {\
            *err_out = mxs_json_error_append(*err_out, format, ##__VA_ARGS__);\
       }\
    } while (false)

using std::string;
typedef std::vector<MXS_MONITORED_SERVER*> ServerVector;
typedef std::vector<string> StringVector;
class MySqlServerInfo;

enum mysql_server_version
{
    MYSQL_SERVER_VERSION_100,
    MYSQL_SERVER_VERSION_55,
    MYSQL_SERVER_VERSION_51
};

enum slave_down_setting_t
{
    ACCEPT_DOWN,
    REJECT_DOWN
};

enum print_repl_warnings_t
{
    WARNINGS_ON,
    WARNINGS_OFF
};

static void monitorMain(void *);
static void *startMonitor(MXS_MONITOR *, const MXS_CONFIG_PARAMETER*);
static void stopMonitor(MXS_MONITOR *);
static bool stop_monitor(MXS_MONITOR *);
static void diagnostics(DCB *, const MXS_MONITOR *);
static json_t* diagnostics_json(const MXS_MONITOR *);
static MXS_MONITORED_SERVER *getServerByNodeId(MXS_MONITORED_SERVER *, long);
static MXS_MONITORED_SERVER *getSlaveOfNodeId(MXS_MONITORED_SERVER *, long, slave_down_setting_t);
static MXS_MONITORED_SERVER *get_replication_tree(MXS_MONITOR *, int);
static void set_master_heartbeat(MYSQL_MONITOR *, MXS_MONITORED_SERVER *);
static void set_slave_heartbeat(MXS_MONITOR *, MXS_MONITORED_SERVER *);
static int add_slave_to_master(long *, int, long);
static bool isMySQLEvent(mxs_monitor_event_t event);
void check_maxscale_schema_replication(MXS_MONITOR *monitor);
static MySqlServerInfo* get_server_info(const MYSQL_MONITOR* handle, const MXS_MONITORED_SERVER* db);
static bool mon_process_failover(MYSQL_MONITOR*, uint32_t, bool*);
static bool do_failover(MYSQL_MONITOR* mon, json_t** output);
static bool do_switchover(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* current_master,
                          MXS_MONITORED_SERVER* new_master, json_t** err_out);
static bool update_gtids(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER *database, MySqlServerInfo* info);
static bool update_replication_settings(MXS_MONITORED_SERVER *database, MySqlServerInfo* info);
static bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                          StringVector* output);
static void read_server_variables(MXS_MONITORED_SERVER* database, MySqlServerInfo* serv_info);
static bool server_is_rejoin_suspect(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* server,
                                     MySqlServerInfo* master_info);
static bool get_joinable_servers(MYSQL_MONITOR* mon, ServerVector* output);
static uint32_t do_rejoin(MYSQL_MONITOR* mon, const ServerVector& servers);
static bool join_cluster(MXS_MONITORED_SERVER* server, const char* change_cmd);
static void disable_setting(MYSQL_MONITOR* mon, const char* setting);
static bool cluster_can_be_joined(MYSQL_MONITOR* mon);
static bool can_replicate_from(MYSQL_MONITOR* mon,
                               MXS_MONITORED_SERVER* slave, MySqlServerInfo* slave_info,
                               MXS_MONITORED_SERVER* master, MySqlServerInfo* master_info);
static bool wait_cluster_stabilization(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* new_master,
                                       const ServerVector& slaves, int seconds_remaining);
static string get_connection_errors(const ServerVector& servers);
static int64_t scan_server_id(const char* id_string);

static bool report_version_err = true;
static const char* hb_table_name = "maxscale_schema.replication_heartbeat";

static const char CN_AUTO_FAILOVER[]      = "auto_failover";
static const char CN_FAILOVER_TIMEOUT[]   = "failover_timeout";
static const char CN_SWITCHOVER_TIMEOUT[] = "switchover_timeout";
static const char CN_AUTO_REJOIN[]        = "auto_rejoin";
static const char CN_FAILCOUNT[]          = "failcount";
static const char CN_NO_PROMOTE_SERVERS[] = "servers_no_promotion";

// Parameters for master failure verification and timeout
static const char CN_VERIFY_MASTER_FAILURE[]    = "verify_master_failure";
static const char CN_MASTER_FAILURE_TIMEOUT[]   = "master_failure_timeout";

// Replication credentials parameters for failover/switchover/join
static const char CN_REPLICATION_USER[]     = "replication_user";
static const char CN_REPLICATION_PASSWORD[] = "replication_password";

/** Default failover timeout */
#define DEFAULT_FAILOVER_TIMEOUT "90"

/** Default switchover timeout */
#define DEFAULT_SWITCHOVER_TIMEOUT "90"

/** Default master failure verification timeout */
#define DEFAULT_MASTER_FAILURE_TIMEOUT "10"

/** Server id default value */
static const int64_t SERVER_ID_UNKNOWN = -1;

class Gtid
{
public:
    uint32_t domain;
    int64_t server_id; // Is actually 32bit unsigned. 0 is only used by server versions  <= 10.1
    uint64_t sequence;
    Gtid()
        : domain(0)
        , server_id(SERVER_ID_UNKNOWN)
        , sequence(0)
    {}

    /**
     * Parse a Gtid-triplet from a string. In case of a multi-triplet value, only the triplet with
     * the given domain is returned.
     *
     * @param str Gtid string
     * @param search_domain The Gtid domain whose triplet should be returned. Negative domain stands for
     * autoselect, which is only allowed when the string contains one triplet.
     */
    Gtid(const char* str, int64_t search_domain = -1)
        : domain(0)
        , server_id(SERVER_ID_UNKNOWN)
        , sequence(0)
    {
        // Autoselect only allowed with one triplet
        ss_dassert(search_domain >= 0 || strchr(str, ',') == NULL);
        parse_triplet(str);
        if (search_domain >= 0 && domain != search_domain)
        {
            // Search for the correct triplet.
            bool found = false;
            for (const char* next_triplet = strchr(str, ',');
                 next_triplet != NULL && !found;
                 next_triplet = strchr(next_triplet, ','))
            {
                parse_triplet(++next_triplet);
                if (domain == search_domain)
                {
                    found = true;
                }
            }
            ss_dassert(found);
        }
    }
    bool operator == (const Gtid& rhs) const
    {
        return domain == rhs.domain &&
            server_id != SERVER_ID_UNKNOWN && server_id == rhs.server_id &&
            sequence == rhs.sequence;
    }
    string to_string() const
    {
        std::stringstream ss;
        if (server_id != SERVER_ID_UNKNOWN)
        {
            ss << domain << "-" << server_id << "-" << sequence;
        }
        return ss.str();
    }
private:
    void parse_triplet(const char* str)
    {
        ss_debug(int rv = ) sscanf(str, "%" PRIu32 "-%" PRId64 "-%" PRIu64, &domain, &server_id, &sequence);
        ss_dassert(rv == 3);
    }
};

// Contains data returned by one row of SHOW ALL SLAVES STATUS
class SlaveStatusInfo
{
public:
    int64_t master_server_id;       /**< The master's server_id value. Valid ids are 32bit unsigned. -1 is
                                     *   unread/error. */
    string master_host;             /**< Master server host name. */
    int master_port;                /**< Master server port. */
    bool slave_io_running;          /**< Whether the slave I/O thread is running and connected. */
    bool slave_sql_running;         /**< Whether or not the SQL thread is running. */
    string master_log_file;         /**< Name of the master binary log file that the I/O thread is currently
                                     *   reading from. */
    uint64_t read_master_log_pos;   /**< Position up to which the I/O thread has read in the current master
                                     *   binary log file. */
    Gtid gtid_io_pos;               /**< Gtid I/O position of the slave thread. Only shows the triplet with
                                     *   the current master domain. */
    string last_error;              /**< Last IO or SQL error encountered. */

    SlaveStatusInfo()
        : master_server_id(SERVER_ID_UNKNOWN)
        , master_port(0)
        , slave_io_running(false)
        , slave_sql_running(false)
        , read_master_log_pos(0)
    {}
};

// This class groups some miscellaneous replication related settings together.
class ReplicationSettings
{
public:
    bool gtid_strict_mode;      /**< Enable additional checks for replication */
    bool log_bin;               /**< Is binary logging enabled */
    bool log_slave_updates;     /**< Does the slave log replicated events to binlog */
    ReplicationSettings()
        : gtid_strict_mode(false)
        , log_bin(false)
        , log_slave_updates(false)
    {}
};

/**
 * Monitor specific information about a server
 *
 * Note: These are initialized in @c init_server_info
 */
class MySqlServerInfo
{
public:
    int64_t          server_id;             /**< Value of @@server_id. Valid values are 32bit unsigned. */
    int              group;                 /**< Multi-master group where this server belongs,
                                             *   0 for servers not in groups */
    bool             read_only;             /**< Value of @@read_only */
    bool             slave_configured;      /**< Whether SHOW SLAVE STATUS returned rows */
    bool             binlog_relay;          /** Server is a Binlog Relay */
    int              n_slaves_configured;   /**< Number of configured slave connections*/
    int              n_slaves_running;      /**< Number of running slave connections */
    int              slave_heartbeats;      /**< Number of received heartbeats */
    double           heartbeat_period;      /**< The time interval between heartbeats */
    time_t           latest_event;          /**< Time when latest event was received from the master */
    int64_t          gtid_domain_id;        /**< The value of gtid_domain_id, the domain which is used for
                                             *   new non-replicated events. */
    Gtid             gtid_current_pos;      /**< Gtid of latest event. Only shows the triplet
                                             *   with the current master domain. */
    Gtid             gtid_binlog_pos;       /**< Gtid of latest event written to binlog. Only shows
                                             *   the triplet with the current master domain. */
    SlaveStatusInfo  slave_status;          /**< Data returned from SHOW SLAVE STATUS */
    ReplicationSettings rpl_settings;       /**< Miscellaneous replication related settings */
    mysql_server_version version;           /**< Server version, 10.X, 5.5 or 5.1 */

    MySqlServerInfo()
        : server_id(SERVER_ID_UNKNOWN)
        , group(0)
        , read_only(false)
        , slave_configured(false)
        , binlog_relay(false)
        , n_slaves_configured(0)
        , n_slaves_running(0)
        , slave_heartbeats(0)
        , heartbeat_period(0)
        , latest_event(0)
        , gtid_domain_id(-1)
        , version(MYSQL_SERVER_VERSION_51)
    {}

    /**
     * Calculate how many events are left in the relay log. If gtid_current_pos is ahead of Gtid_IO_Pos,
     * or a server_id is unknown, an error value is returned.
     *
     * @return Number of events in relay log according to latest queried info. A negative value signifies
     * an error in the gtid-values.
     */
    int64_t relay_log_events()
    {
        if (slave_status.gtid_io_pos.server_id != SERVER_ID_UNKNOWN &&
            gtid_current_pos.server_id != SERVER_ID_UNKNOWN &&
            slave_status.gtid_io_pos.domain == gtid_current_pos.domain &&
            slave_status.gtid_io_pos.sequence >= gtid_current_pos.sequence)
        {
            return slave_status.gtid_io_pos.sequence - gtid_current_pos.sequence;
        }
        return -1;
    }
};

bool uses_gtid(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* mon_server, json_t** error_out)
{
    bool rval = false;
    const MySqlServerInfo* info = get_server_info(mon, mon_server);
    if (info->slave_status.gtid_io_pos.server_id == SERVER_ID_UNKNOWN)
    {
        string slave_not_gtid_msg = string("Slave server ") + mon_server->server->unique_name +
                                    " is not using gtid replication.";
        PRINT_MXS_JSON_ERROR(error_out, "%s", slave_not_gtid_msg.c_str());
    }
    else
    {
        rval = true;
    }
    return rval;
}
/**
 * Check that the given server is a master and it's the only master.
 *
 * @param mon                       Cluster monitor.
 * @param suggested_curr_master     The server to check, given by user.
 * @param error_out                 On output, error object if function failed.
 *
 * @return False, if there is some error with the specified current master,
 *         True otherwise.
 */
bool mysql_switchover_check_current(const MYSQL_MONITOR* mon,
                                    const MXS_MONITORED_SERVER* suggested_curr_master,
                                    json_t** error_out)
{
    bool server_is_master = false;
    MXS_MONITORED_SERVER* extra_master = NULL; // A master server which is not the suggested one
    for (MXS_MONITORED_SERVER* mon_serv = mon->monitor->monitored_servers;
         mon_serv != NULL && extra_master == NULL;
         mon_serv = mon_serv->next)
    {
        if (SERVER_IS_MASTER(mon_serv->server))
        {
            if (mon_serv == suggested_curr_master)
            {
                server_is_master = true;
            }
            else
            {
                extra_master = mon_serv;
            }
        }
    }

    if (!server_is_master)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Server '%s' is not the current master or it's in maintenance.",
                             suggested_curr_master->server->unique_name);
    }
    else if (extra_master)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Cluster has an additional master server '%s'.",
                             extra_master->server->unique_name);
    }
    return server_is_master && !extra_master;
}

/**
 * Check whether specified new master is acceptable.
 *
 * @param monitored_server      The server to check against.
 * @param error                 On output, error object if function failed.
 *
 * @return True, if suggested new master is a viable promotion candidate.
 */
bool mysql_switchover_check_new(const MXS_MONITORED_SERVER* monitored_server, json_t** error)
{
    SERVER* server = monitored_server->server;
    const char* name = server->unique_name;
    bool is_master = SERVER_IS_MASTER(server);
    bool is_slave = SERVER_IS_SLAVE(server);

    if (is_master)
    {
        const char IS_MASTER[] = "Specified new master '%s' is already the current master.";
        PRINT_MXS_JSON_ERROR(error, IS_MASTER, name);
    }
    else if (!is_slave)
    {
        const char NOT_SLAVE[] = "Specified new master '%s' is not a slave.";
        PRINT_MXS_JSON_ERROR(error, NOT_SLAVE, name);
    }

    return !is_master && is_slave;
}

/**
 * Check that preconditions for a failover are met.
 *
 * @param mon Cluster monitor
 * @param error_out JSON error out
 * @return True if failover may proceed
 */
bool failover_check(MYSQL_MONITOR* mon, json_t** error_out)
{
    // Check that there is no running master and that there is at least one running server in the cluster.
    // Also, all slaves must be using gtid-replication.
    int slaves = 0;
    bool error = false;

    for (MXS_MONITORED_SERVER* mon_server = mon->monitor->monitored_servers;
         mon_server != NULL;
         mon_server = mon_server->next)
    {
        uint64_t status_bits = mon_server->server->status;
        uint64_t master_up = (SERVER_MASTER | SERVER_RUNNING);
        if ((status_bits & master_up) == master_up)
        {
            string master_up_msg = string("Master server '") + mon_server->server->unique_name +
                                   "' is running";
            if (status_bits & SERVER_MAINT)
            {
                master_up_msg += ", although in maintenance mode";
            }
            master_up_msg += ".";
            PRINT_MXS_JSON_ERROR(error_out, "%s", master_up_msg.c_str());
            error = true;
        }
        else if (SERVER_IS_SLAVE(mon_server->server))
        {
            if (uses_gtid(mon, mon_server, error_out))
            {
                 slaves++;
            }
            else
            {
                 error = true;
            }
        }
    }

    if (error)
    {
        PRINT_MXS_JSON_ERROR(error_out, "Failover not allowed due to errors.");
    }
    else if (slaves == 0)
    {
        PRINT_MXS_JSON_ERROR(error_out, "No running slaves, cannot failover.");
    }
    return !error && slaves > 0;
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
bool mysql_switchover(MXS_MONITOR* mon, MXS_MONITORED_SERVER* new_master, MXS_MONITORED_SERVER* current_master, json_t** error_out)
{
    bool stopped = stop_monitor(mon);
    if (stopped)
    {
        MXS_NOTICE("Stopped the monitor %s for the duration of switchover.", mon->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, switchover can proceed.", mon->name);
    }

    bool rval = false;
    MYSQL_MONITOR* handle = static_cast<MYSQL_MONITOR*>(mon->handle);

    bool current_ok = mysql_switchover_check_current(handle, current_master, error_out);
    bool new_ok = mysql_switchover_check_new(new_master, error_out);
    // Check that all slaves are using gtid-replication
    bool gtid_ok = true;
    for (MXS_MONITORED_SERVER* mon_serv = mon->monitored_servers; mon_serv != NULL; mon_serv = mon_serv->next)
    {
        if (SERVER_IS_SLAVE(mon_serv->server))
        {
            if (!uses_gtid(handle, mon_serv, error_out))
            {
                 gtid_ok = false;
            }
        }
    }

    if (current_ok && new_ok && gtid_ok)
    {
        bool switched = do_switchover(handle, current_master, new_master, error_out);

        const char* curr_master_name = current_master->server->unique_name;
        const char* new_master_name = new_master->server->unique_name;

        if (switched)
        {
            MXS_NOTICE("Switchover %s -> %s performed.", curr_master_name, new_master_name);
            rval = true;
        }
        else
        {
            string format = "Switchover %s -> %s failed";
            bool failover = config_get_bool(mon->parameters, CN_AUTO_FAILOVER);
            if (failover)
            {
                disable_setting(handle, CN_AUTO_FAILOVER);
                format += ", failover has been disabled.";
            }
            format += ".";
            PRINT_MXS_JSON_ERROR(error_out, format.c_str(), curr_master_name, new_master_name);
        }
    }

    if (stopped)
    {
        startMonitor(mon, mon->parameters);
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
bool mysql_handle_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    ss_dassert((args->argc == 2) || (args->argc == 3));
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);
    ss_dassert((args->argc == 2) || (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER));

    MXS_MONITOR* mon = args->argv[0].value.monitor;
    SERVER* new_master = args->argv[1].value.server;
    SERVER* current_master = (args->argc == 3) ? args->argv[2].value.server : NULL;
    bool error = false;

    const char NO_SERVER[] = "Server '%s' is not a member of monitor '%s'.";
    MXS_MONITORED_SERVER* mon_new_master = mon_get_monitored_server(mon, new_master);
    if (mon_new_master == NULL)
    {
        PRINT_MXS_JSON_ERROR(error_out, NO_SERVER, new_master->unique_name, mon->name);
        error = true;
    }

    MXS_MONITORED_SERVER* mon_curr_master = NULL;
    if (current_master)
    {
        mon_curr_master = mon_get_monitored_server(mon, current_master);
        if (mon_curr_master == NULL)
        {
            PRINT_MXS_JSON_ERROR(error_out, NO_SERVER, current_master->unique_name, mon->name);
             error = true;
        }
    }
    else
    {
        // Autoselect current master
        MYSQL_MONITOR* handle = static_cast<MYSQL_MONITOR*>(mon->handle);
        if (handle->master)
        {
            mon_curr_master = handle->master;
        }
        else
        {
            const char NO_MASTER[] = "Monitor '%s' has no master server.";
            PRINT_MXS_JSON_ERROR(error_out, NO_MASTER, mon->name);
            error = true;
        }
    }
    if (error)
    {
        return false;
    }

    bool rval = false;
    if (!config_get_global_options()->passive)
    {
        rval = mysql_switchover(mon, mon_new_master, mon_curr_master, error_out);
    }
    else
    {
        const char MSG[] = "Switchover attempted but not performed, as MaxScale is in passive mode.";
        PRINT_MXS_JSON_ERROR(error_out, MSG);
    }

    return rval;
}

/**
 * Perform user-activated failover
 *
 * @param mon     Cluster monitor
 * @param output  Json error output
 * @return True on success
 */
bool mysql_failover(MXS_MONITOR* mon, json_t** output)
{
    bool stopped = stop_monitor(mon);
    if (stopped)
    {
        MXS_NOTICE("Stopped monitor %s for the duration of failover.", mon->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, failover can proceed.", mon->name);
    }

    bool rv = true;
    MYSQL_MONITOR *handle = static_cast<MYSQL_MONITOR*>(mon->handle);
    rv = failover_check(handle, output);
    if (rv)
    {
        rv = do_failover(handle, output);
        if (rv)
        {
            MXS_NOTICE("Failover performed.");
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output, "Failover failed.");
        }
    }

    if (stopped)
    {
        startMonitor(mon, mon->parameters);
    }
    return rv;
}

/**
 * Command handler for 'failover'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool mysql_handle_failover(const MODULECMD_ARG* args, json_t** output)
{
    ss_dassert(args->argc == 1);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);

    MXS_MONITOR* mon = args->argv[0].value.monitor;

    bool rv = false;
    if (!config_get_global_options()->passive)
    {
        rv = mysql_failover(mon, output);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(output, "Failover attempted but not performed, as MaxScale is in passive mode.");
    }
    return rv;
}

/**
 * Perform user-activated rejoin
 *
 * @param mon               Cluster monitor
 * @param rejoin_server     Server to join
 * @param output            Json error output
 * @return True on success
 */
bool mysql_rejoin(MXS_MONITOR* mon, SERVER* rejoin_server, json_t** output)
{
    bool stopped = stop_monitor(mon);
    if (stopped)
    {
        MXS_NOTICE("Stopped monitor %s for the duration of rejoin.", mon->name);
    }
    else
    {
        MXS_NOTICE("Monitor %s already stopped, rejoin can proceed.", mon->name);
    }

    bool rval = false;
    MYSQL_MONITOR *handle = static_cast<MYSQL_MONITOR*>(mon->handle);
    if (cluster_can_be_joined(handle))
    {
        MXS_MONITORED_SERVER* mon_server = mon_get_monitored_server(mon, rejoin_server);
        if (mon_server)
        {
            MXS_MONITORED_SERVER* master = handle->master;
            MySqlServerInfo* master_info = get_server_info(handle, master);
            MySqlServerInfo* server_info = get_server_info(handle, mon_server);

            if (server_is_rejoin_suspect(handle, mon_server, master_info) &&
                update_gtids(handle, master, master_info) &&
                can_replicate_from(handle, mon_server, server_info, master, master_info))
            {
                ServerVector joinable_server;
                joinable_server.push_back(mon_server);
                if (do_rejoin(handle, joinable_server) == 1)
                {
                    rval = true;
                    MXS_NOTICE("Rejoin performed.");
                }
                else
                {
                    PRINT_MXS_JSON_ERROR(output, "Rejoin attempted but failed.");
                }
            }
            else
            {
                PRINT_MXS_JSON_ERROR(output, "Server is not eligible for rejoin or eligibility could not be "
                                     "ascertained.");
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(output, "The given server '%s' is not monitored by this monitor.",
                                 rejoin_server->unique_name);
        }
    }
    else
    {
        const char BAD_CLUSTER[] = "The server cluster of monitor '%s' is not in a state valid for joining. "
                                   "Either it has no master or its gtid domain is unknown.";
        PRINT_MXS_JSON_ERROR(output, BAD_CLUSTER, mon->name);
    }

    if (stopped)
    {
        startMonitor(mon, mon->parameters);
    }
    return rval;
}

/**
 * Command handler for 'rejoin'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool mysql_handle_rejoin(const MODULECMD_ARG* args, json_t** output)
{
    ss_dassert(args->argc == 2);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    ss_dassert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    MXS_MONITOR* mon = args->argv[0].value.monitor;
    SERVER* server = args->argv[1].value.server;

    bool rv = false;
    if (!config_get_global_options()->passive)
    {
        rv = mysql_rejoin(mon, server, output);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(output, "Rejoin attempted but not performed, as MaxScale is in passive mode.");
    }
    return rv;
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
        static const char ARG_MONITOR_DESC[] = "MySQL Monitor name (from configuration file)";
        static modulecmd_arg_type_t switchover_argv[] =
        {
            {
                MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
                ARG_MONITOR_DESC
            },
            { MODULECMD_ARG_SERVER, "New master" },
            { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Current master (optional)" }
        };

        modulecmd_register_command(MXS_MODULE_NAME, "switchover", MODULECMD_TYPE_ACTIVE,
                                   mysql_handle_switchover, MXS_ARRAY_NELEMS(switchover_argv),
                                   switchover_argv, "Perform master switchover");

        static modulecmd_arg_type_t failover_argv[] =
        {
            {
                MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
                ARG_MONITOR_DESC
            },
        };

        modulecmd_register_command(MXS_MODULE_NAME, "failover", MODULECMD_TYPE_ACTIVE,
                                   mysql_handle_failover, MXS_ARRAY_NELEMS(failover_argv),
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
                                   mysql_handle_rejoin, MXS_ARRAY_NELEMS(rejoin_argv),
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
                {CN_FAILOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, DEFAULT_FAILOVER_TIMEOUT},
                {CN_SWITCHOVER_TIMEOUT, MXS_MODULE_PARAM_COUNT, DEFAULT_SWITCHOVER_TIMEOUT},
                {CN_REPLICATION_USER, MXS_MODULE_PARAM_STRING},
                {CN_REPLICATION_PASSWORD, MXS_MODULE_PARAM_STRING},
                {CN_VERIFY_MASTER_FAILURE, MXS_MODULE_PARAM_BOOL, "true"},
                {CN_MASTER_FAILURE_TIMEOUT, MXS_MODULE_PARAM_COUNT, DEFAULT_MASTER_FAILURE_TIMEOUT},
                {CN_AUTO_REJOIN, MXS_MODULE_PARAM_BOOL, "false"},
                {CN_NO_PROMOTE_SERVERS, MXS_MODULE_PARAM_SERVERLIST},
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }

}

void* info_copy_func(const void *val)
{
    ss_dassert(val);
    MySqlServerInfo *old_val = (MySqlServerInfo*)val;
    MySqlServerInfo *new_val = new (std::nothrow) MySqlServerInfo;

    if (new_val)
    {
        *new_val = *old_val;
    }
    return new_val;
}

void info_free_func(void *val)
{
    if (val)
    {
        MySqlServerInfo *old_val = (MySqlServerInfo*)val;
        delete old_val;
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

    MySqlServerInfo info;

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

static MySqlServerInfo* get_server_info(const MYSQL_MONITOR* handle, const  MXS_MONITORED_SERVER* db)
{
    void* value = hashtable_fetch(handle->server_info, db->server->unique_name);
    ss_dassert(value);
    return static_cast<MySqlServerInfo*>(value);
}

static bool set_replication_credentials(MYSQL_MONITOR *handle, const MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    const char* repl_user = config_get_string(params, CN_REPLICATION_USER);
    const char* repl_pw = config_get_string(params, CN_REPLICATION_PASSWORD);

    if (!*repl_user && !*repl_pw)
    {
        // No replication credentials defined, use monitor credentials
        repl_user = handle->monitor->user;
        repl_pw = handle->monitor->password;
    }

    if (*repl_user && *repl_pw)
    {
        handle->replication_user = MXS_STRDUP_A(repl_user);
        handle->replication_password = decrypt_password(repl_pw);
        rval = true;
    }

    return rval;
}

/**
 * Is the server in the excluded list
 *
 * @param handle Cluster monitor
 * @param server Server to test
 * @return True if server is in the excluded-list of the monitor.
 */
static bool server_is_excluded(const MYSQL_MONITOR *handle, const MXS_MONITORED_SERVER* server)
{
    for (int i = 0; i < handle->n_excluded; i++)
    {
        if (handle->excluded_servers[i] == server)
        {
            return true;
        }
    }
    return false;
}

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
    bool error = false;
    MYSQL_MONITOR *handle = (MYSQL_MONITOR*) monitor->handle;
    if (handle)
    {
        handle->shutdown = 0;
        MXS_FREE(handle->script);
        MXS_FREE(handle->replication_user);
        MXS_FREE(handle->replication_password);
        MXS_FREE(handle->excluded_servers);
        handle->excluded_servers = NULL;
        handle->n_excluded = 0;
    }
    else
    {
        handle = (MYSQL_MONITOR *) MXS_MALLOC(sizeof(MYSQL_MONITOR));
        HASHTABLE *server_info = hashtable_alloc(MAX_NUM_SLAVES,
                                                 hashtable_item_strhash, hashtable_item_strcmp);

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
        handle->warn_set_standalone_master = true;
        handle->master_gtid_domain = -1;
        handle->monitor = monitor;
    }

    /** This should always be reset to NULL */
    handle->master = NULL;

    handle->detectStaleMaster = config_get_bool(params, "detect_stale_master");
    handle->detectStaleSlave = config_get_bool(params, "detect_stale_slave");
    handle->replicationHeartbeat = config_get_bool(params, "detect_replication_lag");
    handle->multimaster = config_get_bool(params, "multimaster");
    handle->ignore_external_masters = config_get_bool(params, "ignore_external_masters");
    handle->detect_standalone_master = config_get_bool(params, "detect_standalone_master");
    handle->failcount = config_get_integer(params, CN_FAILCOUNT);
    handle->allow_cluster_recovery = config_get_bool(params, "allow_cluster_recovery");
    handle->mysql51_replication = config_get_bool(params, "mysql51_replication");
    handle->script = config_copy_string(params, "script");
    handle->events = config_get_enum(params, "events", mxs_monitor_event_enum_values);
    handle->auto_failover = config_get_bool(params, CN_AUTO_FAILOVER);
    handle->failover_timeout = config_get_integer(params, CN_FAILOVER_TIMEOUT);
    handle->switchover_timeout = config_get_integer(params, CN_SWITCHOVER_TIMEOUT);
    handle->verify_master_failure = config_get_bool(params, CN_VERIFY_MASTER_FAILURE);
    handle->master_failure_timeout = config_get_integer(params, CN_MASTER_FAILURE_TIMEOUT);
    handle->auto_rejoin = config_get_bool(params, CN_AUTO_REJOIN);

    handle->excluded_servers = NULL;
    handle->n_excluded = mon_config_get_servers(params, CN_NO_PROMOTE_SERVERS, monitor,
                                                &handle->excluded_servers);
    if (handle->n_excluded < 0)
    {
        error = true;
    }

    if (!set_replication_credentials(handle, params))
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", CN_REPLICATION_USER, CN_REPLICATION_PASSWORD);
        error = true;
    }

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
        MXS_FREE(handle->excluded_servers);
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

    dcb_printf(dcb, "Automatic failover:\t%s\n", handle->auto_failover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Failcount:\t\t%d\n", handle->failcount);
    dcb_printf(dcb, "Failover Timeout:\t%u\n", handle->failover_timeout);
    dcb_printf(dcb, "Switchover Timeout:\t%u\n", handle->switchover_timeout);
    dcb_printf(dcb, "Auto rejoin:\t\t%s\n", handle->auto_rejoin ? "Enabled" : "Disabled");
    dcb_printf(dcb, "MaxScale MonitorId:\t%lu\n", handle->id);
    dcb_printf(dcb, "Replication lag:\t%s\n", (handle->replicationHeartbeat == 1) ? "enabled" : "disabled");
    dcb_printf(dcb, "Detect Stale Master:\t%s\n", (handle->detectStaleMaster == 1) ? "enabled" : "disabled");
    dcb_printf(dcb, "Server information\n\n");

    for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db; db = db->next)
    {
        MySqlServerInfo *serv_info = get_server_info(handle, db);
        dcb_printf(dcb, "Server: %s\n", db->server->unique_name);
        dcb_printf(dcb, "Server ID: %" PRId64 "\n", serv_info->server_id);
        dcb_printf(dcb, "Read only: %s\n", serv_info->read_only ? "ON" : "OFF");
        dcb_printf(dcb, "Slave configured: %s\n", serv_info->slave_configured ? "YES" : "NO");
        dcb_printf(dcb, "Slave IO running: %s\n", serv_info->slave_status.slave_io_running ? "YES" : "NO");
        dcb_printf(dcb, "Slave SQL running: %s\n", serv_info->slave_status.slave_sql_running ? "YES" : "NO");
        dcb_printf(dcb, "Master ID: %" PRId64 "\n", serv_info->slave_status.master_server_id);
        dcb_printf(dcb, "Master binlog file: %s\n", serv_info->slave_status.master_log_file.c_str());
        dcb_printf(dcb, "Master binlog position: %lu\n", serv_info->slave_status.read_master_log_pos);
        if (serv_info->slave_status.gtid_io_pos.server_id != SERVER_ID_UNKNOWN)
        {
            dcb_printf(dcb, "Gtid_IO_Pos: %s\n", serv_info->slave_status.gtid_io_pos.to_string().c_str());
        }
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
    json_object_set_new(rval, CN_FAILCOUNT, json_integer(handle->failcount));
    json_object_set_new(rval, "allow_cluster_recovery", json_boolean(handle->allow_cluster_recovery));
    json_object_set_new(rval, "mysql51_replication", json_boolean(handle->mysql51_replication));
    json_object_set_new(rval, CN_AUTO_FAILOVER, json_boolean(handle->auto_failover));
    json_object_set_new(rval, CN_FAILOVER_TIMEOUT, json_integer(handle->failover_timeout));
    json_object_set_new(rval, CN_SWITCHOVER_TIMEOUT, json_integer(handle->switchover_timeout));
    json_object_set_new(rval, CN_AUTO_REJOIN, json_boolean(handle->auto_rejoin));

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
            MySqlServerInfo *serv_info = get_server_info(handle, db);
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
            if (serv_info->slave_status.gtid_io_pos.server_id != SERVER_ID_UNKNOWN)
            {
                json_object_set_new(srv, "gtid_io_pos",
                                    json_string(serv_info->slave_status.gtid_io_pos.to_string().c_str()));
            }
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

static enum mysql_server_version get_server_version(MXS_MONITORED_SERVER* db)
{
    unsigned long server_version = mysql_get_server_version(db->con);

    if (server_version >= 100000)
    {
        return MYSQL_SERVER_VERSION_100;
    }
    else if (server_version >= 5 * 10000 + 5 * 100)
    {
        return MYSQL_SERVER_VERSION_55;
    }

    return MYSQL_SERVER_VERSION_51;
}

static bool do_show_slave_status(MYSQL_MONITOR* mon,
                                 MySqlServerInfo* serv_info,
                                 MXS_MONITORED_SERVER* database)
{
    bool rval = true;
    unsigned int columns;
    int i_slave_io_running, i_slave_sql_running, i_read_master_log_pos, i_master_server_id, i_master_log_file;
    const char *query;
    mysql_server_version server_version = serv_info->version;

    if (server_version == MYSQL_SERVER_VERSION_100)
    {
        columns = 42;
        query = "SHOW ALL SLAVES STATUS";
        i_slave_io_running = MARIA10_STATUS_IO_RUNNING;
        i_slave_sql_running = MARIA10_STATUS_SQL_RUNNING;
        i_master_log_file = MARIA10_STATUS_MASTER_LOG_FILE;
        i_read_master_log_pos = MARIA10_STATUS_MASTER_LOG_POS;
        i_master_server_id = MARIA10_STATUS_MASTER_ID;
    }
    else
    {
        columns = server_version == MYSQL_SERVER_VERSION_55 ? 40 : 38;
        query = "SHOW SLAVE STATUS";
        i_slave_io_running = MYSQL55_STATUS_IO_RUNNING;
        i_slave_sql_running = MYSQL55_STATUS_SQL_RUNNING;
        i_master_log_file = MYSQL55_STATUS_MASTER_LOG_FILE;
        i_read_master_log_pos = MYSQL55_STATUS_MASTER_LOG_POS;
        i_master_server_id = MYSQL55_STATUS_MASTER_ID;
    }

    MYSQL_RES* result;
    int64_t master_server_id = SERVER_ID_UNKNOWN;
    int nconfigured = 0;
    int nrunning = 0;

    if (mxs_mysql_query(database->con, query) == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < columns)
        {
            mysql_free_result(result);
            MXS_ERROR("\"%s\" returned less than the expected amount of columns. "
                      "Expected %u columns.", query, columns);
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(result);

        if (row)
        {
            serv_info->slave_configured = true;

            do
            {
                /* get Slave_IO_Running and Slave_SQL_Running values*/
                serv_info->slave_status.slave_io_running = strncmp(row[i_slave_io_running], "Yes", 3) == 0;
                serv_info->slave_status.slave_sql_running = strncmp(row[i_slave_sql_running], "Yes", 3) == 0;

                if (serv_info->slave_status.slave_io_running && serv_info->slave_status.slave_sql_running)
                {
                    if (nrunning == 0)
                    {
                        /** Only check binlog name for the first running slave */
                        uint64_t read_master_log_pos = atol(row[i_read_master_log_pos]);
                        char* master_log_file = row[i_master_log_file];
                        if (serv_info->slave_status.master_log_file != master_log_file ||
                            read_master_log_pos != serv_info->slave_status.read_master_log_pos)
                        {
                            // IO thread is reading events from the master
                            serv_info->latest_event = time(NULL);
                        }

                        serv_info->slave_status.master_log_file = master_log_file;
                        serv_info->slave_status.read_master_log_pos = read_master_log_pos;
                    }

                    nrunning++;
                }

                /* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building
                 * the replication tree, slaves ids will be added to master(s) and we will have at least the
                 * root master server.
                 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
                 */
                if (serv_info->slave_status.slave_io_running && server_version != MYSQL_SERVER_VERSION_51)
                {
                    /* Get Master_Server_Id */
                    master_server_id = scan_server_id(row[i_master_server_id]);
                }

                if (server_version == MYSQL_SERVER_VERSION_100)
                {
                    const char* beats = mxs_mysql_get_value(result, row, "Slave_received_heartbeats");
                    const char* period = mxs_mysql_get_value(result, row, "Slave_heartbeat_period");
                    const char* using_gtid = mxs_mysql_get_value(result, row, "Using_Gtid");
                    const char* master_host = mxs_mysql_get_value(result, row, "Master_Host");
                    const char* master_port = mxs_mysql_get_value(result, row, "Master_Port");
                    const char* last_io_error = mxs_mysql_get_value(result, row, "Last_IO_Error");
                    const char* last_sql_error = mxs_mysql_get_value(result, row, "Last_SQL_Error");
                    ss_dassert(beats && period && using_gtid && master_host && master_port &&
                               last_io_error && last_sql_error);
                    serv_info->slave_status.master_host = master_host;
                    serv_info->slave_status.master_port = atoi(master_port);
                    serv_info->slave_status.last_error = *last_io_error ? last_io_error :
                                                         (*last_sql_error ? last_sql_error : "");

                    int heartbeats = atoi(beats);
                    if (serv_info->slave_heartbeats < heartbeats)
                    {
                        serv_info->latest_event = time(NULL);
                        serv_info->slave_heartbeats = heartbeats;
                        serv_info->heartbeat_period = atof(period);
                    }
                    if (mon->master_gtid_domain >= 0 &&
                        (strcmp(using_gtid, "Current_Pos") == 0 || strcmp(using_gtid, "Slave_Pos") == 0))
                    {
                        const char* gtid_io_pos = mxs_mysql_get_value(result, row, "Gtid_IO_Pos");
                        ss_dassert(gtid_io_pos);
                        serv_info->slave_status.gtid_io_pos = gtid_io_pos[0] != '\0' ?
                                                              Gtid(gtid_io_pos, mon->master_gtid_domain) :
                                                              Gtid();
                    }
                    else
                    {
                        serv_info->slave_status.gtid_io_pos = Gtid();
                    }
                }

                nconfigured++;
                row = mysql_fetch_row(result);
            }
            while (row);
        }
        else
        {
            /** Query returned no rows, replication is not configured */
            serv_info->slave_configured = false;
            serv_info->slave_heartbeats = 0;
            serv_info->slave_status = SlaveStatusInfo();
        }

        serv_info->slave_status.master_server_id = master_server_id;
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(database);
    }

    serv_info->n_slaves_configured = nconfigured;
    serv_info->n_slaves_running = nrunning;

    return rval;
}

/**
 * Check if a slave is receiving events from master.
 *
 * @param handle Cluster monitor
 * @return True, if a slave has an event more recent than master_failure_timeout.
 */
static bool slave_receiving_events(MYSQL_MONITOR* handle)
{
    ss_dassert(handle->master);
    bool received_event = false;
    int64_t master_id = handle->master->server->node_id;
    for (MXS_MONITORED_SERVER* server = handle->monitor->monitored_servers; server; server = server->next)
    {
        MySqlServerInfo* info = get_server_info(handle, server);

        if (info->slave_configured &&
            info->slave_status.master_server_id == master_id &&
            difftime(time(NULL), info->latest_event) < handle->master_failure_timeout)
        {
            /**
             * The slave is still connected to the correct master and has received events. This means that
             * while MaxScale can't connect to the master, it's probably still alive.
             */
            received_event = true;
            break;
        }
    }
    return received_event;
}

static inline void monitor_mysql_db(MYSQL_MONITOR* mon,
                                    MXS_MONITORED_SERVER* database,
                                    MySqlServerInfo *serv_info)
{
    /** Clear old states */
    monitor_clear_pending_status(database, SERVER_SLAVE | SERVER_MASTER | SERVER_RELAY_MASTER |
                                 SERVER_SLAVE_OF_EXTERNAL_MASTER);

    if (do_show_slave_status(mon, serv_info, database))
    {
        /* If all configured slaves are running set this node as slave */
        if (serv_info->slave_configured && serv_info->n_slaves_running > 0 &&
            serv_info->n_slaves_running == serv_info->n_slaves_configured)
        {
            monitor_set_pending_status(database, SERVER_SLAVE);
        }

        /** Store master_id of current node. For MySQL 5.1 it will be set at a later point. */
        database->server->master_id = serv_info->slave_status.master_server_id;
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
            if (mxs_mysql_query(database->con, "SHOW SLAVE HOSTS") == 0
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

            monitor_set_pending_status(database, SERVER_SLAVE);
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
        /**
         * The current server is not running. Clear all but the stale master bit
         * as it is used to detect masters that went down but came up.
         */
        unsigned int all_bits = ~SERVER_STALE_STATUS;
        server_clear_status_nolock(database->server, all_bits);
        monitor_clear_pending_status(database, all_bits);

        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(database->server, SERVER_AUTH_ERROR);
            monitor_set_pending_status(database, SERVER_AUTH_ERROR);
        }

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

    MySqlServerInfo *serv_info = get_server_info(handle, database);
    /* Check whether current server is MaxScale Binlog Server */
    MYSQL_RES *result;
    if (mxs_mysql_query(database->con, "SELECT @@maxscale_version") == 0 &&
        (result = mysql_store_result(database->con)) != NULL)
    {
        serv_info->binlog_relay = true;
        mysql_free_result(result);
    }
    else
    {
        serv_info->binlog_relay = false;
    }

    /* Get server version string, also get/set numeric representation. */
    mxs_mysql_set_server_version(database->con, database->server);
    /* Set monitor version enum. */
    uint64_t version_num = server_get_version(database->server);
    if (version_num >= 100000)
    {
        serv_info->version = MYSQL_SERVER_VERSION_100;
    }
    else if (version_num >= 5 * 10000 + 5 * 100)
    {
        serv_info->version = MYSQL_SERVER_VERSION_55;
    }
    else
    {
        serv_info->version = MYSQL_SERVER_VERSION_51;
    }
    /* Query a few settings. */
    read_server_variables(database, serv_info);
    /* Check for MariaDB 10.x.x and get status for multi-master replication */
    if (serv_info->version == MYSQL_SERVER_VERSION_100 || serv_info->version == MYSQL_SERVER_VERSION_55)
    {
        monitor_mysql_db(handle, database, serv_info);
    }
    else
    {
        if (handle->mysql51_replication)
        {
            monitor_mysql_db(handle, database, serv_info);
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
    MySqlServerInfo *info;
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
        graph[nodes].info = get_server_info(handle, db);
        graph[nodes].db = db;
        graph[nodes].index = graph[nodes].lowest_index = 0;
        graph[nodes].cycle = 0;
        graph[nodes].active = false;
        graph[nodes].parent = NULL;
        nodes++;
    }

    /** Build the graph */
    for (int i = 0; i < nservers; i++)
    {
        if (graph[i].info->slave_status.master_server_id > 0)
        {
            /** Found a connected node */
            for (int k = 0; k < nservers; k++)
            {
                if (graph[k].info->server_id == graph[i].info->slave_status.master_server_id)
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
                monitor_set_pending_status(graph[i].db, SERVER_SLAVE | SERVER_STALE_SLAVE);
                monitor_clear_pending_status(graph[i].db, SERVER_MASTER);
            }
            else
            {
                monitor_set_pending_status(graph[i].db, SERVER_MASTER);
                monitor_clear_pending_status(graph[i].db, SERVER_SLAVE | SERVER_STALE_SLAVE);
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
                monitor_set_pending_status(graph[i].db, SERVER_SLAVE | SERVER_STALE_SLAVE);
                monitor_clear_pending_status(graph[i].db, SERVER_MASTER | SERVER_STALE_STATUS);
            }
            else
            {
                monitor_set_pending_status(graph[i].db, SERVER_MASTER | SERVER_STALE_STATUS);
                monitor_clear_pending_status(graph[i].db, SERVER_SLAVE | SERVER_STALE_SLAVE);
            }
        }
    }
}

/**
 * @brief Check whether standalone master conditions have been met
 *
 * This function checks whether all the conditions to use a standalone master have
 * been met. For this to happen, only one server must be available and
 * other servers must have passed the configured tolerance level of failures.
 *
 * @param handle Monitor instance
 * @param db     Monitor servers
 *
 * @return True if standalone master should be used
 */
bool standalone_master_required(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *db)
{
    int candidates = 0;

    while (db)
    {
        if (SERVER_IS_RUNNING(db->server))
        {
            candidates++;
            MySqlServerInfo *server_info = get_server_info(handle, db);

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
 * @brief Use standalone master
 *
 * This function assigns the last remaining server the master status and sets all other
 * servers into maintenance mode. By setting the servers into maintenance mode, we
 * prevent any possible conflicts when the failed servers come back up.
 *
 * @param handle Monitor instance
 * @param db     Monitor servers
 */
bool set_standalone_master(MYSQL_MONITOR *handle, MXS_MONITORED_SERVER *db)
{
    bool rval = false;

    while (db)
    {
        if (SERVER_IS_RUNNING(db->server))
        {
            if (!SERVER_IS_MASTER(db->server) && handle->warn_set_standalone_master)
            {
                MXS_WARNING("Setting standalone master, server '%s' is now the master.%s",
                            db->server->unique_name,
                            handle->allow_cluster_recovery ?
                            "" : " All other servers are set into maintenance mode.");
                handle->warn_set_standalone_master = false;
            }

            server_clear_set_status(db->server, SERVER_SLAVE, SERVER_MASTER | SERVER_STALE_STATUS);
            monitor_set_pending_status(db, SERVER_MASTER | SERVER_STALE_STATUS);
            monitor_clear_pending_status(db, SERVER_SLAVE);
            handle->master = db;
            rval = true;
        }
        else if (!handle->allow_cluster_recovery)
        {
            server_set_status_nolock(db->server, SERVER_MAINT);
            monitor_set_pending_status(db, SERVER_MAINT);
        }
        db = db->next;
    }

    return rval;
}

bool failover_not_possible(MYSQL_MONITOR* handle)
{
    bool rval = false;

    for (MXS_MONITORED_SERVER* s = handle->monitor->monitored_servers; s; s = s->next)
    {
        MySqlServerInfo* info = get_server_info(handle, s);

        if (info->n_slaves_configured > 1)
        {
            MXS_ERROR("Server '%s' is configured to replicate from multiple "
                      "masters, failover is not possible.", s->server->unique_name);
            rval = true;
        }
    }

    return rval;
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

        if (handle->master != NULL && SERVER_IS_MASTER(handle->master->server))
        {
            int64_t domain = get_server_info(handle, handle->master)->gtid_domain_id;
            if (handle->master_gtid_domain >= 0 && domain != handle->master_gtid_domain)
            {
                MXS_INFO("gtid_domain_id of master has changed: %" PRId64 " -> %" PRId64 ".",
                         handle->master_gtid_domain, domain);
            }
            handle->master_gtid_domain = domain;
        }

        ptr = mon->monitored_servers;
        while (ptr)
        {
            MySqlServerInfo *serv_info = get_server_info(handle, ptr);
            ss_dassert(serv_info);

            if (ptr->server->node_id > 0 && ptr->server->master_id > 0 &&
                getSlaveOfNodeId(mon->monitored_servers, ptr->server->node_id, REJECT_DOWN) &&
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
                MySqlServerInfo *serv_info = get_server_info(handle, ptr);

                /** If "detect_stale_master" option is On, let's use the previous master.
                 *
                 * Multi-master mode detects the stale masters in find_graph_cycles().
                 *
                 * TODO: If a stale master goes down and comes back up, it loses
                 * the master status. An adequate solution would be to promote
                 * the stale master as a real master if it is the last running server.
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
                    monitor_set_pending_status(ptr, SERVER_STALE_STATUS | SERVER_MASTER);

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
                    unsigned int bits = SERVER_SLAVE | SERVER_RUNNING;

                    if ((ptr->mon_prev_status & bits) == bits &&
                        root_master && SERVER_IS_MASTER(root_master->server))
                    {
                        /** Slave with a running master, assign stale slave candidacy */
                        if ((ptr->pending_status & bits) == bits)
                        {
                            monitor_set_pending_status(ptr, SERVER_STALE_SLAVE);
                        }
                        /** Server lost slave when a master is available, remove
                         * stale slave candidacy */
                        else if ((ptr->pending_status & bits) == SERVER_RUNNING)
                        {
                            monitor_clear_pending_status(ptr, SERVER_STALE_SLAVE);
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
                        monitor_set_pending_status(ptr, SERVER_SLAVE);
                    }
                    else if (root_master == NULL && serv_info->slave_configured)
                    {
                        monitor_set_pending_status(ptr, SERVER_SLAVE);
                    }
                }

                ptr->server->status = ptr->pending_status;
            }
            ptr = ptr->next;
        }

        /** Now that all servers have their status correctly set, we can check
            if we need to use standalone master. */
        if (handle->detect_standalone_master)
        {
            if (standalone_master_required(handle, mon->monitored_servers))
            {
                // Other servers have died, set last remaining server as master
                if (set_standalone_master(handle, mon->monitored_servers))
                {
                    // Update the root_master to point to the standalone master
                    root_master = handle->master;
                }
            }
            else
            {
                handle->warn_set_standalone_master = true;
            }
        }

        if (root_master && SERVER_IS_MASTER(root_master->server))
        {
            // Clear slave and stale slave status bits from current master
            server_clear_status_nolock(root_master->server, SERVER_SLAVE | SERVER_STALE_SLAVE);
            monitor_clear_pending_status(root_master, SERVER_SLAVE | SERVER_STALE_SLAVE);

            /**
             * Clear external slave status from master if configured to do so.
             * This allows parts of a multi-tiered replication setup to be used
             * in MaxScale.
             */
            if (handle->ignore_external_masters)
            {
                monitor_clear_pending_status(root_master, SERVER_SLAVE_OF_EXTERNAL_MASTER);
                server_clear_status_nolock(root_master->server, SERVER_SLAVE_OF_EXTERNAL_MASTER);
            }
        }

        ss_dassert(handle->master == root_master);
        ss_dassert((root_master->server->status & (SERVER_SLAVE | SERVER_MASTER))
                   != (SERVER_SLAVE | SERVER_MASTER));

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(mon, handle->script, handle->events);
        bool failover_performed = false; // Has an automatic failover been performed this loop?

        if (handle->auto_failover)
        {
            const char RE_ENABLE_FMT[] = "%s To re-enable failover, manually set '%s' to 'true' for monitor "
                                         "'%s' via MaxAdmin or the REST API, or restart MaxScale.";
            if (failover_not_possible(handle))
            {
                const char PROBLEMS[] = "Failover is not possible due to one or more problems in the "
                                        "replication configuration, disabling automatic failover. Failover "
                                        "should only be enabled after the replication configuration has been "
                                        "fixed.";
                MXS_ERROR(RE_ENABLE_FMT, PROBLEMS, CN_AUTO_FAILOVER, mon->name);
                handle->auto_failover = false;
                disable_setting(handle, CN_AUTO_FAILOVER);
            }
            // If master seems to be down, check if slaves are receiving events.
            else if (handle->verify_master_failure && handle->master &&
                     SERVER_IS_DOWN(handle->master->server) && slave_receiving_events(handle))
            {
                MXS_INFO("Master failure not yet confirmed by slaves, delaying failover.");
            }
            else if (!mon_process_failover(handle, handle->failover_timeout, &failover_performed))
            {
                const char FAILED[] = "Failed to perform failover, disabling automatic failover.";
                MXS_ERROR(RE_ENABLE_FMT, FAILED, CN_AUTO_FAILOVER, mon->name);
                handle->auto_failover = false;
                disable_setting(handle, CN_AUTO_FAILOVER);
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
                MySqlServerInfo *serv_info = get_server_info(handle, ptr);

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

        // Do not auto-join servers on this monitor loop if a failover (or any other cluster modification)
        // has been performed, as server states have not been updated yet. It will happen next iteration.
        if (handle->auto_rejoin && !failover_performed && cluster_can_be_joined(handle))
        {
            // Check if any servers should be autojoined to the cluster
            ServerVector joinable_servers;
            if (get_joinable_servers(handle, &joinable_servers))
            {
                uint32_t joins = do_rejoin(handle, joinable_servers);
                if (joins > 0)
                {
                    MXS_NOTICE("%d server(s) redirected or rejoined the cluster.", joins);
                }
                if (joins < joinable_servers.size())
                {
                    MXS_ERROR("A cluster join operation failed, disabling automatic rejoining. "
                              "To re-enable, manually set '%s' to 'true' for monitor '%s' via MaxAdmin or "
                              "the REST API.", CN_AUTO_REJOIN, mon->name);
                    handle->auto_rejoin = false;
                    disable_setting(handle, CN_AUTO_REJOIN);
                }
            }
            else
            {
                MXS_ERROR("Query error to master '%s' prevented a possible rejoin operation.",
                          handle->master->server->unique_name);
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
 * @param ptr                The list of servers to monitor
 * @param node_id            The MySQL server_id to fetch
 * @param slave_down_setting Whether to accept or reject slaves which are down
 * @return                   The slave server of this node_id
 */
static MXS_MONITORED_SERVER *
getSlaveOfNodeId(MXS_MONITORED_SERVER *ptr, long node_id, slave_down_setting_t slave_down_setting)
{
    SERVER *current;
    while (ptr)
    {
        current = ptr->server;
        if (current->master_id == node_id && (slave_down_setting == ACCEPT_DOWN || !SERVER_IS_DOWN(current)))
        {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
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

/*******
 * This function sets the replication heartbeat
 * into the maxscale_schema.replication_heartbeat table in the current master.
 * The inserted values will be seen from all slaves replicating from this master.
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
            heartbeat, handle->master->server->node_id, id);

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
                    handle->master->server->node_id, id, heartbeat);

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
    if (handle->master != NULL && (mxs_mysql_query(database->con, select_heartbeat_query) == 0
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
            find_slave = getSlaveOfNodeId(mon->monitored_servers, current->node_id, ACCEPT_DOWN);

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
                        handle->master = master;
                    }

                    MySqlServerInfo* info = get_server_info(handle, master);

                    if (SERVER_IS_RUNNING(master->server))
                    {
                        /** Only set the Master status if read_only is disabled */
                        monitor_set_pending_status(master, info->read_only ? SERVER_SLAVE : SERVER_MASTER);
                    }
                }
                else
                {
                    if (current->master_id > 0)
                    {
                        monitor_set_pending_status(ptr, SERVER_SLAVE);
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

    if (mxs_mysql_query(database->con,
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

    if (mxs_mysql_query(database->con,
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

    if (mxs_mysql_query(database->con,
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

    if (mxs_mysql_query(database->con,
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

/**
 * @brief Process possible failover event
 *
 * If a master failure has occurred and MaxScale is configured with failover
 * functionality, this fuction executes an external failover program to elect
 * a new master server.
 *
 * This function should be called immediately after @c mon_process_state_changes.
 *
 * @param monitor               Monitor whose cluster is processed
 * @param failover_timeout      Timeout in seconds for the failover
 * @param cluster_modified_out  Set to true if modifying cluster
 * @return True on success, false on error
 *
 * @todo Currently this only works with flat replication topologies and
 *       needs to be moved inside mysqlmon as it is MariaDB specific code.
 */
bool mon_process_failover(MYSQL_MONITOR* monitor, uint32_t failover_timeout, bool* cluster_modified_out)
{
    ss_dassert(*cluster_modified_out == false);
    bool rval = true;
    MXS_CONFIG* cnf = config_get_global_options();
    MXS_MONITORED_SERVER* failed_master = NULL;

    if (!cnf->passive)
    {
        for (MXS_MONITORED_SERVER *ptr = monitor->monitor->monitored_servers; ptr; ptr = ptr->next)
        {
            if (ptr->new_event && ptr->server->last_event == MASTER_DOWN_EVENT)
            {
                if (failed_master)
                {
                    MXS_ALERT("Multiple failed master servers detected: "
                              "'%s' is the first master to fail but server "
                              "'%s' has also triggered a master_down event.",
                              failed_master->server->unique_name,
                              ptr->server->unique_name);
                    return false;
                }

                if (ptr->server->active_event)
                {
                    // MaxScale was active when the event took place
                    failed_master = ptr;
                }
                else if (monitor->monitor->master_has_failed)
                {
                    /**
                     * If a master_down event was triggered when this MaxScale was
                     * passive, we need to execute the failover script again if no new
                     * masters have appeared.
                     */
                    int64_t timeout = SEC_TO_HB(failover_timeout);
                    int64_t t = hkheartbeat - ptr->server->triggered_at;

                    if (t > timeout)
                    {
                        MXS_WARNING("Failover of server '%s' did not take place within "
                                    "%u seconds, failover needs to be re-triggered",
                                    ptr->server->unique_name, failover_timeout);
                        failed_master = ptr;
                    }
                }
            }
        }
    }

    if (failed_master)
    {
        int failcount = monitor->failcount;
        if (failcount > 1 && failed_master->mon_err_count == 1)
        {
            MXS_WARNING("Master has failed. If master status does not change in %d monitor passes, failover "
                        "begins.", failcount - 1);
        }
        else if (failed_master->mon_err_count >= failcount)
        {
            MXS_NOTICE("Performing automatic failover to replace failed master '%s'.",
                       failed_master->server->unique_name);
            failed_master->new_event = false;
            rval = failover_check(monitor, NULL) && do_failover(monitor, NULL);
            if (rval)
            {
                *cluster_modified_out = true;
            }
        }
    }

    return rval;
}

/**
 * Update replication settings and gtid:s of the slave server.
 *
 * @param mon Cluster monitor
 * @param server Slave to update
 * @return Slave server info. NULL on error, or if server is not a slave.
 */
static MySqlServerInfo* update_slave_info(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* server)
{
    MySqlServerInfo* info = get_server_info(mon, server);
    if (info->slave_status.slave_sql_running &&
        update_replication_settings(server, info) &&
        update_gtids(mon, server, info) &&
        do_show_slave_status(mon, info, server))
    {
        return info;
    }
    return NULL;
}

/**
 * Check if server has binary log enabled. Print warnings if gtid_strict_mode or log_slave_updates is off.
 *
 * @param server Server to check
 * @param server_info Server info
 * @param print_on Print warnings or not
 * @return True if log_bin is on
 */
static bool check_replication_settings(const MXS_MONITORED_SERVER* server, MySqlServerInfo* server_info,
                                       print_repl_warnings_t print_warnings = WARNINGS_ON)
{
    bool rval = true;
    const char* servername = server->server->unique_name;
    if (server_info->rpl_settings.log_bin == false)
    {
        if (print_warnings == WARNINGS_ON)
        {
            const char NO_BINLOG[] =
                "Slave '%s' has binary log disabled and is not a valid promotion candidate.";
            MXS_WARNING(NO_BINLOG, servername);
        }
        rval = false;
    }
    else if (print_warnings == WARNINGS_ON)
    {
        if (server_info->rpl_settings.gtid_strict_mode == false)
        {
            const char NO_STRICT[] =
                "Slave '%s' has gtid_strict_mode disabled. Enabling this setting is recommended. "
                "For more information, see https://mariadb.com/kb/en/library/gtid/#gtid_strict_mode";
            MXS_WARNING(NO_STRICT, servername);
        }
        if (server_info->rpl_settings.log_slave_updates == false)
        {
            const char NO_SLAVE_UPDATES[] =
                "Slave '%s' has log_slave_updates disabled. It is a valid candidate but replication "
                "will break for lagging slaves if '%s' is promoted.";
            MXS_WARNING(NO_SLAVE_UPDATES, servername, servername);
        }
    }
    return rval;
}

/**
 * Check that the given slave is a valid promotion candidate. Update the server info structs of all slaves.
 * Also populate the output vector with other slave servers.
 *
 * @param mon Cluster monitor
 * @param preferred Preferred new master
 * @param slaves_out Output array for other slaves. These should be redirected to the new master. Can be NULL.
 * @param err_out Json object for error printing. Can be NULL.
 * @return True, if given slave is a valid promotion candidate.
 */
bool switchover_check_preferred_master(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* preferred,
                                       ServerVector* slaves_out, json_t** err_out)
{
    ss_dassert(preferred);
    bool rval = true;
    MySqlServerInfo* preferred_info = update_slave_info(mon, preferred);
    if (preferred_info == NULL || !check_replication_settings(preferred, preferred_info))
    {
        PRINT_MXS_JSON_ERROR(err_out, "The requested server '%s' is not a valid promotion candidate.",
                             preferred->server->unique_name);
        rval = false;
    }
    for (MXS_MONITORED_SERVER *slave = mon->monitor->monitored_servers; slave; slave = slave->next)
    {
        if (slave != preferred)
        {
            // The update_slave_info()-call is not strictly necessary here, but it should be ran to keep this
            // function analogous with failover_select_new_master(). The later functions can then assume that
            // slave server info is up to date.
            MySqlServerInfo* slave_info = update_slave_info(mon, slave);
            if (slave_info && slaves_out)
            {
                slaves_out->push_back(slave);
            }
        }
    }
    return rval;
}

/**
 * Is the candidate a better choice for master than the previous best?
 *
 * @param current_best_info Server info of current best choice
 * @param candidate_info Server info of new candidate
 * @return True if candidate is better
 */
bool is_candidate_better(const MySqlServerInfo* current_best_info, const MySqlServerInfo* candidate_info)
{
    uint64_t cand_io = candidate_info->slave_status.gtid_io_pos.sequence;
    uint64_t cand_processed = candidate_info->gtid_current_pos.sequence;
    uint64_t curr_io = current_best_info->slave_status.gtid_io_pos.sequence;
    uint64_t curr_processed = current_best_info->gtid_current_pos.sequence;
    bool cand_updates = candidate_info->rpl_settings.log_slave_updates;
    bool curr_updates = current_best_info->rpl_settings.log_slave_updates;
    bool is_better = false;
    // Accept a slave with a later event in relay log.
    if (cand_io > curr_io)
    {
        is_better = true;
    }
    // If io sequences are identical, the slave with more events processed wins.
    else if (cand_io == curr_io)
    {
        if (cand_processed > curr_processed)
        {
            is_better = true;
        }
        // Finally, if binlog positions are identical, prefer a slave with log_slave_updates.
        else if (cand_processed == curr_processed && cand_updates && !curr_updates)
        {
            is_better = true;
        }
    }
    return is_better;
}

/**
 * Select a new master. Also add slaves which should be redirected to an array.
 *
 * @param mon The monitor
 * @param out_slaves Vector for storing slave servers.
 * @param err_out json object for error printing. Can be NULL.
 * @return The found master, or NULL if not found
 */
MXS_MONITORED_SERVER* select_new_master(MYSQL_MONITOR* mon, ServerVector* slaves_out, json_t** err_out)
{
    ss_dassert(slaves_out && slaves_out->size() == 0);
    /* Select a new master candidate. Selects the one with the latest event in relay log.
     * If multiple slaves have same number of events, select the one with most processed events. */
    MXS_MONITORED_SERVER* current_best = NULL;
    MySqlServerInfo* current_best_info = NULL;
     // Servers that cannot be selected because of exclusion, but seem otherwise ok.
    ServerVector valid_but_excluded;
    // Index of the current best candidate in slaves_out
    int master_vector_index = -1;

    for (MXS_MONITORED_SERVER *cand = mon->monitor->monitored_servers; cand; cand = cand->next)
    {
        // If a server cannot be connected to, it won't be considered for promotion or redirected.
        // Do not worry about the exclusion list yet, querying the excluded servers is ok.
        MySqlServerInfo* cand_info = update_slave_info(mon, cand);
        if (cand_info)
        {
            slaves_out->push_back(cand);
            // Check that server is not in the exclusion list while still being a valid choice.
            if (server_is_excluded(mon, cand) && check_replication_settings(cand, cand_info, WARNINGS_OFF))
            {
                valid_but_excluded.push_back(cand);
                const char CANNOT_SELECT[] = "Promotion candidate '%s' is excluded from new "
                "master selection.";
                MXS_INFO(CANNOT_SELECT, cand->server->unique_name);
            }
            else if (check_replication_settings(cand, cand_info))
            {
                // If no new master yet, accept any valid candidate. Otherwise check.
                if (current_best == NULL || is_candidate_better(current_best_info, cand_info))
                {
                    // The server has been selected for promotion, for now.
                    current_best = cand;
                    current_best_info = cand_info;
                    master_vector_index = slaves_out->size() - 1;
                }
            }
        }
    }

    if (current_best)
    {
        // Remove the selected master from the vector.
        ServerVector::iterator remove_this = slaves_out->begin();
        remove_this += master_vector_index;
        slaves_out->erase(remove_this);
    }

    // Check if any of the excluded servers would be better than the best candidate.
    for (ServerVector::const_iterator iter = valid_but_excluded.begin();
         iter != valid_but_excluded.end();
         iter++)
    {
        MySqlServerInfo* excluded_info = get_server_info(mon, *iter);
        const char* excluded_name = (*iter)->server->unique_name;
        if (current_best == NULL)
        {
            const char EXCLUDED_ONLY_CAND[] = "Server '%s' is a viable choice for new master, "
            "but cannot be selected as it's excluded.";
            MXS_WARNING(EXCLUDED_ONLY_CAND, excluded_name);
            break;
        }
        else if (is_candidate_better(current_best_info, excluded_info))
        {
            // Print a warning if this server is actually a better candidate than the previous
            // best.
            const char EXCLUDED_CAND[] = "Server '%s' is superior to current "
            "best candidate '%s', but cannot be selected as it's excluded. This may lead to "
            "loss of data if '%s' is ahead of other servers.";
            MXS_WARNING(EXCLUDED_CAND, excluded_name, current_best->server->unique_name, excluded_name);
            break;
        }
    }

    if (current_best == NULL)
    {
        PRINT_MXS_JSON_ERROR(err_out, "No suitable promotion candidate found.");
    }
    return current_best;
}

/**
 * Waits until the new master has processed all its relay log, or time is up.
 *
 * @param mon The monitor
 * @param new_master The new master
 * @param seconds_remaining How much time left
 * @param err_out Json error output
 * @return True if relay log was processed within time limit, or false if time ran out or an error occurred.
 */
bool failover_wait_relay_log(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* new_master, int seconds_remaining,
                             json_t** err_out)
{
    MySqlServerInfo* master_info = get_server_info(mon, new_master);
    time_t begin = time(NULL);
    bool query_ok = true;
    bool io_pos_stable = true;
    while (master_info->relay_log_events() > 0 &&
           query_ok &&
           io_pos_stable &&
           difftime(time(NULL), begin) < seconds_remaining)
    {
        MXS_INFO("Relay log of server '%s' not yet empty, waiting to clear %" PRId64 " events.",
                 new_master->server->unique_name, master_info->relay_log_events());
        thread_millisleep(1000); // Sleep for a while before querying server again.
        // Todo: check server version before entering failover.
        Gtid old_gtid_io_pos = master_info->slave_status.gtid_io_pos;
        // Update gtid:s first to make sure Gtid_IO_Pos is the more recent value.
        // It doesn't matter here, but is a general rule.
        query_ok = update_gtids(mon, new_master, master_info) &&
                   do_show_slave_status(mon, master_info, new_master);
        io_pos_stable = (old_gtid_io_pos == master_info->slave_status.gtid_io_pos);
    }

    bool rval = false;
    if (master_info->relay_log_events() == 0)
    {
        rval = true;
    }
    else
    {
        string reason = "Timeout";
        if (!query_ok)
        {
            reason = "Query error";
        }
        else if (!io_pos_stable)
        {
            reason = "Old master sent new event(s)";
        }
        else if (master_info->relay_log_events() < 0)
        {
            reason = "Invalid Gtid(s) (current_pos: " + master_info->gtid_current_pos.to_string() +
                     ", io_pos: " + master_info->slave_status.gtid_io_pos.to_string() + ")";
        }
        PRINT_MXS_JSON_ERROR(err_out, "Failover: %s while waiting for server '%s' to process relay log. "
                             "Cancelling failover.",
                             reason.c_str(), new_master->server->unique_name);
        rval = false;
    }
    return rval;
}

/**
 * Prepares a server for the replication master role.
 *
 * @param mon The monitor
 * @param new_master The new master server
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful
 */
bool promote_new_master(MXS_MONITORED_SERVER* new_master, json_t** err_out)
{
    bool success = false;
    MXS_NOTICE("Promoting server '%s' to master.", new_master->server->unique_name);
    const char* query = "STOP SLAVE;";
    if (mxs_mysql_query(new_master->con, query) == 0)
    {
        query = "RESET SLAVE ALL;";
        if (mxs_mysql_query(new_master->con, query) == 0)
        {
            query = "SET GLOBAL read_only=0;";
            if (mxs_mysql_query(new_master->con, query) == 0)
            {
                success = true;
            }
        }
    }
    if (!success)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Promotion failed: '%s'. Query: '%s'.",
                             mysql_error(new_master->con), query);
    }
    return success;
}

string generate_change_master_cmd(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* new_master)
{
    std::stringstream change_cmd;
    change_cmd << "CHANGE MASTER TO MASTER_HOST = '" << new_master->server->name << "', ";
    change_cmd << "MASTER_PORT = " <<  new_master->server->port << ", ";
    change_cmd << "MASTER_USE_GTID = current_pos, ";
    change_cmd << "MASTER_USER = '" << mon->replication_user << "', ";
    const char MASTER_PW[] = "MASTER_PASSWORD = '";
    const char END[] = "';";
#if defined(SS_DEBUG)
    std::stringstream change_cmd_nopw;
    change_cmd_nopw << change_cmd.str();
    change_cmd_nopw << MASTER_PW << "******" << END;;
    MXS_DEBUG("Change master command is '%s'.", change_cmd_nopw.str().c_str());
#endif
    change_cmd << MASTER_PW << mon->replication_password << END;
    return change_cmd.str();
}

/**
 * Redirect one slave server to another master
 *
 * @param slave Server to redirect
 * @param change_cmd Change master command, usually generated by generate_change_master_cmd()
 * @return True if slave accepted all commands
 */
bool redirect_one_slave(MXS_MONITORED_SERVER* slave, const char* change_cmd)
{
    bool rval = false;
    if (mxs_mysql_query(slave->con, "STOP SLAVE;") == 0 &&
        mxs_mysql_query(slave->con, "RESET SLAVE;") == 0 && // To erase any old I/O or SQL errors
        mxs_mysql_query(slave->con, change_cmd) == 0 &&
        mxs_mysql_query(slave->con, "START SLAVE;") == 0)
    {
        rval = true;
        MXS_NOTICE("Slave '%s' redirected to new master.", slave->server->unique_name);
    }
    else
    {
        MXS_WARNING("Slave '%s' redirection failed: '%s'.", slave->server->unique_name,
                    mysql_error(slave->con));
    }
    return rval;
}

/**
 * Redirects slaves to replicate from another master server.
 *
 * @param mon The monitor
 * @param slaves An array of slaves
 * @param new_master The replication master
 * @param redirected_slaves A vector where to insert successfully redirected slaves. Can be NULL.
 * @return The number of slaves successfully redirected.
 */
int redirect_slaves(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* new_master, const ServerVector& slaves,
                    ServerVector* redirected_slaves = NULL)
{
    MXS_NOTICE("Redirecting slaves to new master.");
    std::string change_cmd = generate_change_master_cmd(mon, new_master);
    int successes = 0;
    for (ServerVector::const_iterator iter = slaves.begin(); iter != slaves.end(); iter++)
    {
        if (redirect_one_slave(*iter, change_cmd.c_str()))
        {
            successes++;
            if (redirected_slaves)
            {
                redirected_slaves->push_back(*iter);
            }
        }
    }
    return successes;
}

/**
 * Print a redirect error to logs. If err_out exists, generate a combined error message by querying all
 * the server parameters for connection errors and append these errors to err_out.
 *
 * @param demotion_target If not NULL, this is the first server to query.
 * @param redirectable_slaves Other servers to query for errors.
 * @param err_out If not null, the error output object.
 */
void print_redirect_errors(MXS_MONITORED_SERVER* first_server, const ServerVector& servers,
                           json_t** err_out)
{
    // Individual server errors have already been printed to the log.
    // For JSON, gather the errors again.
    const char MSG[] = "Could not redirect any slaves to the new master.";
    MXS_ERROR(MSG);
    if (err_out)
    {
        ServerVector failed_slaves;
        if (first_server)
        {
            failed_slaves.push_back(first_server);
        }
        failed_slaves.insert(failed_slaves.end(),
                             servers.begin(), servers.end());
        string combined_error = get_connection_errors(failed_slaves);
        *err_out = mxs_json_error_append(*err_out,
                                         "%s Errors: %s.", MSG, combined_error.c_str());
    }
}

/**
 * Performs failover for a simple topology (1 master, N slaves, no intermediate masters).
 *
 * @param mon Server cluster monitor
 * @param err_out Json output
 * @return True if successful
 */
static bool do_failover(MYSQL_MONITOR* mon, json_t** err_out)
{
    // Topology has already been tested to be simple.
    if (mon->master_gtid_domain < 0)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Cluster gtid domain is unknown. Cannot failover.");
        return false;
    }
    // Total time limit on how long this operation may take. Checked and modified after significant steps are
    // completed.
    int seconds_remaining = mon->failover_timeout;
    time_t start_time = time(NULL);
    // Step 1: Select new master. Also populate a vector with all slaves not the selected master.
    ServerVector redirectable_slaves;
    MXS_MONITORED_SERVER* new_master = select_new_master(mon, &redirectable_slaves, err_out);
    if (new_master == NULL)
    {
        return false;
    }
    time_t step1_time = time(NULL);
    seconds_remaining -= difftime(step1_time, start_time);

    bool rval = false;
    // Step 2: Wait until relay log consumed.
    if (failover_wait_relay_log(mon, new_master, seconds_remaining, err_out))
    {
        time_t step2_time = time(NULL);
        int seconds_step2 = difftime(step2_time, step1_time);
        MXS_DEBUG("Failover: relay log processing took %d seconds.", seconds_step2);
        seconds_remaining -= seconds_step2;

        // Step 3: Stop and reset slave, set read-only to 0.
        if (promote_new_master(new_master, err_out))
        {
            // Step 4: Redirect slaves.
            ServerVector redirected_slaves;
            int redirects = redirect_slaves(mon, new_master, redirectable_slaves, &redirected_slaves);
            bool success = redirectable_slaves.empty() ? true : redirects > 0;
            if (success)
            {
                time_t step4_time = time(NULL);
                seconds_remaining -= difftime(step4_time, step2_time);

                // Step 5: Finally, add an event to the new master to advance gtid and wait for the slaves
                // to receive it. seconds_remaining can be 0 or less at this point. Even in such a case
                // wait_cluster_stabilization() may succeed if replication is fast enough.
                if (wait_cluster_stabilization(mon, new_master, redirected_slaves, seconds_remaining))
                {
                    rval = true;
                    time_t step5_time = time(NULL);
                    int seconds_step5 = difftime(step5_time, step4_time);
                    seconds_remaining -= seconds_step5;
                    MXS_DEBUG("Failover: slave replication confirmation took %d seconds with "
                              "%d seconds to spare.", seconds_step5, seconds_remaining);
                }
            }
            else
            {
                print_redirect_errors(NULL, redirectable_slaves, err_out);
            }
        }
    }

    return rval;
}

/**
 * Query one row of results, save strings to array. Any additional rows are ignored.
 *
 * @param database The database to query.
 * @param query The query to execute.
 * @param expected_cols How many columns the result should have.
 * @param output The output array to populate.
 * @return True on success.
 */
static bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
                          StringVector* output)
{
    bool rval = false;
    MYSQL_RES *result;
    if (mxs_mysql_query(database->con, query) == 0 && (result = mysql_store_result(database->con)) != NULL)
    {
        unsigned int columns = mysql_field_count(database->con);
        if (columns != expected_cols)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for '%s'. Expected %d columns, got %d. MySQL Version: %s",
                      query, expected_cols, columns, database->server->version_string);
        }
        else
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row)
            {
                for (unsigned int i = 0; i < columns; i++)
                {
                    output->push_back((row[i] != NULL) ? row[i] : "");
                }
                rval = true;
            }
            else
            {
                MXS_ERROR("Query '%s' returned no rows.", query);
            }
            mysql_free_result(result);
        }
    }
    else
    {
        mon_report_query_error(database);
    }
    return rval;
}

/**
 * Query a few miscellaneous replication settings.
 *
 * @param database The slave server to query
 * @param info Where to save results
 * @return True on success
 */
static bool update_replication_settings(MXS_MONITORED_SERVER *database, MySqlServerInfo* info)
{
    StringVector row;
    bool ok = query_one_row(database, "SELECT @@gtid_strict_mode, @@log_bin, @@log_slave_updates;", 3, &row);
    if (ok)
    {
        info->rpl_settings.gtid_strict_mode = (row[0] == "1");
        info->rpl_settings.log_bin = (row[1] == "1");
        info->rpl_settings.log_slave_updates = (row[2] == "1");
    }
    return ok;
}

/**
 * Query gtid_current_pos and gtid_binlog_pos and save the values to the server info object.
 * Only the cluster master domain is parsed.
 *
 * @param mon Cluster monitor
 * @param database The server to query
 * @param info Server info structure for saving result
 * @return True if successful
 */
static bool update_gtids(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER *database, MySqlServerInfo* info)
{
    StringVector row;
    const char query[] = "SELECT @@gtid_current_pos, @@gtid_binlog_pos;";
    const int ind_current_pos = 0;
    const int ind_binlog_pos = 1;
    int64_t domain = mon->master_gtid_domain;
    ss_dassert(domain >= 0);
    bool rval = false;
    if (query_one_row(database, query, 2, &row))
    {
        info->gtid_current_pos = (row[ind_current_pos] != "") ?
                                 Gtid(row[ind_current_pos].c_str(), domain) : Gtid();
        info->gtid_binlog_pos = (row[ind_binlog_pos] != "") ?
                                Gtid(row[ind_binlog_pos].c_str(), domain) : Gtid();
        rval = true;
    }
    return rval;
}

/**
 * Demotes the current master server, preparing it for replicating from another server. This step can take a
 * while if long writes are running on the server.
 *
 * @param mon Cluster monitor
 * @param current_master Server to demote
 * @param info Current master info. Will be written to.
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful.
 */
static bool switchover_demote_master(MYSQL_MONITOR* mon,
                                     MXS_MONITORED_SERVER* current_master,
                                     MySqlServerInfo* info,
                                     json_t** err_out)
{
    MXS_NOTICE("Demoting server '%s'.", current_master->server->unique_name);
    string error;
    bool success = false;
    const char* query = "SET GLOBAL read_only=1;";
    if (mxs_mysql_query(current_master->con, query) == 0)
    {
        query = "FLUSH TABLES;";
        if (mxs_mysql_query(current_master->con, query) == 0)
        {
            query = "FLUSH LOGS;";
            if (mxs_mysql_query(current_master->con, query) == 0)
            {
                query = "";
                if (update_gtids(mon, current_master, info))
                {
                    success = true;
                }
            }
        }
        if (!success)
        {
            // Somehow, a step after "SET read_only" failed. Try to set read_only back to 0. It may not
            // work since the connection is likely broken.
            error = mysql_error(current_master->con);
            mxs_mysql_query(current_master->con, "SET GLOBAL read_only=0;");
        }
    }
    else
    {
        error = mysql_error(current_master->con);
    }

    if (!success)
    {
        if (error.empty())
        {
            PRINT_MXS_JSON_ERROR(err_out, "Demotion failed due to an error in updating gtid:s.");
        }
        else
        {
            PRINT_MXS_JSON_ERROR(err_out, "Demotion failed due to a query error: '%s'. Query: '%s'.",
                                 error.c_str(), query);
        }
    }
    return success;
}

static string generate_master_gtid_wait_cmd(const Gtid& gtid, double timeout)
{
    std::stringstream query_ss;
    query_ss << "SELECT MASTER_GTID_WAIT(\"" << gtid.to_string() << "\", " << timeout << ");";
    return query_ss.str();
}

/**
 * Wait until slave replication catches up with the master gtid
 *
 * @param slave Slave to wait on
 * @param gtid Which gtid must be reached
 * @param total_timeout Maximum wait time in seconds
 * @param read_timeout The value of read_timeout for the connection
 * @param err_out json object for error printing. Can be NULL.
 * @return True, if target gtid was reached within allotted time
 */
static bool switchover_wait_slave_catchup(MXS_MONITORED_SERVER* slave, const Gtid& gtid,
                                          int total_timeout, int read_timeout,
                                          json_t** err_out)
{
    ss_dassert(read_timeout > 0);
    StringVector output;
    bool gtid_reached = false;
    bool error = false;
    double seconds_remaining = total_timeout;

    // Determine a reasonable timeout for the MASTER_GTID_WAIT-function depending on the
    // backend_read_timeout setting (should be >= 1) and time remaining.
    double loop_timeout = double(read_timeout) - 0.5;
    string cmd = generate_master_gtid_wait_cmd(gtid, loop_timeout);

    while (seconds_remaining > 0 && !gtid_reached && !error)
    {
        if (loop_timeout > seconds_remaining)
        {
            // For the last iteration, change the wait timeout.
            cmd = generate_master_gtid_wait_cmd(gtid, seconds_remaining);
        }
        seconds_remaining -= loop_timeout;

        if (query_one_row(slave, cmd.c_str(), 1, &output))
        {
            if (output[0] == "0")
            {
                gtid_reached = true;
            }
            output.clear();
        }
        else
        {
            error = true;
        }
    }

    if (error)
    {
        PRINT_MXS_JSON_ERROR(err_out, "MASTER_GTID_WAIT() query error on slave '%s'.",
                             slave->server->unique_name);
    }
    else if (!gtid_reached)
    {
        PRINT_MXS_JSON_ERROR(err_out, "MASTER_GTID_WAIT() timed out on slave '%s'.",
                             slave->server->unique_name);
    }
    return gtid_reached;
}

/**
 * Wait until slave replication catches up with the master gtid for all slaves in the vector.
 *
 * @param slave Slaves to wait on
 * @param gtid Which gtid must be reached
 * @param total_timeout Maximum wait time in seconds
 * @param read_timeout The value of read_timeout for the connection
 * @param err_out json object for error printing. Can be NULL.
 * @return True, if target gtid was reached within allotted time for all servers
 */
static bool switchover_wait_slaves_catchup(const ServerVector& slaves, const Gtid& gtid,
                                           int total_timeout, int read_timeout,
                                           json_t** err_out)
{
    bool success = true;
    int seconds_remaining = total_timeout;

    for (ServerVector::const_iterator iter = slaves.begin();
         iter != slaves.end() && success;
         iter++)
    {
        if (seconds_remaining <= 0)
        {
            success = false;
        }
        else
        {
            time_t begin = time(NULL);
            MXS_MONITORED_SERVER* slave = *iter;
            if (switchover_wait_slave_catchup(slave, gtid, seconds_remaining, read_timeout, err_out))
            {
                seconds_remaining -= difftime(time(NULL), begin);
            }
            else
            {
                success = false;
            }
        }
    }
    return success;
}

/**
 * Starts a new slave connection on a server. Should be used on a demoted master server.
 *
 * @param mon Cluster monitor
 * @param old_master The server which will start replication
 * @param new_master Replication target
 * @return True if commands were accepted. This does not guarantee that replication proceeds
 * successfully.
 */
static bool switchover_start_slave(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* old_master,
                                   MXS_MONITORED_SERVER* new_master)
{
    bool rval = false;
    std::string change_cmd = generate_change_master_cmd(mon, new_master);
    if (mxs_mysql_query(old_master->con, change_cmd.c_str()) == 0 &&
        mxs_mysql_query(old_master->con, "START SLAVE;") == 0)
    {
        MXS_NOTICE("Old master '%s' starting replication from '%s'.",
                   old_master->server->unique_name, new_master->server->unique_name);
        rval = true;
    }
    else
    {
        MXS_ERROR("Old master '%s' could not start replication: '%s'.",
                  old_master->server->unique_name, mysql_error(old_master->con));
    }
    return rval;
}

/**
 * Get MySQL connection error strings from all the given servers, form one string.
 *
 * @param slaves Servers with errors
 * @return Concatenated string.
 */
static string get_connection_errors(const ServerVector& servers)
{
    // Get errors from all connections, form a string.
    std::stringstream ss;
    for (ServerVector::const_iterator iter = servers.begin(); iter != servers.end(); iter++)
    {
        const char* error = mysql_error((*iter)->con);
        ss_dassert(*error); // Every connection should have an error.
        ss << (*iter)->server->unique_name << ": '" << error << "'";
        if (iter + 1 != servers.end())
        {
            ss << ", ";
        }
    }
    return ss.str();
}

/**
 * Send an event to new master and wait for slaves to get the event.
 *
 * @param mon Cluster monitor
 * @param new_master Where to send the event
 * @param slaves Servers to monitor
 * @param seconds_remaining How long can we wait
 * @return True, if at least one slave got the new event within the time limit
 */
static bool wait_cluster_stabilization(MYSQL_MONITOR* mon,
                                       MXS_MONITORED_SERVER* new_master,
                                       const ServerVector& slaves,
                                       int seconds_remaining)
{
    ss_dassert(!slaves.empty());
    bool rval = false;
    time_t begin = time(NULL);
    MySqlServerInfo* new_master_info = get_server_info(mon, new_master);

    if (mxs_mysql_query(new_master->con, "FLUSH TABLES;") == 0 &&
        update_gtids(mon, new_master, new_master_info))
    {
        int query_fails = 0;
        int repl_fails = 0;
        int successes = 0;
        const Gtid target = new_master_info->gtid_current_pos;
        ServerVector wait_list = slaves; // Check all the servers in the list
        bool first_round = true;
        bool time_is_up = false;

        while (!wait_list.empty() && !time_is_up)
        {
            if (!first_round)
            {
                thread_millisleep(500);
            }

            // Erasing elements from an array, so iterate from last to first
            int i = wait_list.size() - 1;
            while (i >= 0)
            {
                MXS_MONITORED_SERVER* slave = wait_list[i];
                MySqlServerInfo* slave_info = get_server_info(mon, slave);
                if (update_gtids(mon, slave, slave_info) && do_show_slave_status(mon, slave_info, slave))
                {
                    if (!slave_info->slave_status.last_error.empty())
                    {
                        // IO or SQL error on slave, replication is a fail
                        MXS_WARNING("Slave '%s' cannot start replication: '%s'.",
                                    slave->server->unique_name,
                                    slave_info->slave_status.last_error.c_str());
                        wait_list.erase(wait_list.begin() + i);
                        repl_fails++;
                    }
                    else if (slave_info->gtid_current_pos.sequence >= target.sequence)
                    {
                        // This slave has reached the same gtid as master, remove from list
                        wait_list.erase(wait_list.begin() + i);
                        successes++;
                    }
                }
                else
                {
                    wait_list.erase(wait_list.begin() + i);
                    query_fails++;
                }
                i--;
            }

            first_round = false; // Sleep at start of next iteration
            if (difftime(time(NULL), begin) >= seconds_remaining)
            {
                time_is_up = true;
            }
        }

        ServerVector::size_type fails = repl_fails + query_fails + wait_list.size();
        if (fails > 0)
        {
            const char MSG[] = "Replication from the new master could not be confirmed for %lu slaves. "
                               "%d encountered an I/O or SQL error, %d failed to reply and %lu did not "
                               "advance in Gtid until time ran out.";
            MXS_WARNING(MSG, fails, repl_fails, query_fails, wait_list.size());
        }
        rval = (successes > 0);
    }
    else
    {
        MXS_ERROR("Could not confirm replication after switchover/failover because query to "
                  "the new master failed.");
    }
    return rval;
}

/**
 * Performs switchover for a simple topology (1 master, N slaves, no intermediate masters). If an intermediate
 * step fails, the cluster may be left without a master.
 *
 * @param mon Server cluster monitor
 * @param err_out json object for error printing. Can be NULL.
 * @return True if successful. If false, the cluster can be in various situations depending on which step
 * failed. In practice, manual intervention is usually required on failure.
 */
static bool do_switchover(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* current_master,
                          MXS_MONITORED_SERVER* new_master, json_t** err_out)
{
    MXS_MONITORED_SERVER* demotion_target = current_master ? current_master : mon->master;
    if (demotion_target == NULL)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Cluster does not have a running master. Run failover instead.");
        return false;
    }
    if (mon->master_gtid_domain < 0)
    {
        PRINT_MXS_JSON_ERROR(err_out, "Cluster gtid domain is unknown. Cannot switchover.");
        return false;
    }
    // Total time limit on how long this operation may take. Checked and modified after significant steps are
    // completed.
    int seconds_remaining = mon->switchover_timeout;
    time_t start_time = time(NULL);
    // Step 1: Select promotion candidate, save all slaves except promotion target to an array. If we have a
    // user-defined master candidate, check it. Otherwise, autoselect.
    MXS_MONITORED_SERVER* promotion_target = NULL;
    ServerVector redirectable_slaves;
    if (new_master)
    {
        if (switchover_check_preferred_master(mon, new_master, &redirectable_slaves, err_out))
        {
            promotion_target = new_master;
        }
    }
    else
    {
        promotion_target = select_new_master(mon, &redirectable_slaves, err_out);
    }
    if (promotion_target == NULL)
    {
        return false;
    }

    bool rval = false;
    MySqlServerInfo* curr_master_info = get_server_info(mon, demotion_target);

    // Step 2: Set read-only to on, flush logs.
    if (switchover_demote_master(mon, demotion_target, curr_master_info, err_out))
    {
        bool catchup_and_promote_success = false;
        time_t step2_time = time(NULL);
        seconds_remaining -= difftime(step2_time, start_time);

        // Step 3: Wait for the slaves (including promotion target) to catch up with master.
        ServerVector catchup_slaves = redirectable_slaves;
        catchup_slaves.push_back(promotion_target);
        if (switchover_wait_slaves_catchup(catchup_slaves, curr_master_info->gtid_binlog_pos,
                                           seconds_remaining, mon->monitor->read_timeout, err_out))
        {
            time_t step3_time = time(NULL);
            int seconds_step3 = difftime(step3_time, step2_time);
            MXS_DEBUG("Switchover: slave catchup took %d seconds.", seconds_step3);
            seconds_remaining -= seconds_step3;

            // Step 4: On new master STOP and RESET SLAVE, set read-only to off.
            if (promote_new_master(promotion_target, err_out))
            {
                catchup_and_promote_success = true;
                // Step 5: Redirect slaves and start replication on old master.
                ServerVector redirected_slaves;
                bool start_ok = switchover_start_slave(mon, demotion_target, promotion_target);
                if (start_ok)
                {
                    redirected_slaves.push_back(demotion_target);
                }
                int redirects = redirect_slaves(mon, promotion_target,
                                                redirectable_slaves, &redirected_slaves);

                bool success = redirectable_slaves.empty() ? start_ok : start_ok || redirects > 0;
                if (success)
                {
                    time_t step5_time = time(NULL);
                    seconds_remaining -= difftime(step5_time, step3_time);

                    // Step 6: Finally, add an event to the new master to advance gtid and wait for the slaves
                    // to receive it.
                    if (wait_cluster_stabilization(mon, promotion_target, redirected_slaves,
                                                   seconds_remaining))
                    {
                        rval = true;
                        time_t step6_time = time(NULL);
                        int seconds_step6 = difftime(step6_time, step5_time);
                        seconds_remaining -= seconds_step6;
                        MXS_DEBUG("Switchover: slave replication confirmation took %d seconds with "
                                  "%d seconds to spare.", seconds_step6, seconds_remaining);
                    }
                }
                else
                {
                    print_redirect_errors(demotion_target, redirectable_slaves, err_out);
                }
            }
        }

        if (!catchup_and_promote_success)
        {
            // Step 3 or 4 failed, try to undo step 2.
            const char QUERY_UNDO[] = "SET GLOBAL read_only=0;";
            if (mxs_mysql_query(demotion_target->con, QUERY_UNDO) == 0)
            {
                PRINT_MXS_JSON_ERROR(err_out, "read_only disabled on server %s.",
                                     demotion_target->server->unique_name);
            }
            else
            {
                PRINT_MXS_JSON_ERROR(err_out, "Could not disable read_only on server %s: '%s'.",
                                     demotion_target->server->unique_name, mysql_error(demotion_target->con));
            }
        }
    }
    return rval;
}

/**
 * Read server_id, read_only and (if 10.X) gtid_domain_id.
 *
 * @param database Database to update
 * @param serv_info Where to save results
 */
static void read_server_variables(MXS_MONITORED_SERVER* database, MySqlServerInfo* serv_info)
{
    string query = "SELECT @@global.server_id, @@read_only;";
    int columns = 2;
    if (serv_info->version ==  MYSQL_SERVER_VERSION_100)
    {
        query.erase(query.end() - 1);
        query += ", @@gtid_domain_id;";
        columns = 3;
    }

    int ind_id = 0;
    int ind_ro = 1;
    int ind_domain = 2;
    StringVector row;
    if (query_one_row(database, query.c_str(), columns, &row))
    {
        int64_t server_id = scan_server_id(row[ind_id].c_str());
        database->server->node_id = server_id;
        serv_info->server_id = server_id;

        ss_dassert(row[ind_ro] == "0" || row[ind_ro] == "1");
        serv_info->read_only = (row[ind_ro] == "1");
        if (columns == 3)
        {
            uint32_t domain = 0;
            ss_debug(int rv = ) sscanf(row[ind_domain].c_str(), "%" PRIu32, &domain);
            ss_dassert(rv == 1);
            serv_info->gtid_domain_id = domain;
        }
    }
}

/**
 * Checks if slave can replicate from master. Only considers gtid:s and only detects obvious errors. The
 * non-detected errors will mostly be detected once the slave tries to start replicating.
 *
 * @param mon Cluster monitor
 * @param slave Slave server candidate
 * @param slave_info Slave info
 * @param master Replication master
 * @param master_info Master info
 * @return True if slave can replicate from master
 */
static bool can_replicate_from(MYSQL_MONITOR* mon,
                               MXS_MONITORED_SERVER* slave, MySqlServerInfo* slave_info,
                               MXS_MONITORED_SERVER* master, MySqlServerInfo* master_info)
{
    bool rval = false;
    if (update_gtids(mon, slave, slave_info))
    {
        Gtid slave_gtid = slave_info->gtid_current_pos;
        Gtid master_gtid = master_info->gtid_binlog_pos;
        // The following are not sufficient requirements for replication to work, they only cover the basics.
        // If the servers have diverging histories, the redirection will seem to succeed but the slave IO
        // thread will stop in error.
        if (slave_gtid.server_id != SERVER_ID_UNKNOWN && master_gtid.server_id != SERVER_ID_UNKNOWN &&
            slave_gtid.domain == master_gtid.domain &&
            slave_gtid.sequence <= master_info->gtid_current_pos.sequence)
        {
            rval = true;
        }
    }
    return rval;
}

/**
 * Checks if a server is a possible rejoin candidate. A true result from this function is not yet sufficient
 * criteria and another call to can_replicate_from() should be made.
 *
 * @param mon Cluster monitor
 * @param server Server to check.
 * @param master_info Master server info
 * @return True, if server is a rejoin suspect.
 */
static bool server_is_rejoin_suspect(MYSQL_MONITOR* mon, MXS_MONITORED_SERVER* server,
                                     MySqlServerInfo* master_info)
{
    bool is_suspect = false;
    if (!SERVER_IS_MASTER(server->server) && SERVER_IS_RUNNING(server->server))
    {
        MySqlServerInfo* server_info = get_server_info(mon, server);
        SlaveStatusInfo* slave_status = &server_info->slave_status;
        // Has no slave connection, yet is not a master.
        if (server_info->n_slaves_configured == 0)
        {
            is_suspect = true;
        }
        // Or has existing slave connection ...
        else if (server_info->n_slaves_configured == 1)
        {
            MXS_MONITORED_SERVER* master = mon->master;
            // which is connected to master but it's the wrong one
            if (slave_status->slave_io_running  &&
                slave_status->master_server_id != master_info->server_id)
            {
                is_suspect = true;
            }
            // or is disconnected but master host or port is wrong.
            else if (!slave_status->slave_io_running && slave_status->slave_sql_running &&
                     (slave_status->master_host != master->server->name ||
                      slave_status->master_port != master->server->port))
            {
                is_suspect = true;
            }
        }
    }
    return is_suspect;
}

/**
 * Scan the servers in the cluster and add (re)joinable servers to an array.
 *
 * @param mon Cluster monitor
 * @param output Array to save results to. Each element is a valid (re)joinable server according
 * to latest data.
 * @return False, if there were possible rejoinable servers but communications error to master server
 * prevented final checks.
 */
static bool get_joinable_servers(MYSQL_MONITOR* mon, ServerVector* output)
{
    ss_dassert(output);
    MXS_MONITORED_SERVER* master = mon->master;
    MySqlServerInfo *master_info = get_server_info(mon, master);

    // Whether a join operation should be attempted or not depends on several criteria. Start with the ones
    // easiest to test. Go though all slaves and construct a preliminary list.
    ServerVector suspects;
    for (MXS_MONITORED_SERVER* server = mon->monitor->monitored_servers;
         server != NULL;
         server = server->next)
    {
        if (server_is_rejoin_suspect(mon, server, master_info))
        {
            suspects.push_back(server);
        }
    }

    // Update Gtid of master for better info.
    bool comm_ok = true;
    if (!suspects.empty())
    {
        if (update_gtids(mon, master, master_info))
        {
            for (size_t i = 0; i < suspects.size(); i++)
            {
                MXS_MONITORED_SERVER* suspect = suspects[i];
                MySqlServerInfo* suspect_info = get_server_info(mon, suspect);
                if (can_replicate_from(mon, suspect, suspect_info, master, master_info))
                {
                    output->push_back(suspect);
                }
            }
        }
        else
        {
            comm_ok = false;
        }
    }
    return comm_ok;
}

/**
 * (Re)join given servers to the cluster. The servers in the array are assumed to be joinable.
 * Usually the list is created by get_joinable_servers().
 *
 * @param mon Cluster monitor
 * @param joinable_servers Which servers to rejoin
 * @return The number of servers successfully rejoined
 */
static uint32_t do_rejoin(MYSQL_MONITOR* mon, const ServerVector& joinable_servers)
{
    MXS_MONITORED_SERVER* master = mon->master;
    uint32_t servers_joined = 0;
    if (!joinable_servers.empty())
    {
        string change_cmd = generate_change_master_cmd(mon, master);
        for (ServerVector::const_iterator iter = joinable_servers.begin();
             iter != joinable_servers.end();
             iter++)
        {
            MXS_MONITORED_SERVER* joinable = *iter;
            const char* name = joinable->server->unique_name;
            const char* master_name = master->server->unique_name;
            MySqlServerInfo* redir_info = get_server_info(mon, joinable);

            bool op_success;
            if (redir_info->n_slaves_configured == 0)
            {
                MXS_NOTICE("Directing standalone server '%s' to replicate from '%s'.", name, master_name);
                op_success = join_cluster(joinable, change_cmd.c_str());
            }
            else
            {
                MXS_NOTICE("Server '%s' is replicating from a server other than '%s', "
                           "redirecting it to '%s'.", name, master_name, master_name);
                op_success = redirect_one_slave(joinable, change_cmd.c_str());
            }

            if (op_success)
            {
                servers_joined++;
            }
        }
    }
    return servers_joined;
}

/**
 * Joins a standalone server to the cluster.
 *
 * @param server Server to join
 * @param change_cmd Change master command
 * @return True if commands were accepted by server
 */
static bool join_cluster(MXS_MONITORED_SERVER* server, const char* change_cmd)
{
    /* Server does not have slave connections. This operation can fail, or the resulting
     * replication may end up broken. */
    bool rval = false;
    if (mxs_mysql_query(server->con, "SET GLOBAL read_only=1;") == 0 &&
        mxs_mysql_query(server->con, change_cmd) == 0 &&
        mxs_mysql_query(server->con, "START SLAVE;") == 0)
    {
        rval = true;
    }
    else
    {
        mxs_mysql_query(server->con, "SET GLOBAL read_only=0;");
    }
    return rval;
}

/**
 * Set a monitor config parameter to "false". The effect persists over stopMonitor/startMonitor but not
 * MaxScale restart. Only use on boolean config settings.
 *
 * @param mon Cluster monitor
 * @param setting_name Setting to disable
 */
static void disable_setting(MYSQL_MONITOR* mon, const char* setting)
{
    MXS_CONFIG_PARAMETER p = {};
    p.name = const_cast<char*>(setting);
    p.value = const_cast<char*>("false");
    monitorAddParameters(mon->monitor, &p);
}

/**
 * Is the cluster a valid rejoin target
 *
 * @param mon Cluster monitor
 * @return True, if cluster can be joined
 */
static bool cluster_can_be_joined(MYSQL_MONITOR* mon)
{
    return (mon->master != NULL && SERVER_IS_MASTER(mon->master->server) && mon->master_gtid_domain >= 0);
}

/**
 * Scan a server id from a string.
 *
 * @param id_string
 * @return Server id, or -1 if scanning fails
 */
static int64_t scan_server_id(const char* id_string)
{
    int64_t server_id = SERVER_ID_UNKNOWN;
    ss_debug(int rv = ) sscanf(id_string, "%" PRId64, &server_id);
    ss_dassert(rv == 1);
    // Server id can be 0, which was even the default value until 10.2.1.
    // KB is a bit hazy on this, but apparently when replicating, the server id should not be 0. Not sure,
    // so MaxScale allows this.
#if defined(SS_DEBUG)
    const int64_t SERVER_ID_MIN = std::numeric_limits<uint32_t>::min();
    const int64_t SERVER_ID_MAX = std::numeric_limits<uint32_t>::max();
#endif
    ss_dassert(server_id >= SERVER_ID_MIN && server_id <= SERVER_ID_MAX);
    return server_id;
}
