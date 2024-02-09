/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "monitor_commands.hh"
#include <maxbase/format.hh>
#include <maxbase/http.hh>
#include <maxscale/modulecmd.hh>
#include "mariadbmon.hh"
#include "ssh_utils.hh"

using maxscale::Monitor;
using maxscale::MonitorServer;
using std::string;
using std::move;
using RType = ssh_util::CmdResult::Type;

namespace
{
// Execution mode for commands.
enum class ExecMode
{
    SYNC,   /**< Function waits for completion or error */
    ASYNC   /**< Function only schedules the operation and will not wait */
};

const char err_passive_mode[] = "%s requested but not performed, as MaxScale is in passive mode.";
const char failover_cmd[] = "failover";
const char switchover_cmd[] = "switchover";
const char rejoin_cmd[] = "rejoin";
const char reset_repl_cmd[] = "reset-replication";
const char release_locks_cmd[] = "release-locks";
const char cs_add_node_cmd[] = "cs-add-node";
const char cs_remove_node_cmd[] = "cs-remove-node";
const char cs_get_status_cmd[] = "cs-get-status";
const char cs_start_cluster_cmd[] = "cs-start-cluster";
const char cs_stop_cluster_cmd[] = "cs-stop-cluster";
const char cs_set_readonly_cmd[] = "cs-set-readonly";
const char cs_set_readwrite_cmd[] = "cs-set-readwrite";
const char rebuild_server_cmd[] = "rebuild-server";
const char mask[] = "******";

const string rebuild_datadir = "/var/lib/mysql";    // configurable?
const char link_test_msg[] = "Test message";
const int socat_timeout_s = 5;      // TODO: configurable?

bool manual_switchover(ExecMode mode, const MODULECMD_ARG* args, json_t** error_out);
bool manual_failover(ExecMode mode, const MODULECMD_ARG* args, json_t** output);
bool manual_rejoin(ExecMode mode, const MODULECMD_ARG* args, json_t** output);
bool manual_reset_replication(ExecMode mode, const MODULECMD_ARG* args, json_t** output);
bool release_locks(ExecMode mode, const MODULECMD_ARG* args, json_t** output);

std::tuple<MariaDBMonitor*, string, string> read_args(const MODULECMD_ARG& args);
std::tuple<bool, std::chrono::seconds>      get_timeout(const string& timeout_str, json_t** output);

/**
 * Command handlers. These are called by the rest-api.
 */

// switchover
bool handle_manual_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_switchover(ExecMode::SYNC, args, error_out);
}

// async-switchover
bool handle_async_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_switchover(ExecMode::ASYNC, args, error_out);
}

// failover
bool handle_manual_failover(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_failover(ExecMode::SYNC, args, error_out);
}

// async-failover
bool handle_async_failover(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_failover(ExecMode::ASYNC, args, error_out);
}

// rejoin
bool handle_manual_rejoin(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_rejoin(ExecMode::SYNC, args, error_out);
}

// async-rejoin
bool handle_async_rejoin(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_rejoin(ExecMode::ASYNC, args, error_out);
}

// reset-replication
bool handle_manual_reset_replication(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_reset_replication(ExecMode::SYNC, args, error_out);
}

// async-reset-replication
bool handle_async_reset_replication(const MODULECMD_ARG* args, json_t** error_out)
{
    return manual_reset_replication(ExecMode::ASYNC, args, error_out);
}

// release-locks
bool handle_manual_release_locks(const MODULECMD_ARG* args, json_t** output)
{
    return release_locks(ExecMode::SYNC, args, output);
}

// async-release-locks
bool handle_async_release_locks(const MODULECMD_ARG* args, json_t** output)
{
    return release_locks(ExecMode::ASYNC, args, output);
}

bool handle_fetch_cmd_result(const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    Monitor* mon = args->argv[0].value.monitor;
    auto mariamon = static_cast<MariaDBMonitor*>(mon);
    mariamon->fetch_cmd_result(output);
    return true;    // result fetch always works, even if there is nothing to return
}

bool handle_cancel_cmd(const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    Monitor* mon = args->argv[0].value.monitor;
    auto mariamon = static_cast<MariaDBMonitor*>(mon);
    return mariamon->cancel_cmd(output);
}

bool handle_async_cs_add_node(const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;
    auto [mon, host, timeout_str] = read_args(*args);
    auto [to_ok, timeout] = get_timeout(timeout_str, output);

    if (to_ok)
    {
        rval = mon->schedule_cs_add_node(host, timeout, output);
    }
    return rval;
}

bool handle_async_cs_remove_node(const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;
    auto [mon, host, timeout_str] = read_args(*args);
    auto [to_ok, timeout] = get_timeout(timeout_str, output);

    if (to_ok)
    {
        rval = mon->schedule_cs_remove_node(host, timeout, output);
    }
    return rval;
}

bool handle_cs_get_status(const MODULECMD_ARG* args, json_t** output)
{
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    return mon->run_cs_get_status(output);
}

bool handle_async_cs_get_status(const MODULECMD_ARG* args, json_t** output)
{
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    return mon->schedule_cs_get_status(output);
}

bool async_cs_run_cmd_with_timeout(const std::function<bool(MariaDBMonitor*, std::chrono::seconds)>& func,
                                   const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    string timeout_str = args->argv[1].value.string;
    auto [to_ok, timeout] = get_timeout(timeout_str, output);
    if (to_ok)
    {
        rval = func(mon, timeout);
    }
    return rval;
}

bool handle_async_cs_start_cluster(const MODULECMD_ARG* args, json_t** output)
{
    auto func = [output](MariaDBMonitor* mon, std::chrono::seconds timeout) {
        return mon->schedule_cs_start_cluster(timeout, output);
    };
    return async_cs_run_cmd_with_timeout(func, args, output);
}

bool handle_async_cs_stop_cluster(const MODULECMD_ARG* args, json_t** output)
{
    auto func = [output](MariaDBMonitor* mon, std::chrono::seconds timeout) {
        return mon->schedule_cs_stop_cluster(timeout, output);
    };
    return async_cs_run_cmd_with_timeout(func, args, output);
}

bool handle_async_cs_set_readonly(const MODULECMD_ARG* args, json_t** output)
{
    auto func = [output](MariaDBMonitor* mon, std::chrono::seconds timeout) {
        return mon->schedule_cs_set_readonly(timeout, output);
    };
    return async_cs_run_cmd_with_timeout(func, args, output);
}

bool handle_async_cs_set_readwrite(const MODULECMD_ARG* args, json_t** output)
{
    auto func = [output](MariaDBMonitor* mon, std::chrono::seconds timeout) {
        return mon->schedule_cs_set_readwrite(timeout, output);
    };
    return async_cs_run_cmd_with_timeout(func, args, output);
}

bool handle_async_rebuild_server(const MODULECMD_ARG* args, json_t** output)
{
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    SERVER* target = args->argv[1].value.server;
    SERVER* source = args->argc > 2 ? args->argv[2].value.server : nullptr;
    return mon->schedule_rebuild_server(target, source, output);
}

/**
 * Run manual switchover.
 *
 * @param mode    Execution mode
 * @param args    The provided arguments
 * @param output  Pointer where to place output object
 *
 * @return True, if the command was executed/scheduled, false otherwise.
 */
bool manual_switchover(ExecMode mode, const MODULECMD_ARG* args, json_t** error_out)
{
    mxb_assert((args->argc >= 1) && (args->argc <= 3));
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert((args->argc < 2) || (MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER));
    mxb_assert((args->argc < 3) || (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER));

    bool rval = false;
    if (mxs::Config::get().passive.get())
    {
        PRINT_MXS_JSON_ERROR(error_out, err_passive_mode, "Switchover");
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon);
        SERVER* promotion_server = (args->argc >= 2) ? args->argv[1].value.server : nullptr;
        SERVER* demotion_server = (args->argc == 3) ? args->argv[2].value.server : nullptr;

        switch (mode)
        {
        case ExecMode::SYNC:
            rval = handle->run_manual_switchover(promotion_server, demotion_server, error_out);
            break;

        case ExecMode::ASYNC:
            rval = handle->schedule_async_switchover(promotion_server, demotion_server, error_out);
            break;
        }
    }
    return rval;
}

/**
 * Run manual failover.
 *
 * @paran mode Execution mode
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool manual_failover(ExecMode mode, const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    bool rv = false;

    if (mxs::Config::get().passive.get())
    {
        PRINT_MXS_JSON_ERROR(output, err_passive_mode, "Failover");
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon);

        switch (mode)
        {
        case ExecMode::SYNC:
            rv = handle->run_manual_failover(output);
            break;

        case ExecMode::ASYNC:
            rv = handle->schedule_async_failover(output);
            break;
        }
    }
    return rv;
}

/**
 * Run manual rejoin.
 *
 * @paran mode Execution mode
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool manual_rejoin(ExecMode mode, const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 2);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    bool rv = false;
    if (mxs::Config::get().passive.get())
    {
        PRINT_MXS_JSON_ERROR(output, "Rejoin requested but not performed, as MaxScale is in passive mode.");
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        SERVER* server = args->argv[1].value.server;
        auto handle = static_cast<MariaDBMonitor*>(mon);

        switch (mode)
        {
        case ExecMode::SYNC:
            rv = handle->run_manual_rejoin(server, output);
            break;

        case ExecMode::ASYNC:
            rv = handle->schedule_async_rejoin(server, output);
            break;
        }
    }
    return rv;
}

/**
 * Run reset replication.
 *
 * @paran mode Execution mode
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool manual_reset_replication(ExecMode mode, const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc >= 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(args->argc == 1 || MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    bool rv = false;
    if (mxs::Config::get().passive.get())
    {
        PRINT_MXS_JSON_ERROR(output, "Replication reset requested but not performed, as MaxScale is in "
                                     "passive mode.");
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        SERVER* server = args->argv[1].value.server;
        auto handle = static_cast<MariaDBMonitor*>(mon);

        switch (mode)
        {
        case ExecMode::SYNC:
            rv = handle->run_manual_reset_replication(server, output);
            break;

        case ExecMode::ASYNC:
            rv = handle->schedule_reset_replication(server, output);
            break;
        }
    }
    return rv;
}

/**
 * Run release locks.
 *
 * @paran mode Execution mode
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool release_locks(ExecMode mode, const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);

    bool rv = false;
    Monitor* mon = args->argv[0].value.monitor;
    auto mariamon = static_cast<MariaDBMonitor*>(mon);

    switch (mode)
    {
    case ExecMode::SYNC:
        rv = mariamon->run_release_locks(output);
        break;

    case ExecMode::ASYNC:
        rv = mariamon->schedule_release_locks(output);
        break;
    }
    return rv;
}

std::tuple<MariaDBMonitor*, string, string> read_args(const MODULECMD_ARG& args)
{
    mxb_assert(MODULECMD_GET_TYPE(&args.argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(args.argc <= 1 || MODULECMD_GET_TYPE(&args.argv[1].type) == MODULECMD_ARG_STRING);
    mxb_assert(args.argc <= 2 || MODULECMD_GET_TYPE(&args.argv[2].type) == MODULECMD_ARG_STRING);

    MariaDBMonitor* mon = static_cast<MariaDBMonitor*>(args.argv[0].value.monitor);
    string text1 = args.argc >= 2 ? args.argv[1].value.string : "";
    string text2 = args.argc >= 3 ? args.argv[2].value.string : "";

    return {mon, text1, text2};
}

std::tuple<bool, std::chrono::seconds> get_timeout(const string& timeout_str, json_t** output)
{
    std::chrono::milliseconds duration;
    mxs::config::DurationUnit unit;
    std::chrono::seconds timeout = 0s;

    bool rv = get_suffixed_duration(timeout_str.c_str(), &duration, &unit);
    if (rv)
    {
        if (unit == mxs::config::DURATION_IN_MILLISECONDS)
        {
            MXB_WARNING("Duration specified in milliseconds, will be converted to seconds.");
        }

        timeout = std::chrono::duration_cast<std::chrono::seconds>(duration);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(output,
                             "Timeout must be specified with a 's', 'm', or 'h' suffix. 'ms' is accepted "
                             "but the time will be converted to seconds.");
        rv = false;
    }

    return {rv, timeout};
}
}

void register_monitor_commands()
{
    static const char ARG_MONITOR_DESC[] = "Monitor name";
    static modulecmd_arg_type_t switchover_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC           },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "New master (optional)"    },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Current master (optional)"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, switchover_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Perform master switchover");

    modulecmd_register_command(MXB_MODULE_NAME, "async-switchover", MODULECMD_TYPE_ACTIVE,
                               handle_async_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Schedule master switchover. Does not wait for completion");

    static modulecmd_arg_type_t failover_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXB_MODULE_NAME, failover_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_failover, MXS_ARRAY_NELEMS(failover_argv), failover_argv,
                               "Perform master failover");

    modulecmd_register_command(MXB_MODULE_NAME, "async-failover", MODULECMD_TYPE_ACTIVE,
                               handle_async_failover, MXS_ARRAY_NELEMS(failover_argv), failover_argv,
                               "Schedule master failover. Does not wait for completion.");

    static modulecmd_arg_type_t rejoin_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
        {MODULECMD_ARG_SERVER,                                      "Joining server"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, rejoin_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_rejoin, MXS_ARRAY_NELEMS(rejoin_argv), rejoin_argv,
                               "Rejoin server to a cluster");

    modulecmd_register_command(MXB_MODULE_NAME, "async-rejoin", MODULECMD_TYPE_ACTIVE,
                               handle_async_rejoin, MXS_ARRAY_NELEMS(rejoin_argv), rejoin_argv,
                               "Rejoin server to a cluster. Does not wait for completion.");

    static modulecmd_arg_type_t reset_gtid_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC          },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Master server (optional)"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, reset_repl_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_reset_replication,
                               MXS_ARRAY_NELEMS(reset_gtid_argv), reset_gtid_argv,
                               "Delete slave connections, delete binary logs and "
                               "set up replication (dangerous)");

    modulecmd_register_command(MXB_MODULE_NAME, "async-reset-replication", MODULECMD_TYPE_ACTIVE,
                               handle_async_reset_replication,
                               MXS_ARRAY_NELEMS(reset_gtid_argv), reset_gtid_argv,
                               "Delete slave connections, delete binary logs and "
                               "set up replication (dangerous). Does not wait for completion.");

    static modulecmd_arg_type_t release_locks_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXB_MODULE_NAME, release_locks_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_release_locks,
                               MXS_ARRAY_NELEMS(release_locks_argv), release_locks_argv,
                               "Release any held server locks for 1 minute.");

    modulecmd_register_command(MXB_MODULE_NAME, "async-release-locks", MODULECMD_TYPE_ACTIVE,
                               handle_async_release_locks,
                               MXS_ARRAY_NELEMS(release_locks_argv), release_locks_argv,
                               "Release any held server locks for 1 minute. Does not wait for completion.");

    static modulecmd_arg_type_t fetch_cmd_result_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXB_MODULE_NAME, "fetch-cmd-result", MODULECMD_TYPE_PASSIVE,
                               handle_fetch_cmd_result,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               "Fetch result of the last scheduled command.");

    modulecmd_register_command(MXB_MODULE_NAME, "cancel-cmd", MODULECMD_TYPE_ACTIVE, handle_cancel_cmd,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               "Cancel the last scheduled command.");

    const modulecmd_arg_type_t csmon_add_node_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_STRING, "Hostname/IP of node to add to ColumnStore cluster" },
        { MODULECMD_ARG_STRING, "Timeout" }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-add-node", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_add_node,
                               MXS_ARRAY_NELEMS(csmon_add_node_argv), csmon_add_node_argv,
                               "Add a node to a ColumnStore cluster. Does not wait for completion.");

    const modulecmd_arg_type_t csmon_remove_node_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_STRING, "Hostname/IP of node to remove from ColumnStore cluster" },
        { MODULECMD_ARG_STRING, "Timeout" }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-remove-node", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_remove_node,
                               MXS_ARRAY_NELEMS(csmon_remove_node_argv), csmon_remove_node_argv,
                               "Remove a node from a ColumnStore cluster. Does not wait for completion.");

    modulecmd_register_command(MXB_MODULE_NAME, "cs-get-status", MODULECMD_TYPE_ACTIVE,
                               handle_cs_get_status,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               "Get ColumnStore cluster status.");

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-get-status", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_get_status,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               "Get ColumnStore cluster status. Does not wait for completion.");

    const modulecmd_arg_type_t csmon_cmd_timeout_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_STRING, "Timeout" }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-start-cluster", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_start_cluster,
                               MXS_ARRAY_NELEMS(csmon_cmd_timeout_argv), csmon_cmd_timeout_argv,
                               "Start ColumnStore cluster. Does not wait for completion.");

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-stop-cluster", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_stop_cluster,
                               MXS_ARRAY_NELEMS(csmon_cmd_timeout_argv), csmon_cmd_timeout_argv,
                               "Stop ColumnStore cluster. Does not wait for completion.");

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-set-readonly", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_set_readonly,
                               MXS_ARRAY_NELEMS(csmon_cmd_timeout_argv), csmon_cmd_timeout_argv,
                               "Set ColumnStore cluster read-only. Does not wait for completion.");

    modulecmd_register_command(MXB_MODULE_NAME, "async-cs-set-readwrite", MODULECMD_TYPE_ACTIVE,
                               handle_async_cs_set_readwrite,
                               MXS_ARRAY_NELEMS(csmon_cmd_timeout_argv), csmon_cmd_timeout_argv,
                               "Set ColumnStore cluster readwrite. Does not wait for completion.");

    const modulecmd_arg_type_t rebuild_server_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER, "Target server" },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Source server (optional)" }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "async-rebuild-server", MODULECMD_TYPE_ACTIVE,
                               handle_async_rebuild_server,
                               MXS_ARRAY_NELEMS(rebuild_server_argv), rebuild_server_argv,
                               "Rebuild a server with mariabackup. Does not wait for completion.");
}

bool MariaDBMonitor::run_manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out)
{
    auto func = [this, new_master, current_master](){
        return manual_switchover(new_master, current_master);
    };
    return execute_manual_command(func, switchover_cmd, error_out);
}

bool MariaDBMonitor::schedule_async_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out)
{
    auto func = [this, new_master, current_master](){
        return manual_switchover(new_master, current_master);
    };
    return schedule_manual_command(func, switchover_cmd, error_out);
}

bool MariaDBMonitor::run_manual_failover(json_t** error_out)
{
    auto func = [this](){
        return manual_failover();
    };
    return execute_manual_command(func, failover_cmd, error_out);
}

bool MariaDBMonitor::schedule_async_failover(json_t** error_out)
{
    auto func = [this](){
        return manual_failover();
    };
    return schedule_manual_command(func, failover_cmd, error_out);
}

bool MariaDBMonitor::run_manual_rejoin(SERVER* rejoin_server, json_t** error_out)
{
    auto func = [this, rejoin_server](){
        return manual_rejoin(rejoin_server);
    };
    return execute_manual_command(func, rejoin_cmd, error_out);
}

bool MariaDBMonitor::schedule_async_rejoin(SERVER* rejoin_server, json_t** error_out)
{
    auto func = [this, rejoin_server](){
        return manual_rejoin(rejoin_server);
    };
    return schedule_manual_command(func, rejoin_cmd, error_out);
}

bool MariaDBMonitor::run_manual_reset_replication(SERVER* master_server, json_t** error_out)
{
    auto func = [this, master_server](){
        return manual_reset_replication(master_server);
    };
    return execute_manual_command(func, reset_repl_cmd, error_out);
}

bool MariaDBMonitor::schedule_reset_replication(SERVER* master_server, json_t** error_out)
{
    auto func = [this, master_server](){
        return manual_reset_replication(master_server);
    };
    return schedule_manual_command(func, reset_repl_cmd, error_out);
}

bool MariaDBMonitor::run_release_locks(json_t** error_out)
{
    auto func = [this](){
        return manual_release_locks();
    };
    return execute_manual_command(func, release_locks_cmd, error_out);
}

bool MariaDBMonitor::schedule_release_locks(json_t** error_out)
{
    auto func = [this](){
        return manual_release_locks();
    };
    return schedule_manual_command(func, release_locks_cmd, error_out);
}

mon_op::Result MariaDBMonitor::manual_release_locks()
{
    // Manual commands should only run in the main monitor thread.
    mxb_assert(mxb::Worker::get_current()->id() == this->id());
    mxb_assert(m_op_info.exec_state == mon_op::ExecState::RUNNING);

    mon_op::Result rval;
    auto& error_out = rval.output;

    bool success = false;
    if (server_locks_in_use())
    {
        std::atomic_int released_locks {0};
        auto release_lock_task = [&released_locks](MariaDBServer* server) {
            released_locks += server->release_all_locks();
        };
        execute_task_all_servers(release_lock_task);
        m_locks_info.have_lock_majority.store(false, std::memory_order_relaxed);

        // Set next locking attempt 1 minute to the future.
        m_locks_info.next_lock_attempt_delay = std::chrono::minutes(1);
        m_locks_info.last_locking_attempt.restart();

        int released = released_locks.load(std::memory_order_relaxed);
        const char LOCK_DELAY_MSG[] = "Will not attempt to reacquire locks for 1 minute.";
        if (released > 0)
        {
            MXB_NOTICE("Released %i lock(s). %s", released, LOCK_DELAY_MSG);
            success = true;
        }
        else
        {
            PRINT_JSON_ERROR(error_out, "Did not release any locks. %s", LOCK_DELAY_MSG);
        }
    }
    else
    {
        PRINT_JSON_ERROR(error_out, "Server locks are not in use, cannot release them.");
    }
    rval.success = success;
    return rval;
}

bool MariaDBMonitor::fetch_cmd_result(json_t** output)
{
    mxb_assert(is_main_worker());
    using ExecState = mon_op::ExecState;
    auto manual_cmd_state = ExecState::DONE;
    string manual_cmd_name;
    bool have_result = false;
    mon_op::Result manual_cmd_result;

    std::unique_lock<std::mutex> lock(m_op_info.lock);
    if (m_op_info.result_info)
    {
        // Deep copy the json since ownership moves.
        manual_cmd_result = m_op_info.result_info->res.deep_copy();
        manual_cmd_name = m_op_info.result_info->cmd_name;
        have_result = true;
    }
    else
    {
        // No results are available. If current operation is a manual one, then make an error message from
        // its info. If not, then a manual op must not have been ran yet as the results of manual op are
        // only removed when a new one is scheduled.
        if (m_op_info.current_op_is_manual)
        {
            manual_cmd_name = m_op_info.op_name;
            manual_cmd_state = m_op_info.exec_state;
        }
    }
    lock.unlock();

    // The string contents here must match with GUI code.
    const char cmd_running_fmt[] = "No manual command results are available, %s is still %s.";
    switch (manual_cmd_state)
    {
    case ExecState::SCHEDULED:
        *output = mxs_json_error_append(*output, cmd_running_fmt, manual_cmd_name.c_str(), "pending");
        break;

    case ExecState::RUNNING:
        *output = mxs_json_error_append(*output, cmd_running_fmt, manual_cmd_name.c_str(), "running");
        break;

    case ExecState::DONE:
        if (have_result)
        {
            // If command has its own output, return that. Otherwise, report success or error.
            if (manual_cmd_result.output.object_size() > 0)
            {
                *output = manual_cmd_result.output.release();
            }
            else if (manual_cmd_result.success)
            {
                *output = json_sprintf("%s completed successfully.", manual_cmd_name.c_str());
            }
            else
            {
                // Command failed, but printed no results.
                *output = json_sprintf("%s failed.", manual_cmd_name.c_str());
            }
        }
        else
        {
            // Command has not been ran.
            *output = mxs_json_error_append(*output, "No manual command results are available.");
        }
        break;
    }
    return true;
}

bool MariaDBMonitor::cancel_cmd(json_t** output)
{
    mxb_assert(is_main_worker());
    using ExecState = mon_op::ExecState;

    bool canceled = false;
    std::lock_guard<std::mutex> guard(m_op_info.lock);
    auto op_state = m_op_info.exec_state.load();
    if (op_state == ExecState::SCHEDULED)
    {
        m_op_info.scheduled_op = nullptr;
        MXB_NOTICE("Scheduled %s canceled.", m_op_info.op_name.c_str());
        m_op_info.op_name.clear();
        m_op_info.exec_state = ExecState::DONE;
        m_op_info.current_op_is_manual = false;
        canceled = true;
    }
    else if (op_state == ExecState::RUNNING)
    {
        // Cannot cancel a running operation from main thread. Set a flag instead.
        m_op_info.cancel_op = true;
        canceled = true;
    }
    else
    {
        *output = mxs_json_error_append(*output, "No manual command is scheduled or running.");
    }
    return canceled;
}

bool MariaDBMonitor::schedule_cs_add_node(const std::string& host, std::chrono::seconds timeout,
                                          json_t** error_out)
{
    auto func = [this, host, timeout]() {
        return manual_cs_add_node(host, timeout);
    };
    return schedule_manual_command(func, cs_add_node_cmd, error_out);
}

bool MariaDBMonitor::schedule_cs_remove_node(const std::string& host, std::chrono::seconds timeout,
                                             json_t** error_out)
{
    auto func = [this, host, timeout]() {
        return manual_cs_remove_node(host, timeout);
    };
    return schedule_manual_command(func, cs_remove_node_cmd, error_out);
}

bool MariaDBMonitor::run_cs_get_status(json_t** output)
{
    auto func = [this]() {
        return manual_cs_get_status();
    };
    return execute_manual_command(func, cs_get_status_cmd, output);
}

bool MariaDBMonitor::schedule_cs_get_status(json_t** output)
{
    auto func = [this]() {
        return manual_cs_get_status();
    };
    return schedule_manual_command(func, cs_get_status_cmd, output);
}

bool MariaDBMonitor::schedule_cs_start_cluster(std::chrono::seconds timeout, json_t** error_out)
{
    auto func = [this, timeout]() {
        return manual_cs_start_cluster(timeout);
    };
    return schedule_manual_command(func, cs_start_cluster_cmd, error_out);
}

bool MariaDBMonitor::schedule_cs_stop_cluster(std::chrono::seconds timeout, json_t** error_out)
{
    auto func = [this, timeout]() {
        return manual_cs_stop_cluster(timeout);
    };
    return schedule_manual_command(func, cs_stop_cluster_cmd, error_out);
}

bool MariaDBMonitor::schedule_cs_set_readonly(std::chrono::seconds timeout, json_t** error_out)
{
    auto func = [this, timeout]() {
        return manual_cs_set_readonly(timeout);
    };
    return schedule_manual_command(func, cs_set_readonly_cmd, error_out);
}

bool MariaDBMonitor::schedule_cs_set_readwrite(std::chrono::seconds timeout, json_t** error_out)
{
    auto func = [this, timeout]() {
        return manual_cs_set_readwrite(timeout);
    };
    return schedule_manual_command(func, cs_set_readwrite_cmd, error_out);
}

MariaDBMonitor::CsRestResult MariaDBMonitor::check_cs_rest_result(const mxb::http::Response& resp)
{
    bool rval = false;
    string err_str;
    mxb::Json json_data(mxb::Json::Type::UNDEFINED);

    if (resp.is_success())
    {
        // The response body should be json text. Parse it.
        if (json_data.load_string(resp.body))
        {
            rval = true;
        }
        else
        {
            err_str = mxb::string_printf("REST-API call succeeded yet returned data was not JSON. %s",
                                         json_data.error_msg().c_str());
        }
    }
    else
    {
        auto rc_desc = mxb::http::Response::to_string(resp.code);

        if (resp.is_fatal())
        {
            err_str = mxb::string_printf("REST-API call failed. Error %d: %s", resp.code, rc_desc);
        }
        else
        {
            err_str = mxb::string_printf("Error %d: %s", resp.code, rc_desc);
        }

        // The response body is json, try parse it and get CS error information.
        mxb::Json cs_error;
        if (cs_error.load_string(resp.body))
        {
            auto cs_err_desc = cs_error.get_string("error");
            if (!cs_err_desc.empty())
            {
                err_str.append(" ColumnStore error: ").append(cs_err_desc);
            }
        }
    }

    return {rval, err_str, json_data};
}

mon_op::Result
MariaDBMonitor::manual_cs_add_node(const std::string& node_host, std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())},
                            {"node", mxb::string_printf("\"%s\"", node_host.c_str())}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::PUT, "node", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not add node '%s' to the ColumnStore cluster. %s",
                                           node_host.c_str(), rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result
MariaDBMonitor::manual_cs_remove_node(const std::string& node_host, std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())},
                            {"node", mxb::string_printf("\"%s\"", node_host.c_str())}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::DELETE, "node", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not remove node '%s' from the ColumnStore cluster: %s",
                                           node_host.c_str(), rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result MariaDBMonitor::manual_cs_get_status()
{
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::GET, "status", {}, 0s);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not fetch status from the ColumnStore cluster: %s",
                                           rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result MariaDBMonitor::manual_cs_start_cluster(std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::PUT, "start", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not start ColumnStore cluster: %s", rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result MariaDBMonitor::manual_cs_stop_cluster(std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::PUT, "shutdown", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not stop ColumnStore cluster: %s", rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result MariaDBMonitor::manual_cs_set_readonly(std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())},
                            {"mode", "\"readonly\""}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::PUT, "mode-set", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not set ColumnStore cluster to read-only mode: %s",
                                           rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

mon_op::Result MariaDBMonitor::manual_cs_set_readwrite(std::chrono::seconds timeout)
{
    RestDataFields input = {{"timeout", std::to_string(timeout.count())},
                            {"mode", "\"readwrite\""}};
    auto [ok, rest_error, rest_output] = run_cs_rest_cmd(HttpCmd::PUT, "mode-set", input, timeout);

    mon_op::Result rval;
    if (ok)
    {
        rval.success = true;
        rval.output = move(rest_output);
    }
    else
    {
        string errmsg = mxb::string_printf("Could not set ColumnStore cluster to read-write mode: %s",
                                           rest_error.c_str());
        PRINT_JSON_ERROR(rval.output, "%s", errmsg.c_str());
    }
    return rval;
}

MariaDBMonitor::CsRestResult
MariaDBMonitor::run_cs_rest_cmd(HttpCmd httcmd, const std::string& rest_cmd, const RestDataFields& data,
                                std::chrono::seconds cs_timeout)
{
    auto& srvs = servers();
    if (srvs.empty())
    {
        return {false, "No valid server to send ColumnStore REST-API command found",
                mxb::Json(mxb::Json::Type::UNDEFINED)};
    }
    else
    {
        // Send the command to the first server. TODO: send to master instead?
        string url = mxb::string_printf("https://%s:%li%s/cluster/%s",
                                        srvs.front()->server->address(), m_settings.cs_admin_port,
                                        m_settings.cs_admin_base_path.c_str(), rest_cmd.c_str());
        string body = "{";
        string sep;
        for (const auto& elem : data)
        {
            body.append(sep).append("\"").append(elem.first).append("\": ").append(elem.second);
            sep = ", ";
        }
        body += "}";

        // Set the timeout to larger than the timeout specified to the ColumnStore daemon. The timeout surely
        // expires first in the daemon and only then in the HTTP library.
        mxb::http::Config http_config = m_http_config;
        http_config.timeout = cs_timeout + std::chrono::seconds(mxb::http::DEFAULT_TIMEOUT);

        mxb::http::Response response;

        switch (httcmd)
        {
        case HttpCmd::GET:
            response = mxb::http::get(url, http_config);
            break;

        case HttpCmd::PUT:
            response = mxb::http::put(url, body, http_config);
            break;

        case HttpCmd::DELETE:
            response = mxb::http::del(url, body, http_config);
            break;
        }

        return check_cs_rest_result(response);
    }
}

bool MariaDBMonitor::schedule_rebuild_server(SERVER* target, SERVER* source, json_t** error_out)
{
    MariaDBServer* rebuild_master = (m_master && m_master->is_master()) ? m_master : nullptr;
    auto op = std::make_unique<mon_op::RebuildServer>(*this, target, source, rebuild_master);
    return schedule_manual_command(move(op), rebuild_server_cmd, error_out);
}

namespace mon_op
{
bool RebuildServer::rebuild_check_preconds()
{
    auto& error_out = m_result.output;
    MariaDBServer* target = m_mon.get_server(m_target_srv);
    if (!target)
    {
        PRINT_JSON_ERROR(error_out, "%s is not monitored by %s, cannot rebuild it.",
                         m_target_srv->name(), m_mon.name());
    }

    MariaDBServer* source = nullptr;
    if (m_source_srv)
    {
        source = m_mon.get_server(m_source_srv);
        if (!source)
        {
            PRINT_JSON_ERROR(error_out, "%s is not monitored by %s, cannot use it as rebuild source.",
                             m_source_srv->name(), m_mon.name());
        }
    }
    else if (target)
    {
        // User did not give a source server, so autoselect. Prefer a slave.
        source = autoselect_source_srv(target);
        if (!source)
        {
            PRINT_JSON_ERROR(error_out, "Could not autoselect rebuild source server. A valid source must "
                                        "be either a master or slave.");
        }
    }

    bool rval = false;
    if (target && source)
    {
        bool target_ok = true;
        bool source_ok = true;
        bool settings_ok = true;
        const char wrong_state_fmt[] = "Server '%s' is already a %s, cannot rebuild it.";

        // The following do not actually prevent rebuilding, they are just safeguards against user errors.
        if (target->is_master())
        {
            PRINT_JSON_ERROR(error_out, wrong_state_fmt, target->name(), "master");
            target_ok = false;
        }
        else if (target->is_relay_master())
        {
            PRINT_JSON_ERROR(error_out, wrong_state_fmt, target->name(), "relay");
            target_ok = false;
        }
        else if (target->is_slave())
        {
            PRINT_JSON_ERROR(error_out, wrong_state_fmt, target->name(), "slave");
            target_ok = false;
        }

        if (!source->is_slave() && !source->is_master())
        {
            PRINT_JSON_ERROR(error_out, "Server '%s' is neither a master or slave, cannot use it "
                                        "as source.", source->name());
            source_ok = false;
        }

        const char settings_err_fmt[] = "'%s' is not set. %s requires ssh access to servers.";
        if (m_mon.m_settings.ssh_user.empty())
        {
            PRINT_JSON_ERROR(error_out, settings_err_fmt, CONFIG_SSH_USER, rebuild_server_cmd);
            settings_ok = false;
        }
        if (m_mon.m_settings.ssh_keyfile.empty())
        {
            // TODO: perhaps allow no authentication
            PRINT_JSON_ERROR(error_out, settings_err_fmt, CONFIG_SSH_KEYFILE, rebuild_server_cmd);
            settings_ok = false;
        }

        if (target_ok && source_ok && settings_ok)
        {
            m_target = target;
            m_source = source;
            rval = true;
        }
    }
    return rval;
}

bool RebuildServer::run_cmd_on_target(const std::string& cmd, const std::string& desc)
{
    bool rval = false;
    auto res = ssh_util::run_cmd(*m_target_ses, cmd, m_ssh_timeout);
    if (res.type == RType::OK && res.rc == 0)
    {
        rval = true;
    }
    else
    {
        string errmsg = ssh_util::form_cmd_error_msg(res, cmd.c_str());
        PRINT_JSON_ERROR(m_result.output, "Could not %s on %s. %s",
                         desc.c_str(), m_target->name(), errmsg.c_str());
    }
    return rval;
}

bool RebuildServer::check_rebuild_tools(MariaDBServer* server, ssh::Session& ssh)
{
    const char socat_cmd[] = "socat -V";
    const char pigz_cmd[] = "pigz -V";
    const char mbu_cmd[] = "mariabackup -v";

    auto socat_res = ssh_util::run_cmd(ssh, socat_cmd, m_ssh_timeout);
    auto pigz_res = ssh_util::run_cmd(ssh, pigz_cmd, m_ssh_timeout);
    auto mbu_res = ssh_util::run_cmd(ssh, mbu_cmd, m_ssh_timeout);

    auto& error_out = m_result.output;
    bool all_ok = true;
    auto check = [server, &all_ok, &error_out](const ssh_util::CmdResult& res, const char* tool,
                                               const char* cmd) {
        if (res.type == RType::OK)
        {
            if (res.rc != 0)
            {
                string fail_reason = ssh_util::form_cmd_error_msg(res, cmd);
                PRINT_JSON_ERROR(error_out, "'%s' lacks '%s', which is required for server rebuild. %s",
                                 server->name(), tool, fail_reason.c_str());
                all_ok = false;
            }
        }
        else
        {
            string fail_reason = ssh_util::form_cmd_error_msg(res, cmd);
            PRINT_JSON_ERROR(error_out, "Could not check that '%s' has '%s', which is required for "
                                        "server rebuild. %s", server->name(), tool, fail_reason.c_str());
            all_ok = false;
        }
    };

    check(socat_res, "socat", socat_cmd);
    check(pigz_res, "pigz", pigz_cmd);
    check(mbu_res, "mariabackup", mbu_cmd);
    return all_ok;
}

bool RebuildServer::check_free_listen_port(ssh::Session& ses, MariaDBServer* server)
{
    bool success = true;
    auto& error_out = m_result.output;

    auto get_port_pids = [this, &success, &ses, server, &error_out]() {
        std::vector<int> pids;
        // lsof needs to be ran as sudo to see ports, even from the same user. This part could be made
        // optional if users are not willing to give MaxScale sudo-privs.
        auto port_pid_cmd = mxb::string_printf("sudo lsof -n -P -i TCP:%i -s TCP:LISTEN | tr -s ' ' "
                                               "| cut --fields=2 --delimiter=' ' | tail -n+2",
                                               m_rebuild_port);
        auto port_res = ssh_util::run_cmd(ses, port_pid_cmd, m_ssh_timeout);
        if (port_res.type == RType::OK && port_res.rc == 0)
        {
            if (!port_res.output.empty())
            {
                auto lines = mxb::strtok(port_res.output, "\n");
                for (auto& line : lines)
                {
                    int pid = 0;
                    if (mxb::get_int(line.c_str(), &pid))
                    {
                        pids.push_back(pid);
                    }
                    else
                    {
                        PRINT_JSON_ERROR(error_out, "Could not parse pid from text '%s'.", line.c_str());
                        success = false;
                    }
                }
            }
        }
        else
        {
            string fail_reason = ssh_util::form_cmd_error_msg(port_res, port_pid_cmd.c_str());
            PRINT_JSON_ERROR(error_out, "Could not read pid of process using port %i on %s. %s",
                             m_rebuild_port, server->name(), fail_reason.c_str());
            success = false;
        }
        return pids;
    };

    auto pids = get_port_pids();
    if (success)
    {
        for (auto pid : pids)
        {
            MXB_WARNING("Port %i on %s is used by process %i, trying to kill it.",
                        m_rebuild_port, server->name(), pid);
            auto kill_cmd = mxb::string_printf("kill -s SIGKILL %i", pid);
            auto kill_res = ssh_util::run_cmd(ses, kill_cmd, m_ssh_timeout);
            if (kill_res.type != RType::OK || kill_res.rc != 0)
            {
                string fail_reason = ssh_util::form_cmd_error_msg(kill_res, kill_cmd.c_str());
                PRINT_JSON_ERROR(error_out, "Failed to kill process %i on %s. %s",
                                 pid, server->name(), fail_reason.c_str());
                success = false;
            }
        }

        if (success && !pids.empty())
        {
            // Sleep a little in case the kill takes a while to take effect. Then check the port again.
            sleep(1);
            auto pids2 = get_port_pids();
            if (success && !pids2.empty())
            {
                PRINT_JSON_ERROR(error_out, "Port %i is still in use on %s. Cannot use it as rebuild source.",
                                 m_rebuild_port, server->name());
                success = false;
            }
        }
    }

    return success;
}

SimpleOp::SimpleOp(CmdMethod func)
    : m_func(move(func))
{
}

bool SimpleOp::run()
{
    mxb_assert(m_result.output.object_size() == 0);
    m_result = m_func();
    return true;
}

Result SimpleOp::result()
{
    return m_result;
}

void SimpleOp::cancel()
{
    // Can only end up here if the op is canceled right after it transitioned to running state but before
    // any of it was ran. In any case, nothing to do.
}

RebuildServer::RebuildServer(MariaDBMonitor& mon, SERVER* target, SERVER* source, MariaDBServer* master)
    : m_mon(mon)
    , m_target_srv(target)
    , m_source_srv(source)
    , m_repl_master(master)
{
    const auto& sett = m_mon.m_settings;
    m_rebuild_port = (int)sett.rebuild_port;
    m_ssh_timeout = sett.ssh_timeout;
}

bool RebuildServer::run()
{
    bool command_complete = false;
    bool advance = true;
    while (advance)
    {
        switch (m_state)
        {
        case State::INIT:
            advance = init();
            break;

        case State::TEST_DATALINK:
            test_datalink();
            break;

        case State::SERVE_BACKUP:
            advance = serve_backup();
            break;

        case State::PREPARE_TARGET:
            advance = prepare_target();
            break;

        case State::START_TRANSFER:
            advance = start_transfer();
            break;

        case State::WAIT_TRANSFER:
            advance = wait_transfer();
            break;

        case State::CHECK_DATADIR_SIZE:
            check_datadir_size();
            break;

        case State::START_BACKUP_PREPARE:
            advance = start_backup_prepare();
            break;

        case State::WAIT_BACKUP_PREPARE:
            advance = wait_backup_prepare();
            break;

        case State::START_TARGET:
            advance = start_target();
            break;

        case State::START_REPLICATION:
            advance = start_replication();
            break;

        case State::DONE:
            m_result.success = true;
            m_state = State::CLEANUP;
            break;

        case State::CLEANUP:
            cleanup();
            command_complete = true;
            advance = false;
            break;
        }
    }

    return command_complete;
}

void RebuildServer::cancel()
{
    switch (m_state)
    {
    case State::INIT:
        // No need to do anything.
        break;

    case State::WAIT_TRANSFER:
    case State::WAIT_BACKUP_PREPARE:
        cleanup();
        break;

    default:
        mxb_assert(!true);
    }
}

Result RebuildServer::result()
{
    return m_result;
}

bool RebuildServer::init()
{
    bool init_ok = false;

    if (rebuild_check_preconds())
    {
        // Ok so far. Initiate SSH-sessions to both servers.
        auto init_ssh = [this](MariaDBServer* server) {
            const auto& sett = m_mon.m_settings;
            auto [ses, errmsg_con] = ssh_util::init_ssh_session(
                server->server->address(), sett.ssh_port, sett.ssh_user, sett.ssh_keyfile,
                sett.ssh_host_check, m_ssh_timeout);

            if (!ses)
            {
                PRINT_JSON_ERROR(m_result.output, "SSH connection to %s failed. %s",
                                 server->name(), errmsg_con.c_str());
            }
            return ses;
        };

        auto target_ses = init_ssh(m_target);
        auto source_ses = init_ssh(m_source);

        if (target_ses && source_ses)
        {
            bool have_tools = true;

            // Check installed tools.
            auto target_tools = check_rebuild_tools(m_target, *target_ses);
            auto source_tools = check_rebuild_tools(m_source, *source_ses);

            if (target_tools && source_tools)
            {
                if (check_free_listen_port(*source_ses, m_source))
                {
                    m_target_ses = move(target_ses);
                    m_source_ses = move(source_ses);

                    m_source_slaves_old = m_source->m_slave_status;     // Backup slave conns
                    init_ok = true;
                }
            }
        }
    }

    m_state = init_ok ? State::TEST_DATALINK : State::CLEANUP;
    return true;
}

void RebuildServer::test_datalink()
{
    using Status = ssh_util::AsyncCmd::Status;
    auto& error_out = m_result.output;
    bool success = false;
    // Test the datalink between source and target by streaming some bytes.
    string test_serve_cmd = mxb::string_printf("socat -u EXEC:'echo %s' TCP-LISTEN:%i,reuseaddr",
                                               link_test_msg, m_rebuild_port);
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_source_ses, test_serve_cmd);
    if (cmd_handle)
    {
        // According to testing, the listen port sometimes takes a bit of time to actually open.
        // To account for this, try to connect a few times before giving up.
        string test_receive_cmd = mxb::string_printf("socat -u TCP:%s:%i,connect-timeout=%i STDOUT",
                                                     m_source->server->address(), m_rebuild_port,
                                                     socat_timeout_s);
        ssh_util::CmdResult res;
        const int max_tries = 5;
        for (int i = 0; i < max_tries; i++)
        {
            res = ssh_util::run_cmd(*m_target_ses, test_receive_cmd, m_ssh_timeout);
            if (res.type == RType::OK)
            {
                if (res.rc == 0)
                {
                    m_port_open_delay = i * 1s;
                    break;
                }
                else if (i + 1 < max_tries)
                {
                    sleep(1);
                }
            }
            else
            {
                // Unexpected error.
                break;
            }
        }

        if (res.type == RType::OK && res.rc == 0)
        {
            mxb::trim(res.output);
            bool data_ok = (res.output == link_test_msg);
            // Wait for the source to quit. Usually happens immediately.
            auto source_status = Status::BUSY;
            for (int i = 0; i < max_tries; i++)
            {
                source_status = cmd_handle->update_status();
                if (source_status == Status::BUSY && (i + 1 < max_tries))
                {
                    sleep(1);
                }
                else
                {
                    break;
                }
            }

            if (data_ok && source_status == Status::READY)
            {
                success = true;
            }
            else
            {
                if (!data_ok)
                {
                    PRINT_JSON_ERROR(error_out, "Received '%s' when '%s' was expected when testing data "
                                                "streaming from %s to %s.",
                                     res.output.c_str(), link_test_msg, m_source->name(), m_target->name());
                }
                if (source_status == Status::BUSY)
                {
                    PRINT_JSON_ERROR(error_out, "Data stream serve command '%s' did not stop on %s even "
                                                "after data was transferred",
                                     test_receive_cmd.c_str(), m_source->name());
                }
                else if (source_status == Status::SSH_FAIL)
                {
                    PRINT_JSON_ERROR(error_out, "Failed to check transfer status on %s. %s",
                                     m_source->name(), cmd_handle->error_output().c_str());
                }
            }
        }
        else
        {
            string errmsg = ssh_util::form_cmd_error_msg(res, test_receive_cmd.c_str());
            PRINT_JSON_ERROR(m_result.output, "Could not receive test data on %s. %s",
                             m_target->name(), errmsg.c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(error_out, "Failed to test data streaming from %s. %s",
                         m_source->name(), ssh_errmsg.c_str());
    }
    m_state = success ? State::SERVE_BACKUP : State::CLEANUP;
}

bool RebuildServer::serve_backup()
{
    auto& cs = m_source->conn_settings();
    // Start serving the backup stream. The source will wait for a new connection.
    const char stream_fmt[] = "sudo mariabackup --user=%s --password=%s --backup --safe-slave-backup "
                              "--target-dir=/tmp --stream=xbstream --parallel=%i "
                              "| pigz -c | socat - TCP-LISTEN:%i,reuseaddr";
    string stream_cmd = mxb::string_printf(stream_fmt, cs.username.c_str(), cs.password.c_str(), 1,
                                           m_rebuild_port);
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_source_ses, stream_cmd);

    auto& error_out = m_result.output;
    bool rval = false;
    if (cmd_handle)
    {
        m_mon.m_cluster_modified = true;    // Starting mariabackup can affect server states.
        m_source_slaves_stopped = true;

        // Wait a bit, then check that command started successfully and is still running.
        sleep(1);
        auto status = cmd_handle->update_status();

        if (status == ssh_util::AsyncCmd::Status::BUSY)
        {
            rval = true;
            m_source_cmd = move(cmd_handle);
            MXB_NOTICE("%s serving backup on port %i.", m_source->name(), m_rebuild_port);
            // Wait a bit more to ensure listen port is open.
            sleep(m_port_open_delay.count() + 1);
        }
        else if (status == ssh_util::AsyncCmd::Status::READY)
        {
            // The stream serve operation ended before it should. Print all output available.
            // The ssh-command includes username & pw so mask those in the message.
            string masked_cmd = mxb::string_printf(stream_fmt, mask, mask, 1, m_rebuild_port);

            int rc = cmd_handle->rc();

            string message = mxb::string_printf(
                "Failed to stream data from %s. Command '%s' stopped before data was streamed. Return "
                "value %i. Output: '%s' Error output: '%s'", m_source->name(), masked_cmd.c_str(), rc,
                cmd_handle->output().c_str(), cmd_handle->error_output().c_str());

            PRINT_JSON_ERROR(error_out, "%s", message.c_str());
        }
        else
        {
            ssh_errmsg = mxb::string_printf("Failed to read output. %s",
                                            cmd_handle->error_output().c_str());
        }
    }

    if (!ssh_errmsg.empty())
    {
        mxb_assert(!m_source_cmd);
        PRINT_JSON_ERROR(error_out, "Failed to start streaming data from %s. %s",
                         m_source->name(), ssh_errmsg.c_str());
    }

    m_state = rval ? State::PREPARE_TARGET : State::CLEANUP;
    return true;
}

bool RebuildServer::prepare_target()
{
    bool target_prepared = false;
    string clear_datadir = mxb::string_printf("sudo rm -rf %s/*", rebuild_datadir.c_str());
    // Check that the rm-command length is correct. A safeguard against later changes which could
    // cause MaxScale to delete all files (sudo rm -rf *). Datadir may need to be configurable or read
    // from server. rm must be run as sudo since the directory and files is owned by "mysql". Even group
    // access would not suffice as server does not give write access to group members.
    if (clear_datadir.length() == 28)
    {
        if (run_cmd_on_target("sudo systemctl stop mariadb", "stop MariaDB Server")
            && run_cmd_on_target(clear_datadir, "empty data directory"))
        {
            MXB_NOTICE("MariaDB Server %s stopped, data and log directories cleared.", m_target->name());
            target_prepared = true;
        }
    }
    else
    {
        mxb_assert(!true);
        PRINT_JSON_ERROR(m_result.output, "Invalid rm-command (this should not happen!)");
    }
    m_state = target_prepared ? State::START_TRANSFER : State::CLEANUP;
    return true;
}

bool RebuildServer::start_transfer()
{
    bool transfer_started = false;

    // Connect to source and start stream the backup.
    const char receive_fmt[] = "socat -u TCP:%s:%i,connect-timeout=%i STDOUT | pigz -dc "
                               "| sudo mbstream -x --directory=%s";
    string receive_cmd = mxb::string_printf(receive_fmt, m_source->server->address(), m_rebuild_port,
                                            socat_timeout_s, rebuild_datadir.c_str());

    bool rval = false;
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_target_ses, receive_cmd);
    if (cmd_handle)
    {
        transfer_started = true;
        m_target_cmd = move(cmd_handle);
        MXB_NOTICE("Backup transfer from %s to %s started.", m_source->name(), m_target->name());
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Failed to start receiving data to %s. %s",
                         m_target->name(), ssh_errmsg.c_str());
    }
    m_state = transfer_started ? State::WAIT_TRANSFER : State::CLEANUP;
    return true;
}

bool RebuildServer::wait_transfer()
{
    using Status = ssh_util::AsyncCmd::Status;
    auto& error_out = m_result.output;
    bool wait_again = false;
    bool target_success = false;

    auto target_status = m_target_cmd->update_status();
    if (target_status == Status::BUSY)
    {
        // Target is receiving more data, keep waiting.
        wait_again = true;
    }
    else
    {
        if (target_status == Status::READY)
        {
            if (m_target_cmd->rc() == 0)
            {
                // Transfer ended with rval 0. This does not guarantee success since the return value of a
                // multistage command is the return value of the last part. Print any error messages here
                // and in the next step, check that the target directory is not empty.
                target_success = true;
                MXB_NOTICE("Backup transferred to %s.", m_target->name());
                if (!m_target_cmd->output().empty())
                {
                    MXB_NOTICE("Output from %s: %s", m_target->name(), m_target_cmd->output().c_str());
                }
                if (!m_target_cmd->error_output().empty())
                {
                    MXB_WARNING("Error output from %s: %s", m_target->name(),
                                m_target_cmd->error_output().c_str());
                }
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to receive backup on %s. Error %i: '%s'.",
                                 m_target->name(), m_target_cmd->rc(),
                                 m_target_cmd->error_output().c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(error_out, "Failed to check backup transfer status on %s. %s",
                             m_target->name(), m_target_cmd->error_output().c_str());
        }
        m_target_cmd = nullptr;
    }

    // Update source status even if not used right now.
    auto source_status = m_source_cmd->update_status();
    if (!wait_again)
    {
        // Target has finished, check if source has as well.
        if (source_status == Status::BUSY)
        {
            // Weird, source is still sending. Ignore and continue. Check it again during cleanup.
        }
        else
        {
            // Both stopped.
            if (source_status == Status::READY)
            {
                report_source_stream_status();
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to check backup transfer status on %s. %s",
                                 m_source->name(), m_source_cmd->error_output().c_str());
            }
            m_source_cmd = nullptr;
        }
    }

    if (wait_again)
    {
        return false;
    }
    else
    {
        m_state = target_success ? State::CHECK_DATADIR_SIZE : State::CLEANUP;
        return true;
    }
}


void RebuildServer::check_datadir_size()
{
    // Check that target data directory has contents. This is just to get a better error message in case
    // transfer failed without any clear errors. Even if this part fails, go to next state. The size check
    // can fail simply because MaxScale cannot run "du" as sudo.
    string du_cmd = mxb::string_printf("sudo du --apparent-size -sb %s/", rebuild_datadir.c_str());
    auto res = ssh_util::run_cmd(*m_target_ses, du_cmd, m_ssh_timeout);
    if (res.type == RType::OK && res.rc == 0)
    {
        bool parse_ok = false;
        auto words = mxb::strtok(res.output, " \t");
        if (words.size() > 1)
        {
            long bytes = -1;
            if (mxb::get_long(words[0], &bytes))
            {
                parse_ok = true;
                if (bytes < 1024 * 1024)
                {
                    MXB_WARNING("Data directory '%s' on %s is empty. Transfer must have failed.",
                                rebuild_datadir.c_str(), m_target->name());
                }
            }
        }

        if (!parse_ok)
        {
            MXB_WARNING("Could not check data directory size on %s. Command '%s' returned '%s.",
                        m_target->name(), du_cmd.c_str(), res.output.c_str());
        }
    }
    else
    {
        string errmsg = ssh_util::form_cmd_error_msg(res, du_cmd.c_str());
        MXB_WARNING("Could not check data directory size on %s. %s", m_target->name(), errmsg.c_str());
    }
    m_state = State::START_BACKUP_PREPARE;
}

bool RebuildServer::start_backup_prepare()
{
    // This step can fail if the previous step failed to write files. Just looking at the return value
    // of "mbstream" is not reliable.
    auto& cs = m_target->conn_settings();
    const char prepare_fmt[] = "sudo mariabackup --use-memory=1G --prepare --target-dir=%s";
    string prepare_cmd = mxb::string_printf(prepare_fmt, rebuild_datadir.c_str());
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_target_ses, prepare_cmd);

    auto& error_out = m_result.output;
    bool need_wait = false;
    bool success = false;

    if (cmd_handle)
    {
        // Wait a bit, then check that command started/finished successfully.
        sleep(1);
        auto status = cmd_handle->update_status();

        if (status == ssh_util::AsyncCmd::Status::BUSY)
        {
            success = true;
            need_wait = true;
            m_target_cmd = move(cmd_handle);
            MXB_NOTICE("Processing backup on %s.", m_target->name());
        }
        else if (status == ssh_util::AsyncCmd::Status::SSH_FAIL)
        {
            ssh_errmsg = mxb::string_printf("Failed to read output. %s",
                                            cmd_handle->error_output().c_str());
        }
        else
        {
            // The prepare-command completed quickly, which is normal in case source was not receiving
            // more trx during transfer.
            if (cmd_handle->rc() == 0)
            {
                success = true;
                MXB_NOTICE("Backup processed on %s.", m_target->name());
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to process backup data on %s. Command '%s' failed "
                                            "with error %i: '%s'", m_target->name(), prepare_cmd.c_str(),
                                 cmd_handle->rc(), cmd_handle->error_output().c_str());
            }
        }
    }

    if (!ssh_errmsg.empty())
    {
        mxb_assert(!m_target_cmd);
        PRINT_JSON_ERROR(error_out, "Failed to process backup data on %s. %s",
                         m_target->name(), ssh_errmsg.c_str());
    }

    m_state = success ? (need_wait ? State::WAIT_BACKUP_PREPARE : State::START_TARGET) : State::CLEANUP;
    return true;
}

bool RebuildServer::wait_backup_prepare()
{
    using Status = ssh_util::AsyncCmd::Status;
    auto& error_out = m_result.output;
    bool wait_again = false;
    bool target_success = false;

    auto target_status = m_target_cmd->update_status();
    if (target_status == Status::BUSY)
    {
        // Target is still processing, keep waiting.
        wait_again = true;
    }
    else
    {
        if (target_status == Status::READY)
        {
            if (m_target_cmd->rc() == 0)
            {
                target_success = true;
                MXB_NOTICE("Backup processed on %s.", m_target->name());
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to process backup on %s. Error %i: '%s'.",
                                 m_target->name(), m_target_cmd->rc(),
                                 m_target_cmd->error_output().c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(error_out, "Failed to check backup processing status on %s. %s",
                             m_target->name(), m_target_cmd->error_output().c_str());
        }
        m_target_cmd = nullptr;
    }

    if (wait_again)
    {
        return false;
    }
    else
    {
        m_state = target_success ? State::START_TARGET : State::CLEANUP;
        return true;
    }
}

bool RebuildServer::start_target()
{
    bool server_started = false;
    // chown must be ran sudo since changing user to something else than self.
    string chown_cmd = mxb::string_printf("sudo chown -R mysql:mysql %s/*", rebuild_datadir.c_str());
    // Check that the chown-command length is correct. Mainly a safeguard against later changes which could
    // cause MaxScale to change owner of every file on the system.
    if (chown_cmd.length() == 42)
    {
        if (run_cmd_on_target(chown_cmd, "change ownership of datadir contents")
            && run_cmd_on_target("sudo systemctl start mariadb", "start MariaDB Server"))
        {
            server_started = true;
            m_mon.m_cluster_modified = true;
        }
    }
    else
    {
        mxb_assert(!true);
        PRINT_JSON_ERROR(m_result.output, "Invalid chown command (this should not happen!)");
    }

    m_state = server_started ? State::START_REPLICATION : State::CLEANUP;
    return true;
}

bool RebuildServer::start_replication()
{
    string gtid;
    // The gtid of the rebuilt server may not be correct, read it from xtrabackup_binlog_info.
    string read_gtid_cmd = mxb::string_printf("sudo cat %s/xtrabackup_binlog_info | tr -s ' ' | "
                                              "cut --fields=3 | tail -n 1", rebuild_datadir.c_str());
    auto res = ssh_util::run_cmd(*m_target_ses, read_gtid_cmd, m_ssh_timeout);
    if (res.type == RType::OK && res.rc == 0)
    {
        // Check that the result at least looks a bit like a gtid.
        int n_dash = std::count(res.output.begin(), res.output.end(), '-');
        if (n_dash >= 2)
        {
            gtid = res.output;
            mxb::trim(gtid);
        }
        else
        {
            PRINT_JSON_ERROR(m_result.output,
                             "Command '%s' returned invalid result '%s' on %s. Expected a gtid string.",
                             read_gtid_cmd.c_str(), res.output.c_str(), m_target->name());
        }
    }
    else
    {
        string errmsg = ssh_util::form_cmd_error_msg(res, read_gtid_cmd.c_str());
        PRINT_JSON_ERROR(m_result.output, "Could not read gtid on %s. %s", m_target->name(), errmsg.c_str());
    }

    bool replication_attempted = false;
    bool replication_confirmed = false;
    if (!gtid.empty())
    {
        // If monitor had a master when starting rebuild, replicate from it. Otherwise, replicate from the
        // source server.
        MariaDBServer* repl_master = m_repl_master ? m_repl_master : m_source;

        m_target->update_server(false, false);
        if (m_target->is_running())
        {
            string errmsg;
            string set_gtid = mxb::string_printf("set global gtid_slave_pos='%s';", gtid.c_str());
            if (m_target->execute_cmd(set_gtid, &errmsg))
            {
                // Typically, Current_Pos is used when configuring replication on a new slave. Rebuild is
                // an exception as we just configured slave_pos.
                GeneralOpData op(OpStart::MANUAL, m_result.output, m_ssh_timeout);
                SlaveStatus::Settings slave_sett("", repl_master->server,
                                                 SlaveStatus::Settings::GtidMode::SLAVE);

                if (m_target->create_start_slave(op, slave_sett))
                {
                    replication_attempted = true;
                }
            }
            else
            {
                PRINT_JSON_ERROR(m_result.output, "Could not set gtid_slave_pos on %s after rebuild. %s",
                                 m_target->name(), errmsg.c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(m_result.output, "Could not connect to %s after rebuild.", m_target->name());
        }

        if (replication_attempted)
        {
            // Wait a bit and then check that replication works. This only affects the log message.
            // Simplified from MariaDBMonitor::wait_cluster_stabilization().
            std::this_thread::sleep_for(500ms);
            string errmsg;
            if (m_target->do_show_slave_status(&errmsg))
            {
                auto slave_conn = m_target->slave_connection_status_host_port(repl_master);
                if (slave_conn)
                {
                    if (slave_conn->slave_io_running == SlaveStatus::SLAVE_IO_YES
                        && slave_conn->slave_sql_running == true)
                    {
                        replication_confirmed = true;
                        MXB_NOTICE("%s replicating from %s.", m_target->name(), repl_master->name());
                    }
                    else
                    {
                        MXB_WARNING("%s did not start replicating from %s. IO/SQL running: %s/%s.",
                                    m_target->name(), repl_master->name(),
                                    SlaveStatus::slave_io_to_string(slave_conn->slave_io_running).c_str(),
                                    slave_conn->slave_sql_running ? "Yes" : "No");
                    }
                }
                else
                {
                    MXB_WARNING("Could not check replication from %s to %s: slave connection not found.",
                                repl_master->name(), m_target->name());
                }
            }
            else
            {
                MXB_WARNING("Could not check replication from %s to %s: %s",
                            repl_master->name(), m_target->name(), errmsg.c_str());
            }
        }
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Could not start replication on %s.", m_target->name());
    }

    m_state = replication_confirmed ? State::DONE : State::CLEANUP;
    return true;
}

void RebuildServer::cleanup()
{
    using Status = ssh_util::AsyncCmd::Status;
    // Target cmd may exist if rebuild is canceled while waiting for transfer/prepare.
    if (m_target_cmd)
    {
        m_target_cmd->update_status();
        m_target_cmd = nullptr;
    }

    // Ensure that the source server is no longer serving the backup.
    if (m_source_cmd)
    {
        auto source_status = m_source_cmd->update_status();
        if (source_status == Status::BUSY)
        {
            MXB_WARNING("%s is serving backup data stream even after transfer has ended. "
                        "Closing ssh channel.", m_source->name());
        }
        else if (source_status == Status::READY)
        {
            report_source_stream_status();
        }
        else
        {
            PRINT_JSON_ERROR(m_result.output, "Failed to check backup transfer status on %s. %s",
                             m_source->name(), m_source_cmd->error_output().c_str());
        }
        m_source_cmd = nullptr;
    }

    if (m_source_ses)
    {
        check_free_listen_port(*m_source_ses, m_source);
    }

    if (m_source_slaves_stopped && !m_source_slaves_old.empty())
    {
        // Source server replication was stopped when starting Mariabackup. Mariabackup does not restart the
        // connections if it quits in error or is killed. Check that any slave connections are running as
        // before.
        m_source->update_server(false, false);
        if (m_source->is_running())
        {
            const auto& new_slaves = m_source->m_slave_status;
            if (new_slaves.size() == m_source_slaves_old.size())
            {
                for (size_t i = 0; i < new_slaves.size(); i++)
                {
                    const auto& old_slave = m_source_slaves_old[i];
                    const auto& new_slave = new_slaves[i];
                    bool was_running = (old_slave.slave_io_running != SlaveStatus::SLAVE_IO_NO)
                        && old_slave.slave_sql_running;
                    bool is_running = (new_slave.slave_io_running != SlaveStatus::SLAVE_IO_NO)
                        && new_slave.slave_sql_running;
                    if (was_running && !is_running)
                    {
                        auto& slave_name = old_slave.settings.name;
                        MXB_NOTICE("Slave connection '%s' is not running on %s, starting it.",
                                   slave_name.c_str(), m_source->name());
                        string start_slave = mxb::string_printf("START SLAVE '%s';", slave_name.c_str());
                        m_source->execute_cmd(start_slave);
                    }
                }
            }
        }
    }
}

MariaDBServer* RebuildServer::autoselect_source_srv(const MariaDBServer* target)
{
    MariaDBServer* rval = nullptr;
    for (auto* cand : m_mon.servers())
    {
        if (cand != target && (cand->is_master() || cand->is_slave()))
        {
            // Anything is better than nothing, a slave is better than master.
            if (!rval || (rval->is_master() && cand->is_slave()))
            {
                rval = cand;
            }
            else if (rval->is_slave() && cand->is_slave())
            {
                // From two slaves, select one with highest gtid.
                const auto& curr_gtid = rval->m_gtid_current_pos;
                const auto& cand_gtid = cand->m_gtid_current_pos;
                if (cand_gtid.events_ahead(curr_gtid, GtidList::MISSING_DOMAIN_IGNORE) > 0)
                {
                    rval = cand;
                }
            }
        }
    }
    return rval;
}

void RebuildServer::report_source_stream_status()
{
    int rc = m_source_cmd->rc();
    if (rc == 0)
    {
        // Send command completed successfully. Print any output from the command as INFO-level, since it
        // can be a lot and is usually uninteresting.
        if (!m_source_cmd->output().empty())
        {
            MXB_INFO("Backup send output from %s: %s", m_source->name(), m_source_cmd->output().c_str());
        }
        if (!m_source_cmd->error_output().empty())
        {
            MXB_INFO("Backup send error output from %s: %s", m_source->name(),
                     m_source_cmd->error_output().c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Backup send failure on %s. Error %i: '%s'.",
                         m_source->name(), m_source_cmd->rc(),
                         m_source_cmd->error_output().c_str());
    }
}

Result Result::deep_copy() const
{
    return {success, output.deep_copy()};
}
}
