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

#define MXS_MODULE_NAME "mariadbmon"

#include "mariadbmon.hh"
#include <inttypes.h>
#include <limits>
#include <sstream>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/debug.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/modulecmd.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/secrets.h>
#include <maxscale/utils.h>
// TODO: For monitorAddParameters
#include "../../../core/internal/monitor.h"
#include "utilities.hh"

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

using std::string;

class MySqlServerInfo;
class MariaDBMonitor;

enum slave_down_setting_t
{
    ACCEPT_DOWN,
    REJECT_DOWN
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
static void set_master_heartbeat(MariaDBMonitor *, MXS_MONITORED_SERVER *);
static void set_slave_heartbeat(MXS_MONITOR *, MXS_MONITORED_SERVER *);
static int add_slave_to_master(long *, int, long);
static bool isMySQLEvent(mxs_monitor_event_t event);
void check_maxscale_schema_replication(MXS_MONITOR *monitor);
static bool mon_process_failover(MariaDBMonitor*, uint32_t, bool*);


static bool update_replication_settings(MXS_MONITORED_SERVER *database, MySqlServerInfo* info);
static void read_server_variables(MXS_MONITORED_SERVER* database, MySqlServerInfo* serv_info);

static void disable_setting(MariaDBMonitor* mon, const char* setting);

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
const int64_t SERVER_ID_UNKNOWN = -1;

/** Default port */
const int PORT_UNKNOWN = 0;

MariaDBMonitor::MariaDBMonitor(MXS_MONITOR* monitor_base)
    : monitor(monitor_base)
    , id(config_get_global_options()->id)
    , warn_set_standalone_master(true)
    , master_gtid_domain(-1)
    , external_master_port(PORT_UNKNOWN)
{}

MariaDBMonitor::~MariaDBMonitor()
{}

bool uses_gtid(MariaDBMonitor* mon, MXS_MONITORED_SERVER* mon_server, json_t** error_out)
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
bool mysql_switchover_check_current(const MariaDBMonitor* mon,
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
bool failover_check(MariaDBMonitor* mon, json_t** error_out)
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
    MariaDBMonitor* handle = static_cast<MariaDBMonitor*>(mon->handle);

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
        bool switched = handle->do_switchover(current_master, new_master, error_out);

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
        MariaDBMonitor* handle = static_cast<MariaDBMonitor*>(mon->handle);
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
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(mon->handle);
    rv = failover_check(handle, output);
    if (rv)
    {
        rv = handle->do_failover(output);
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
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(mon->handle);
    if (handle->cluster_can_be_joined())
    {
        MXS_MONITORED_SERVER* mon_server = mon_get_monitored_server(mon, rejoin_server);
        if (mon_server)
        {
            MXS_MONITORED_SERVER* master = handle->master;
            MySqlServerInfo* master_info = get_server_info(handle, master);
            MySqlServerInfo* server_info = get_server_info(handle, mon_server);

            if (handle->server_is_rejoin_suspect(mon_server, master_info) &&
                handle->update_gtids(master, master_info) &&
                can_replicate_from(handle, mon_server, server_info, master, master_info))
            {
                ServerVector joinable_server;
                joinable_server.push_back(mon_server);
                if (handle->do_rejoin(joinable_server) == 1)
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
        MXS_NOTICE("Initialise the MariaDB Monitor module.");
        static const char ARG_MONITOR_DESC[] = "Monitor name (from configuration file)";
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

/**
 * Initialize the server info hashtable
 *
 * @param handle MariaDB monitor handle
 */
void init_server_info(MariaDBMonitor *handle)
{
    ServerInfoMap& infos = handle->server_info;
    infos.clear();

    for (MXS_MONITORED_SERVER* server = handle->monitor->monitored_servers; server; server = server->next)
    {
        ServerInfoMap::value_type new_val(server, MySqlServerInfo());
        infos.insert(new_val);
    }
}

MySqlServerInfo* get_server_info(MariaDBMonitor* handle, const MXS_MONITORED_SERVER* db)
{
    ServerInfoMap::iterator iter = handle->server_info.find(db);
    ss_dassert(iter != handle->server_info.end());
    return &iter->second;
}

const MySqlServerInfo* get_server_info(const MariaDBMonitor* handle, const MXS_MONITORED_SERVER* db)
{
    return get_server_info(const_cast<MariaDBMonitor*>(handle), db);
}

bool MariaDBMonitor::set_replication_credentials(const MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    string repl_user = config_get_string(params, CN_REPLICATION_USER);
    string repl_pw = config_get_string(params, CN_REPLICATION_PASSWORD);

    if (repl_user.empty() && repl_pw.empty())
    {
        // No replication credentials defined, use monitor credentials
        repl_user = monitor->user;
        repl_pw = monitor->password;
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

MariaDBMonitor* MariaDBMonitor::start_monitor(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(monitor->handle);
    if (handle == NULL)
    {
        handle = new MariaDBMonitor(monitor);
    }

    /** Always reset these values */
    handle->shutdown = 0;
    handle->master = NULL; // TODO: Is this correct? May not be ok to delete this info every time monitor
                           // restarts.
    init_server_info(handle); // The above question also applies to this.

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
        else
        {
            handle->status = MXS_MONITOR_RUNNING;
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
    detectStaleMaster = config_get_bool(params, "detect_stale_master");
    detectStaleSlave = config_get_bool(params, "detect_stale_slave");
    replicationHeartbeat = config_get_bool(params, "detect_replication_lag");
    multimaster = config_get_bool(params, "multimaster");
    ignore_external_masters = config_get_bool(params, "ignore_external_masters");
    detect_standalone_master = config_get_bool(params, "detect_standalone_master");
    failcount = config_get_integer(params, CN_FAILCOUNT);
    allow_cluster_recovery = config_get_bool(params, "allow_cluster_recovery");
    mysql51_replication = config_get_bool(params, "mysql51_replication");
    script = config_get_string(params, "script");
    events = config_get_enum(params, "events", mxs_monitor_event_enum_values);
    failover_timeout = config_get_integer(params, CN_FAILOVER_TIMEOUT);
    switchover_timeout = config_get_integer(params, CN_SWITCHOVER_TIMEOUT);
    auto_failover = config_get_bool(params, CN_AUTO_FAILOVER);
    auto_rejoin = config_get_bool(params, CN_AUTO_REJOIN);
    verify_master_failure = config_get_bool(params, CN_VERIFY_MASTER_FAILURE);
    master_failure_timeout = config_get_integer(params, CN_MASTER_FAILURE_TIMEOUT);

    excluded_servers.clear();
    MXS_MONITORED_SERVER** excluded_array = NULL;
    int n_excluded = mon_config_get_servers(params, CN_NO_PROMOTE_SERVERS, monitor, &excluded_array);
    for (int i = 0; i < n_excluded; i++)
    {
        excluded_servers.push_back(excluded_array[i]);
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
 * Start the monitor instance and return the instance data. This function creates a thread to
 * execute the monitoring. Use stopMonitor() to stop the thread.
 *
 * @param monitor General monitor data
 * @param params Configuration parameters
 * @return A pointer to MariaDBMonitor specific data. Should be stored in MXS_MONITOR's "handle"-field.
 */
static void* startMonitor(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER* params)
{
    return MariaDBMonitor::start_monitor(monitor, params);
}

void MariaDBMonitor::stop_monitor()
{
    shutdown = 1;
    thread_wait(m_thread);
}

/**
 * Stop a running monitor
 *
 * @param mon  The monitor that should be stopped.
 */
static void stopMonitor(MXS_MONITOR *mon)
{
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(mon->handle);
    handle->stop_monitor();
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

    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(mon->handle);

    if (handle->status == MXS_MONITOR_RUNNING)
    {
        stopMonitor(mon);
        actually_stopped = true;
    }

    return actually_stopped;
}

static string monitored_servers_to_string(const ServerVector& array)
{
    string rval;
    size_t array_size = array.size();
    if (array_size > 0)
    {
        const char* separator = "";
        for (size_t i = 0; i < array_size; i++)
        {
            rval += separator;
            rval += array[i]->server->unique_name;
            separator = ",";
        }
    }
    return rval;
}
/**
 * Daignostic interface
 *
 * @param dcb   DCB to print diagnostics
 * @param arg   The monitor handle
 */
static void diagnostics(DCB *dcb, const MXS_MONITOR *mon)
{
    const MariaDBMonitor *handle = (const MariaDBMonitor *)mon->handle;

    dcb_printf(dcb, "Automatic failover:     %s\n", handle->auto_failover ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Failcount:              %d\n", handle->failcount);
    dcb_printf(dcb, "Failover timeout:       %u\n", handle->failover_timeout);
    dcb_printf(dcb, "Switchover timeout:     %u\n", handle->switchover_timeout);
    dcb_printf(dcb, "Automatic rejoin:       %s\n", handle->auto_rejoin ? "Enabled" : "Disabled");
    dcb_printf(dcb, "MaxScale monitor ID:    %lu\n", handle->id);
    dcb_printf(dcb, "Detect replication lag: %s\n", (handle->replicationHeartbeat) ? "Enabled" : "Disabled");
    dcb_printf(dcb, "Detect stale master:    %s\n", (handle->detectStaleMaster == 1) ?
               "Enabled" : "Disabled");
    if (handle->excluded_servers.size() > 0)
    {
        dcb_printf(dcb, "Non-promotable servers (failover): ");
        dcb_printf(dcb, "%s\n", monitored_servers_to_string(handle->excluded_servers).c_str());
    }

    dcb_printf(dcb, "\nServer information:\n-------------------\n\n");
    for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db; db = db->next)
    {
        const MySqlServerInfo* serv_info = get_server_info(handle, db);
        dcb_printf(dcb, "Server:                 %s\n", db->server->unique_name);
        dcb_printf(dcb, "Server ID:              %" PRId64 "\n", serv_info->server_id);
        dcb_printf(dcb, "Read only:              %s\n", serv_info->read_only ? "YES" : "NO");
        dcb_printf(dcb, "Slave configured:       %s\n", serv_info->slave_configured ? "YES" : "NO");
        if (serv_info->slave_configured)
        {
            dcb_printf(dcb, "Slave IO running:       %s\n", serv_info->slave_status.slave_io_running ? "YES" : "NO");
            dcb_printf(dcb, "Slave SQL running:      %s\n", serv_info->slave_status.slave_sql_running ? "YES" : "NO");
            dcb_printf(dcb, "Master ID:              %" PRId64 "\n", serv_info->slave_status.master_server_id);
            dcb_printf(dcb, "Master binlog file:     %s\n", serv_info->slave_status.master_log_file.c_str());
            dcb_printf(dcb, "Master binlog position: %lu\n", serv_info->slave_status.read_master_log_pos);
        }
        if (serv_info->gtid_current_pos.server_id != SERVER_ID_UNKNOWN)
        {
            dcb_printf(dcb, "Gtid current position:  %s\n",
                       serv_info->gtid_current_pos.to_string().c_str());
        }
        if (serv_info->gtid_binlog_pos.server_id != SERVER_ID_UNKNOWN)
        {
            dcb_printf(dcb, "Gtid binlog position:   %s\n",
                       serv_info->gtid_current_pos.to_string().c_str());
        }
        if (serv_info->slave_status.gtid_io_pos.server_id != SERVER_ID_UNKNOWN)
        {
            dcb_printf(dcb, "Gtid slave IO position: %s\n",
                       serv_info->slave_status.gtid_io_pos.to_string().c_str());
        }
        if (handle->multimaster)
        {
            dcb_printf(dcb, "Master group:           %d\n", serv_info->group);
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

    const MariaDBMonitor *handle = (const MariaDBMonitor *)mon->handle;
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

    if (!handle->script.empty())
    {
        json_object_set_new(rval, "script", json_string(handle->script.c_str()));
    }
    if (handle->excluded_servers.size() > 0)
    {
        string list = monitored_servers_to_string(handle->excluded_servers);
        json_object_set_new(rval, CN_NO_PROMOTE_SERVERS, json_string(list.c_str()));
    }
    if (mon->monitored_servers)
    {
        json_t* arr = json_array();

        for (MXS_MONITORED_SERVER *db = mon->monitored_servers; db; db = db->next)
        {
            json_t* srv = json_object();
            const MySqlServerInfo* serv_info = get_server_info(handle, db);
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

bool do_show_slave_status(MariaDBMonitor* mon,
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
                if (server_version != MYSQL_SERVER_VERSION_51)
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
static bool slave_receiving_events(MariaDBMonitor* handle)
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

static inline void monitor_mysql_db(MariaDBMonitor* mon,
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
    MariaDBMonitor *handle = static_cast<MariaDBMonitor*>(mon->handle);

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
 * @param handle    The Monitor object
 * @param database  The database to probe
 */
static void
monitorDatabase(MXS_MONITOR *mon, MXS_MONITORED_SERVER *database)
{
    MariaDBMonitor* handle = static_cast<MariaDBMonitor*>(mon->handle);

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
    /* If gtid domain exists and server is 10.0, update gtid:s */
    if (handle->master_gtid_domain >= 0 && serv_info->version == MYSQL_SERVER_VERSION_100)
    {
        handle->update_gtids(database, serv_info);
    }
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
void find_graph_cycles(MariaDBMonitor *handle, MXS_MONITORED_SERVER *database, int nservers)
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
bool standalone_master_required(MariaDBMonitor *handle, MXS_MONITORED_SERVER *db)
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
bool set_standalone_master(MariaDBMonitor *handle, MXS_MONITORED_SERVER *db)
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

bool failover_not_possible(MariaDBMonitor* handle)
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
    MariaDBMonitor *handle  = (MariaDBMonitor *) arg;
    MXS_MONITOR* mon = handle->monitor;
    MXS_MONITORED_SERVER *ptr;
    bool replication_heartbeat;
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
            MySqlServerInfo* master_info = get_server_info(handle, handle->master);
            // Update cluster gtid domain
            int64_t domain = master_info->gtid_domain_id;
            if (handle->master_gtid_domain >= 0 && domain != handle->master_gtid_domain)
            {
                MXS_NOTICE("Gtid domain id of master has changed: %" PRId64 " -> %" PRId64 ".",
                         handle->master_gtid_domain, domain);
            }
            handle->master_gtid_domain = domain;

            // Update cluster external master
            if (SERVER_IS_SLAVE_OF_EXTERNAL_MASTER(handle->master->server))
            {
                if (master_info->slave_status.master_host != handle->external_master_host ||
                    master_info->slave_status.master_port != handle->external_master_port)
                {
                    const string new_ext_host =  master_info->slave_status.master_host;
                    const int new_ext_port = master_info->slave_status.master_port;
                    if (handle->external_master_port == PORT_UNKNOWN)
                    {
                        MXS_NOTICE("Cluster master server is replicating from an external master: %s:%d",
                                   new_ext_host.c_str(), new_ext_port);
                    }
                    else
                    {
                        MXS_NOTICE("The external master of the cluster has changed: %s:%d -> %s:%d.",
                                   handle->external_master_host.c_str(), handle->external_master_port,
                                   new_ext_host.c_str(), new_ext_port);
                    }
                    handle->external_master_host = new_ext_host;
                    handle->external_master_port = new_ext_port;
                }
            }
            else
            {
                if (handle->external_master_port != PORT_UNKNOWN)
                {
                    MXS_NOTICE("Cluster lost the external master.");
                }
                handle->external_master_host.clear();
                handle->external_master_port = PORT_UNKNOWN;
            }
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

        ss_dassert(root_master == NULL || handle->master == root_master);
        ss_dassert(!root_master ||
                   ((root_master->server->status & (SERVER_SLAVE | SERVER_MASTER))
                    != (SERVER_SLAVE | SERVER_MASTER)));

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(mon, handle->script.c_str(), handle->events);
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

        /* Generate the replication heartbeat event by performing an update */
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
        if (!config_get_global_options()->passive && handle->auto_rejoin &&
            !failover_performed && handle->cluster_can_be_joined())
        {
            // Check if any servers should be autojoined to the cluster
            ServerVector joinable_servers;
            if (handle->get_joinable_servers(&joinable_servers))
            {
                uint32_t joins = handle->do_rejoin(joinable_servers);
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
 * Fetch a node by node_id
 *
 * @param ptr     The list of servers to monitor
 * @param node_id The server_id to fetch
 *
 * @return The server with the required server_id
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
 * Fetch a slave node from a node_id
 *
 * @param ptr                The list of servers to monitor
 * @param node_id            The server_id to fetch
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
static void set_master_heartbeat(MariaDBMonitor *handle, MXS_MONITORED_SERVER *database)
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
    MariaDBMonitor *handle = (MariaDBMonitor*) mon->handle;
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
 * from a set of monitored servers and returns the root server with
 * SERVER_MASTER bit. The tree is computed even for servers in 'maintenance' mode.
 *
 * @param handle    The monitor handle
 * @param num_servers   The number of servers monitored
 * @return      The server at root level with SERVER_MASTER bit
 */

static MXS_MONITORED_SERVER *get_replication_tree(MXS_MONITOR *mon, int num_servers)
{
    MariaDBMonitor* handle = (MariaDBMonitor*) mon->handle;
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

        /** Either this node doesn't replicate from a master or the master
         * where it replicates from is not configured to this monitor. */
        if (node_id < 1 ||
            getServerByNodeId(mon->monitored_servers, node_id) == NULL)
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
bool mon_process_failover(MariaDBMonitor* monitor, uint32_t failover_timeout, bool* cluster_modified_out)
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
            rval = failover_check(monitor, NULL) && monitor->do_failover(NULL);
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
MySqlServerInfo* update_slave_info(MariaDBMonitor* mon, MXS_MONITORED_SERVER* server)
{
    MySqlServerInfo* info = get_server_info(mon, server);
    if (info->slave_status.slave_sql_running &&
        update_replication_settings(server, info) &&
        mon->update_gtids(server, info) &&
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
bool check_replication_settings(const MXS_MONITORED_SERVER* server, MySqlServerInfo* server_info,
                                print_repl_warnings_t print_warnings)
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
 * Query one row of results, save strings to array. Any additional rows are ignored.
 *
 * @param database The database to query.
 * @param query The query to execute.
 * @param expected_cols How many columns the result should have.
 * @param output The output array to populate.
 * @return True on success.
 */
bool query_one_row(MXS_MONITORED_SERVER *database, const char* query, unsigned int expected_cols,
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
            MXS_ERROR("Unexpected result for '%s'. Expected %d columns, got %d. Server version: %s",
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
 * @param info Server info structure for saving result TODO: remove
 * @return True if successful
 */
bool MariaDBMonitor::update_gtids(MXS_MONITORED_SERVER *database, MySqlServerInfo* info)
{
    StringVector row;
    const char query[] = "SELECT @@gtid_current_pos, @@gtid_binlog_pos;";
    const int ind_current_pos = 0;
    const int ind_binlog_pos = 1;
    int64_t domain = master_gtid_domain;
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

string generate_master_gtid_wait_cmd(const Gtid& gtid, double timeout)
{
    std::stringstream query_ss;
    query_ss << "SELECT MASTER_GTID_WAIT(\"" << gtid.to_string() << "\", " << timeout << ");";
    return query_ss.str();
}

/**
 * Get MariaDB connection error strings from all the given servers, form one string.
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
bool can_replicate_from(MariaDBMonitor* mon,
                               MXS_MONITORED_SERVER* slave, MySqlServerInfo* slave_info,
                               MXS_MONITORED_SERVER* master, MySqlServerInfo* master_info)
{
    bool rval = false;
    if (mon->update_gtids(slave, slave_info))
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
 * Set a monitor config parameter to "false". The effect persists over stopMonitor/startMonitor but not
 * MaxScale restart. Only use on boolean config settings.
 *
 * @param mon Cluster monitor
 * @param setting_name Setting to disable
 */
static void disable_setting(MariaDBMonitor* mon, const char* setting)
{
    MXS_CONFIG_PARAMETER p = {};
    p.name = const_cast<char*>(setting);
    p.value = const_cast<char*>("false");
    monitorAddParameters(mon->monitor, &p);
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
