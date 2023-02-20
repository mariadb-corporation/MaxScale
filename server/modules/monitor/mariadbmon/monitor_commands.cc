/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
const char create_backup_cmd[] = "create-backup";
const char restore_from_backup_cmd[] = "restore-from-backup";
const char mask[] = "******";

const string rebuild_datadir = "/var/lib/mysql";    // configurable?
const char link_test_msg[] = "Test message";
const int socat_timeout_s = 5;      // TODO: configurable?
const char list_dir_err_fmt[] = "Could not list contents of '%s' on %s. '%s' must exist and "
                                "be accessible. %s";

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

bool handle_async_create_backup(const MODULECMD_ARG* args, json_t** output)
{
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    SERVER* source = args->argv[1].value.server;
    string bu_name = args->argv[2].value.string;
    return mon->schedule_create_backup(source, bu_name, output);
}

bool handle_async_restore_from_backup(const MODULECMD_ARG* args, json_t** output)
{
    auto* mon = static_cast<MariaDBMonitor*>(args->argv[0].value.monitor);
    SERVER* target = args->argv[1].value.server;
    string bu_name = args->argv[2].value.string;
    return mon->schedule_restore_from_backup(target, bu_name, output);
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
 * @param mode Execution mode
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
 * @param mode Execution mode
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
 * @param mode Execution mode
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
 * @param mode Execution mode
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
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "New primary (optional)"    },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Current primary (optional)"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, switchover_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Perform primary switchover");

    modulecmd_register_command(MXB_MODULE_NAME, "async-switchover", MODULECMD_TYPE_ACTIVE,
                               handle_async_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Schedule primary switchover. Does not wait for completion");

    static modulecmd_arg_type_t failover_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXB_MODULE_NAME, failover_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_failover, MXS_ARRAY_NELEMS(failover_argv), failover_argv,
                               "Perform primary failover");

    modulecmd_register_command(MXB_MODULE_NAME, "async-failover", MODULECMD_TYPE_ACTIVE,
                               handle_async_failover, MXS_ARRAY_NELEMS(failover_argv), failover_argv,
                               "Schedule primary failover. Does not wait for completion.");

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
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Primary server (optional)"}
    };

    modulecmd_register_command(MXB_MODULE_NAME, reset_repl_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_reset_replication,
                               MXS_ARRAY_NELEMS(reset_gtid_argv), reset_gtid_argv,
                               "Delete replica connections, delete binary logs and "
                               "set up replication (dangerous)");

    modulecmd_register_command(MXB_MODULE_NAME, "async-reset-replication", MODULECMD_TYPE_ACTIVE,
                               handle_async_reset_replication,
                               MXS_ARRAY_NELEMS(reset_gtid_argv), reset_gtid_argv,
                               "Delete replica connections, delete binary logs and "
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
                               "Rebuild a server with Mariabackup. Does not wait for completion.");

    const modulecmd_arg_type_t create_backup_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER, "Source server" },
        { MODULECMD_ARG_STRING, "Backup name"}
    };
    modulecmd_register_command(MXB_MODULE_NAME, "async-create-backup", MODULECMD_TYPE_ACTIVE,
                               handle_async_create_backup,
                               MXS_ARRAY_NELEMS(create_backup_argv), create_backup_argv,
                               "Create a backup with Mariabackup. Does not wait for completion.");

    const modulecmd_arg_type_t restore_backup_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER, "Target server" },
        { MODULECMD_ARG_STRING, "Backup name"}
    };
    modulecmd_register_command(MXB_MODULE_NAME, "async-restore-from-backup", MODULECMD_TYPE_ACTIVE,
                               handle_async_restore_from_backup,
                               MXS_ARRAY_NELEMS(restore_backup_argv), restore_backup_argv,
                               "Restore a server from a backup. Does not wait for completion.");
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
    mxb_assert(mxb::Worker::get_current()->id() == m_worker->id());
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
    auto& srvs = m_servers;
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
    auto op = std::make_unique<mon_op::RebuildServer>(*this, target, source);
    return schedule_manual_command(move(op), rebuild_server_cmd, error_out);
}

bool MariaDBMonitor::schedule_create_backup(SERVER* source, const std::string& bu_name, json_t** error_out)
{
    auto op = std::make_unique<mon_op::CreateBackup>(*this, source, bu_name);
    return schedule_manual_command(std::move(op), create_backup_cmd, error_out);
}

bool MariaDBMonitor::schedule_restore_from_backup(SERVER* target, const std::string& bu_name,
                                                  json_t** error_out)
{
    auto op = std::make_unique<mon_op::RestoreFromBackup>(*this, target, bu_name);
    return schedule_manual_command(std::move(op), restore_from_backup_cmd, error_out);
}

namespace mon_op
{
bool RebuildServer::check_preconditions()
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
                                        "be either a primary or replica.");
        }
    }

    bool rval = false;
    if (target && source)
    {
        bool target_ok = check_server_is_valid_target(target);
        bool source_ok = true;
        bool settings_ok = true;

        if (!check_server_is_valid_source(source))
        {
            source_ok = false;
        }

        if (!check_ssh_settings())
        {
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

bool BackupOperation::run_cmd_on_target(const std::string& cmd, const std::string& desc)
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
                         desc.c_str(), m_target_name.c_str(), errmsg.c_str());
    }
    return rval;
}

bool BackupOperation::check_rebuild_tools(const char* srvname, ssh::Session& ssh)
{
    const char socat_cmd[] = "socat -V";
    const char pigz_cmd[] = "pigz -V";
    const char mbu_cmd[] = "mariabackup -v";

    auto socat_res = ssh_util::run_cmd(ssh, socat_cmd, m_ssh_timeout);
    auto pigz_res = ssh_util::run_cmd(ssh, pigz_cmd, m_ssh_timeout);
    auto mbu_res = ssh_util::run_cmd(ssh, mbu_cmd, m_ssh_timeout);

    auto& error_out = m_result.output;
    bool all_ok = true;
    auto check = [srvname, &all_ok, &error_out](const ssh_util::CmdResult& res, const char* tool,
                                                const char* cmd) {
        if (res.type == RType::OK)
        {
            if (res.rc != 0)
            {
                string fail_reason = ssh_util::form_cmd_error_msg(res, cmd);
                PRINT_JSON_ERROR(error_out, "%s lacks '%s', which is required for backup operations. %s",
                                 srvname, tool, fail_reason.c_str());
                all_ok = false;
            }
        }
        else
        {
            string fail_reason = ssh_util::form_cmd_error_msg(res, cmd);
            PRINT_JSON_ERROR(error_out, "Could not check that %s has '%s', which is required for backup "
                                        "operations. %s", srvname, tool, fail_reason.c_str());
            all_ok = false;
        }
    };

    check(socat_res, "socat", socat_cmd);
    check(pigz_res, "pigz", pigz_cmd);
    check(mbu_res, "mariabackup", mbu_cmd);
    return all_ok;
}

bool BackupOperation::check_free_listen_port(const char* srvname, ssh::Session& ses, int port)
{
    bool success = true;
    auto& error_out = m_result.output;

    auto get_port_pids = [&]() {
        std::vector<int> pids;
        // lsof needs to be run as sudo to see ports, even from the same user. This part could be made
        // optional if users are not willing to give MaxScale sudo-privs.
        auto port_pid_cmd = mxb::string_printf("sudo lsof -n -P -i TCP:%i -s TCP:LISTEN | tr -s ' ' "
                                               "| cut --fields=2 --delimiter=' ' | tail -n+2", port);
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
                             port, srvname, fail_reason.c_str());
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
                        port, srvname, pid);
            auto kill_cmd = mxb::string_printf("kill -s SIGKILL %i", pid);
            auto kill_res = ssh_util::run_cmd(ses, kill_cmd, m_ssh_timeout);
            if (kill_res.type != RType::OK || kill_res.rc != 0)
            {
                string fail_reason = ssh_util::form_cmd_error_msg(kill_res, kill_cmd.c_str());
                PRINT_JSON_ERROR(error_out, "Failed to kill process %i on %s. %s",
                                 pid, srvname, fail_reason.c_str());
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
                PRINT_JSON_ERROR(error_out, "Port %i is still in use on %s. Cannot use it as a backup "
                                            "source.", port, srvname);
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

BackupOperation::BackupOperation(MariaDBMonitor& mon)
    : m_mon(mon)
{
    m_ssh_timeout = m_mon.m_settings.ssh_timeout;
    m_source_port = m_mon.m_settings.rebuild_port;
}

void BackupOperation::set_source(string name, string host)
{
    m_source_name = std::move(name);
    m_source_host = std::move(host);
}

void BackupOperation::set_target(string name, string host)
{
    m_target_name = std::move(name);
    m_target_host = std::move(host);
}

ssh_util::SSession BackupOperation::init_ssh_session(const char* name, const string& host)
{
    const auto& sett = m_mon.m_settings;
    auto [ses, errmsg_con] = ssh_util::init_ssh_session(host, sett.ssh_port, sett.ssh_user, sett.ssh_keyfile,
                                                        sett.ssh_host_check, m_ssh_timeout);

    if (!ses)
    {
        PRINT_JSON_ERROR(m_result.output, "SSH connection to %s failed. %s", name, errmsg_con.c_str());
    }
    return ses;
}

RebuildServer::RebuildServer(MariaDBMonitor& mon, SERVER* target, SERVER* source)
    : BackupOperation(mon)
    , m_target_srv(target)
    , m_source_srv(source)
{
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
            advance = state_init();
            break;

        case State::TEST_DATALINK:
            advance = state_test_datalink();
            break;

        case State::SERVE_BACKUP:
            advance = state_serve_backup();
            break;

        case State::PREPARE_TARGET:
            advance = state_prepare_target();
            break;

        case State::START_TRANSFER:
            advance = state_start_transfer();
            break;

        case State::WAIT_TRANSFER:
            advance = state_wait_transfer();
            break;

        case State::CHECK_DATADIR_SIZE:
            state_check_datadir();
            break;

        case State::START_BACKUP_PREPARE:
            advance = state_start_backup_prepare();
            break;

        case State::WAIT_BACKUP_PREPARE:
            advance = state_wait_backup_prepare();
            break;

        case State::START_TARGET:
            advance = state_start_target();
            break;

        case State::START_REPLICATION:
            advance = state_start_replication();
            break;

        case State::DONE:
            m_result.success = true;
            m_state = State::CLEANUP;
            break;

        case State::CLEANUP:
            state_cleanup();
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
        state_cleanup();
        break;

    default:
        mxb_assert(!true);
    }
}

Result BackupOperation::result()
{
    return m_result;
}

bool BackupOperation::init_operation()
{
    const char* source_name = m_source_name.c_str();
    const char* target_name = m_target_name.c_str();

    bool init_ok = false;

    // Ok so far. Initiate SSH-sessions to both servers.
    auto target_ses = init_ssh_session(m_target_name.c_str(), m_target_host);
    auto source_ses = init_ssh_session(m_source_name.c_str(), m_source_host);

    if (target_ses && source_ses)
    {
        bool have_tools = true;

        // Check installed tools.
        bool target_tools = check_rebuild_tools(target_name, *target_ses);
        bool source_tools = check_rebuild_tools(source_name, *source_ses);

        if (target_tools && source_tools)
        {
            if (check_free_listen_port(source_name, *source_ses, m_source_port))
            {
                m_target_ses = std::move(target_ses);
                m_source_ses = std::move(source_ses);
                init_ok = true;
            }
        }
    }
    return init_ok;
}

bool RebuildServer::state_init()
{
    m_state = State::CLEANUP;
    if (check_preconditions())
    {
        set_source(m_source->name(), m_source->server->address());
        set_target(m_target->name(), m_target->server->address());

        if (init_operation())
        {
            m_source_slaves_old = m_source->m_slave_status;     // Backup slave conns
            m_state = State::TEST_DATALINK;
        }
    }
    return true;
}

bool BackupOperation::test_datalink()
{
    using Status = ssh_util::AsyncCmd::Status;
    auto source_name = m_source_name.c_str();
    auto target_name = m_target_name.c_str();
    auto& error_out = m_result.output;

    bool success = false;
    // Test the datalink between source and target by streaming a message.
    string test_serve_cmd = mxb::string_printf("socat -u EXEC:'echo %s' TCP-LISTEN:%i,reuseaddr",
                                               link_test_msg, m_source_port);
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_source_ses, test_serve_cmd);
    if (cmd_handle)
    {
        // According to testing, the listen port sometimes takes a bit of time to actually open.
        // To account for this, try to connect a few times before giving up.
        string test_receive_cmd = mxb::string_printf("socat -u TCP:%s:%i,connect-timeout=%i STDOUT",
                                                     m_source_host.c_str(), m_source_port, socat_timeout_s);
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
                                     res.output.c_str(), link_test_msg, source_name, target_name);
                }
                if (source_status == Status::BUSY)
                {
                    PRINT_JSON_ERROR(error_out, "Data stream serve command '%s' did not stop on %s even "
                                                "after data was transferred",
                                     test_receive_cmd.c_str(), source_name);
                }
                else if (source_status == Status::SSH_FAIL)
                {
                    PRINT_JSON_ERROR(error_out, "Failed to check transfer status on %s. %s",
                                     source_name, cmd_handle->error_output().c_str());
                }
            }
        }
        else
        {
            string errmsg = ssh_util::form_cmd_error_msg(res, test_receive_cmd.c_str());
            PRINT_JSON_ERROR(m_result.output, "Could not receive test data on %s. %s",
                             target_name, errmsg.c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(error_out, "Failed to test data streaming from %s. %s",
                         source_name, ssh_errmsg.c_str());
    }
    return success;
}

bool BackupOperation::serve_backup(const string& mariadb_user, const string& mariadb_pw)
{
    auto source_name = m_source_name.c_str();
    // Start serving the backup stream. The source will wait for a new connection.
    const char stream_fmt[] = "sudo mariabackup --user=%s --password=%s --backup --safe-slave-backup "
                              "--target-dir=/tmp --stream=xbstream --parallel=%i "
                              "| pigz -c | socat - TCP-LISTEN:%i,reuseaddr";
    string stream_cmd = mxb::string_printf(stream_fmt, mariadb_user.c_str(), mariadb_pw.c_str(), 1,
                                           m_source_port);
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
            m_source_cmd = std::move(cmd_handle);
            MXB_NOTICE("%s serving backup on port %i.", source_name, m_source_port);
            // Wait a bit more to ensure listen port is open.
            sleep(m_port_open_delay.count() + 1);
        }
        else if (status == ssh_util::AsyncCmd::Status::READY)
        {
            // The stream serve operation ended before it should. Print all output available.
            // The ssh-command includes username & pw so mask those in the message.
            string masked_cmd = mxb::string_printf(stream_fmt, mask, mask, 1, m_source_port);

            int rc = cmd_handle->rc();

            string message = mxb::string_printf(
                "Failed to stream data from %s. Command '%s' stopped before data was streamed. Return "
                "value %i. Output: '%s' Error output: '%s'", source_name, masked_cmd.c_str(), rc,
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
                         source_name, ssh_errmsg.c_str());
    }
    return rval;
}

bool BackupOperation::check_ssh_settings()
{
    bool settings_ok = true;
    const char settings_err_fmt[] = "'%s' is not set. Backup operations require ssh access to servers.";
    if (m_mon.m_settings.ssh_user.empty())
    {
        PRINT_JSON_ERROR(m_result.output, settings_err_fmt, CONFIG_SSH_USER);
        settings_ok = false;
    }
    if (m_mon.m_settings.ssh_keyfile.empty())
    {
        // TODO: perhaps allow no authentication
        PRINT_JSON_ERROR(m_result.output, settings_err_fmt, CONFIG_SSH_KEYFILE);
        settings_ok = false;
    }
    return settings_ok;
}

bool BackupOperation::check_backup_storage_settings()
{
    bool settings_ok = true;
    const char settings_err[] = "'%s' is not set. Backup operations require a valid value for this "
                                "setting.";
    if (m_mon.m_settings.backup_storage_addr.empty())
    {
        PRINT_JSON_ERROR(m_result.output, settings_err, CONFIG_BACKUP_ADDR);
        settings_ok = false;
    }
    if (m_mon.m_settings.backup_storage_path.empty())
    {
        PRINT_JSON_ERROR(m_result.output, settings_err, CONFIG_BACKUP_PATH);
        settings_ok = false;
    }
    return settings_ok;
}

bool BackupOperation::check_server_is_valid_target(const MariaDBServer* target)
{
    auto& error_out = m_result.output;
    bool target_ok = true;
    const char wrong_state_fmt[] = "Server '%s' is already a %s, cannot rebuild it.";

    // The following do not actually prevent rebuilding, they are just safeguards against user errors.
    if (target->is_master())
    {
        PRINT_JSON_ERROR(error_out, wrong_state_fmt, target->name(), "primary");
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
    return target_ok;
}

bool BackupOperation::check_server_is_valid_source(const MariaDBServer* source)
{
    bool source_ok = true;
    if (!source->is_slave() && !source->is_master())
    {
        PRINT_JSON_ERROR(m_result.output, "Server '%s' is neither a primary or replica, cannot use it "
                                          "as source.", source->name());
        source_ok = false;
    }
    return source_ok;
}

bool BackupOperation::prepare_target()
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
            MXB_NOTICE("MariaDB Server %s stopped, data and log directories cleared.",
                       m_target_name.c_str());
            target_prepared = true;
        }
    }
    else
    {
        mxb_assert(!true);
        PRINT_JSON_ERROR(m_result.output, "Invalid rm-command (this should not happen!)");
    }
    return target_prepared;
}

bool RebuildServer::state_prepare_target()
{
    m_state = prepare_target() ? State::START_TRANSFER : State::CLEANUP;
    return true;
}

bool BackupOperation::start_transfer(const string& destination, TargetOwner target_owner)
{
    bool transfer_started = false;

    // Connect to source and start stream the backup.
    const char receive_fmt[] = "socat -u TCP:%s:%i,connect-timeout=%i STDOUT | pigz -dc "
                               "| %smbstream -x --directory=%s";
    const char* maybe_sudo = (target_owner == TargetOwner::ROOT) ? "sudo " : "";
    string receive_cmd = mxb::string_printf(receive_fmt, m_source_host.c_str(), m_source_port,
                                            socat_timeout_s, maybe_sudo, destination.c_str());

    bool rval = false;
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_target_ses, receive_cmd);
    if (cmd_handle)
    {
        transfer_started = true;
        m_target_cmd = std::move(cmd_handle);
        MXB_NOTICE("Backup transfer from %s to %s started.", m_source_name.c_str(), m_target_name.c_str());
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Failed to start receiving data to %s. %s",
                         m_target_name.c_str(), ssh_errmsg.c_str());
    }
    return transfer_started;
}

bool RebuildServer::state_start_transfer()
{
    m_state = start_transfer(rebuild_datadir, TargetOwner::ROOT) ? State::WAIT_TRANSFER : State::CLEANUP;
    return true;
}

BackupOperation::StateResult BackupOperation::wait_transfer()
{
    using Status = ssh_util::AsyncCmd::Status;
    auto& error_out = m_result.output;
    const char* target_name = m_target_name.c_str();

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
                MXB_NOTICE("Backup transferred to %s.", target_name);
                if (!m_target_cmd->output().empty())
                {
                    MXB_NOTICE("Output from %s: %s", target_name, m_target_cmd->output().c_str());
                }
                if (!m_target_cmd->error_output().empty())
                {
                    MXB_WARNING("Error output from %s: %s", target_name,
                                m_target_cmd->error_output().c_str());
                }
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to receive backup on %s. Error %i: '%s'.",
                                 target_name, m_target_cmd->rc(),
                                 m_target_cmd->error_output().c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(error_out, "Failed to check backup transfer status on %s. %s",
                             target_name, m_target_cmd->error_output().c_str());
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
                                 m_source_name.c_str(), m_source_cmd->error_output().c_str());
            }
            m_source_cmd = nullptr;
        }
    }

    return wait_again ? StateResult::AGAIN : (target_success ? StateResult::OK : StateResult::ERROR);
}

bool RebuildServer::state_wait_transfer()
{
    auto res = wait_transfer();
    bool rval = false;
    switch (res)
    {
    case StateResult::OK:
        rval = true;
        m_state = State::CHECK_DATADIR_SIZE;
        break;

    case StateResult::AGAIN:
        break;

    case StateResult::ERROR:
        rval = true;
        m_state = State::CLEANUP;
        break;
    }
    return rval;
}

bool BackupOperation::check_datadir(std::shared_ptr<ssh::Session> ses, const string& datadir_path)
{
    auto& output = m_result.output;
    const string search_file = "backup-my.cnf";
    bool file_found = false;
    auto bu_check_func = [&file_found, &search_file](const ssh_util::FileInfo& info) {
        if (info.name == search_file)
        {
            file_found = true;
            return false;
        }
        return true;
    };

    bool success = false;
    if (check_directory_entries(ses, m_target_name, datadir_path, bu_check_func))
    {
        if (file_found)
        {
            success = true;
        }
        else
        {
            PRINT_JSON_ERROR(output, "Directory '%s' on %s does not contain file '%s'. Transfer must "
                                     "have failed.", datadir_path.c_str(), m_target_name.c_str(),
                             search_file.c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(output, "Could not check contents of '%s' on %s.",
                         datadir_path.c_str(), m_target_name.c_str());
    }

    return success;
}

bool RebuildServer::state_start_backup_prepare()
{
    m_state = start_backup_prepare() ? State::WAIT_BACKUP_PREPARE : State::CLEANUP;
    return true;
}

bool BackupOperation::start_backup_prepare()
{
    bool prepare_started = false;
    // This step can fail if the previous step failed to write files. Just looking at the return value
    // of "mbstream" is not reliable.
    const char prepare_fmt[] = "sudo mariabackup --use-memory=1G --prepare --target-dir=%s";
    string prepare_cmd = mxb::string_printf(prepare_fmt, rebuild_datadir.c_str());
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_target_ses, prepare_cmd);

    if (cmd_handle)
    {
        prepare_started = true;
        m_target_cmd = std::move(cmd_handle);
        MXB_NOTICE("Processing backup on %s.", m_target_name.c_str());
        // Wait a bit. This increases the likelihood that the command completes during this monitor tick.
        sleep(1);
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Failed to start processing backup data on %s. %s",
                         m_target_name.c_str(), ssh_errmsg.c_str());
    }

    return prepare_started;
}

bool RestoreFromBackup::state_wait_backup_prepare()
{
    auto res = wait_backup_prepare();
    bool rval = false;
    switch (res)
    {
    case StateResult::OK:
        rval = true;
        m_state = State::START_TARGET;
        break;

    case StateResult::AGAIN:
        break;

    case StateResult::ERROR:
        rval = true;
        m_state = State::CLEANUP;
        break;
    }
    return rval;
}

BackupOperation::StateResult BackupOperation::wait_backup_prepare()
{
    const char* target_name = m_target_name.c_str();
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
                MXB_NOTICE("Backup processed on %s.", target_name);
            }
            else
            {
                PRINT_JSON_ERROR(error_out, "Failed to process backup on %s. Error %i: '%s'.",
                                 target_name, m_target_cmd->rc(),
                                 m_target_cmd->error_output().c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(error_out, "Failed to check backup processing status on %s. %s",
                             target_name, m_target_cmd->error_output().c_str());
        }
        m_target_cmd = nullptr;
    }

    return wait_again ? StateResult::AGAIN : (target_success ? StateResult::OK : StateResult::ERROR);
}

bool BackupOperation::start_target()
{
    bool server_started = false;
    // chown must be run as sudo since changing user to something else than self.
    string chown_cmd = mxb::string_printf("sudo chown -R mysql:mysql %s", rebuild_datadir.c_str());
    // Check that the chown-command length is correct. Mainly a safeguard against later changes which could
    // cause MaxScale to change owner of every file on the system.
    if (chown_cmd.length() == 40)
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
    return server_started;
}

bool BackupOperation::start_replication(MariaDBServer* target, MariaDBServer* repl_master)
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
                             read_gtid_cmd.c_str(), res.output.c_str(), target->name());
        }
    }
    else
    {
        string errmsg = ssh_util::form_cmd_error_msg(res, read_gtid_cmd.c_str());
        PRINT_JSON_ERROR(m_result.output, "Could not read gtid on %s. %s", target->name(), errmsg.c_str());
    }

    bool replication_attempted = false;
    bool replication_confirmed = false;
    if (!gtid.empty())
    {
        target->update_server(false, false);
        if (target->is_running())
        {
            string errmsg;
            string set_gtid = mxb::string_printf("set global gtid_slave_pos='%s';", gtid.c_str());
            if (target->execute_cmd(set_gtid, &errmsg))
            {
                // Just configured gtid_slave_pos, so use it to start replication.
                GeneralOpData op(OpStart::MANUAL, m_result.output, m_ssh_timeout);
                SlaveStatus::Settings slave_sett("", repl_master->server,
                                                 SlaveStatus::Settings::GtidMode::SLAVE);

                if (target->create_start_slave(op, slave_sett))
                {
                    replication_attempted = true;
                }
            }
            else
            {
                PRINT_JSON_ERROR(m_result.output, "Could not set gtid_slave_pos on %s after rebuild. %s",
                                 target->name(), errmsg.c_str());
            }
        }
        else
        {
            PRINT_JSON_ERROR(m_result.output, "Could not connect to %s after rebuild.", target->name());
        }

        if (replication_attempted)
        {
            // Wait a bit and then check that replication works. This only affects the log message.
            // Simplified from MariaDBMonitor::wait_cluster_stabilization().
            std::this_thread::sleep_for(500ms);
            string errmsg;
            if (target->do_show_slave_status(&errmsg))
            {
                auto slave_conn = target->slave_connection_status_host_port(repl_master);
                if (slave_conn)
                {
                    if (slave_conn->slave_io_running == SlaveStatus::SLAVE_IO_YES
                        && slave_conn->slave_sql_running == true)
                    {
                        replication_confirmed = true;
                        MXB_NOTICE("%s replicating from %s.", target->name(), repl_master->name());
                    }
                    else
                    {
                        MXB_WARNING("%s did not start replicating from %s. IO/SQL running: %s/%s.",
                                    target->name(), repl_master->name(),
                                    SlaveStatus::slave_io_to_string(slave_conn->slave_io_running).c_str(),
                                    slave_conn->slave_sql_running ? "Yes" : "No");
                    }
                }
                else
                {
                    MXB_WARNING("Could not check replication from %s to %s: replica connection not found.",
                                repl_master->name(), target->name());
                }
            }
            else
            {
                MXB_WARNING("Could not check replication from %s to %s: %s",
                            repl_master->name(), target->name(), errmsg.c_str());
            }
        }
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Could not start replication on %s.", target->name());
    }
    return replication_confirmed;
}

void BackupOperation::cleanup(MariaDBServer* source, const SlaveStatusArray& source_slaves_old)
{
    mxb_assert(source_slaves_old.empty() || source);
    const char* source_name = m_source_name.c_str();
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
                        "Closing ssh channel.", source_name);
        }
        else if (source_status == Status::READY)
        {
            report_source_stream_status();
        }
        else
        {
            PRINT_JSON_ERROR(m_result.output, "Failed to check backup transfer status on %s. %s",
                             source_name, m_source_cmd->error_output().c_str());
        }
        m_source_cmd = nullptr;
    }

    if (m_source_ses)
    {
        check_free_listen_port(source_name, *m_source_ses, m_source_port);
    }

    if (m_source_slaves_stopped && !source_slaves_old.empty())
    {
        // Source server replication was stopped when starting Mariabackup. Mariabackup does not restart the
        // connections if it quits in error or is killed. Check that any slave connections are running as
        // before.
        source->update_server(false, false);
        if (source->is_running())
        {
            const auto& new_slaves = source->m_slave_status;
            if (new_slaves.size() == source_slaves_old.size())
            {
                for (size_t i = 0; i < new_slaves.size(); i++)
                {
                    const auto& old_slave = source_slaves_old[i];
                    const auto& new_slave = new_slaves[i];
                    bool was_running = (old_slave.slave_io_running != SlaveStatus::SLAVE_IO_NO)
                        && old_slave.slave_sql_running;
                    bool is_running = (new_slave.slave_io_running != SlaveStatus::SLAVE_IO_NO)
                        && new_slave.slave_sql_running;
                    if (was_running && !is_running)
                    {
                        auto& slave_name = old_slave.settings.name;
                        MXB_NOTICE("Replica connection '%s' is not running on %s, starting it.",
                                   slave_name.c_str(), source_name);
                        string start_slave = mxb::string_printf("START SLAVE '%s';", slave_name.c_str());
                        source->execute_cmd(start_slave);
                    }
                }
            }
        }
    }
}

MariaDBServer* BackupOperation::autoselect_source_srv(const MariaDBServer* target)
{
    MariaDBServer* rval = nullptr;
    for (auto* cand : m_mon.m_servers)
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

void BackupOperation::report_source_stream_status()
{
    const char* source_name = m_source_name.c_str();
    int rc = m_source_cmd->rc();
    if (rc == 0)
    {
        // Send command completed successfully. Print any output from the command as INFO-level, since it
        // can be a lot and is usually uninteresting.
        if (!m_source_cmd->output().empty())
        {
            MXB_INFO("Backup send output from %s: %s", source_name, m_source_cmd->output().c_str());
        }
        if (!m_source_cmd->error_output().empty())
        {
            MXB_INFO("Backup send error output from %s: %s", source_name,
                     m_source_cmd->error_output().c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Backup send failure on %s. Error %i: '%s'.",
                         source_name, m_source_cmd->rc(),
                         m_source_cmd->error_output().c_str());
    }
}

bool BackupOperation::check_directory_entries(std::shared_ptr<ssh::Session> ses, const std::string& srv_name,
                                              const string& path, const DirCheckFunc& check_func)
{
    bool fetch_success = false;
    auto& output = m_result.output;

    auto [sftp, errmsg] = ssh_util::start_sftp_ses(std::move(ses));
    if (sftp)
    {
        auto [dir_contents, dir_errmsg] = sftp->list_directory(path);
        if (dir_errmsg.empty())
        {
            fetch_success = true;
            for (const auto& elem : dir_contents)
            {
                if (!check_func(elem))
                {
                    break;
                }
            }
        }
        else
        {
            PRINT_JSON_ERROR(output, "Error fetching contents of '%s': %s", path.c_str(), dir_errmsg.c_str());
        }
    }
    else
    {
        PRINT_JSON_ERROR(output, "Error starting sftp session on %s: %s", srv_name.c_str(), errmsg.c_str());
    }

    return fetch_success;
}

int BackupOperation::source_port() const
{
    return m_source_port;
}

bool RebuildServer::state_test_datalink()
{
    m_state = test_datalink() ? State::SERVE_BACKUP : State::CLEANUP;
    return true;
}

bool RebuildServer::state_serve_backup()
{
    auto& cs = m_source->conn_settings();
    m_state = serve_backup(cs.username, cs.password) ? State::PREPARE_TARGET : State::CLEANUP;
    return true;
}

void RebuildServer::state_check_datadir()
{
    m_state = check_datadir(m_target_ses, rebuild_datadir) ? State::START_BACKUP_PREPARE :
        State::CLEANUP;
}

void RebuildServer::state_cleanup()
{
    cleanup(m_source, m_source_slaves_old);
}

bool RebuildServer::state_wait_backup_prepare()
{
    auto res = wait_backup_prepare();
    bool rval = false;
    switch (res)
    {
    case StateResult::OK:
        rval = true;
        m_state = State::START_TARGET;
        break;

    case StateResult::AGAIN:
        break;

    case StateResult::ERROR:
        rval = true;
        m_state = State::CLEANUP;
        break;
    }
    return rval;
}

bool RebuildServer::state_start_target()
{
    m_state = start_target() ? State::START_REPLICATION : State::CLEANUP;
    return true;
}

bool RebuildServer::state_start_replication()
{
    // If monitor has a master, replicate from it. Otherwise, replicate from the source server.
    auto master = m_mon.m_master;
    auto repl_master = (master && master->is_master()) ? master : m_source;
    m_state = start_replication(m_target, repl_master) ? State::DONE : State::CLEANUP;
    return true;
}

Result Result::deep_copy() const
{
    return {success, output.deep_copy()};
}

CreateBackup::CreateBackup(MariaDBMonitor& mon, SERVER* source, std::string bu_name)
    : BackupOperation(mon)
    , m_source_srv(source)
    , m_bu_name(std::move(bu_name))
{
    mxb_assert(!m_bu_name.empty());
}

bool CreateBackup::run()
{
    bool command_complete = false;
    bool advance = true;
    while (advance)
    {
        switch (m_state)
        {
        case State::INIT:
            advance = state_init();
            break;

        case State::CHECK_BACKUP_STORAGE:
            advance = state_check_backup_storage();
            break;

        case State::TEST_DATALINK:
            advance = state_test_datalink();
            break;

        case State::SERVE_BACKUP:
            advance = state_serve_backup();
            break;

        case State::START_TRANSFER:
            advance = state_start_transfer();
            break;

        case State::WAIT_TRANSFER:
            advance = state_wait_transfer();
            break;

        case State::CHECK_BACKUP_SIZE:
            state_check_backup();
            break;

        case State::DONE:
            m_result.success = true;
            m_state = State::CLEANUP;
            break;

        case State::CLEANUP:
            state_cleanup();
            command_complete = true;
            advance = false;
            break;
        }
    }

    return command_complete;
}

bool CreateBackup::state_init()
{
    m_state = State::CLEANUP;
    if (check_preconditions())
    {
        set_source(m_source->name(), m_source->server->address());
        set_target("backup storage", m_mon.m_settings.backup_storage_addr);

        if (init_operation())
        {
            m_source_slaves_old = m_source->m_slave_status;     // Backup slave conns
            m_state = State::CHECK_BACKUP_STORAGE;
        }
    }
    return true;
}

bool CreateBackup::state_check_backup_storage()
{
    // Check that the backup storage directory exists and that it does not already contain a backup with
    // the same name.
    bool success = false;
    auto& output = m_result.output;
    string bu_storage_path = m_mon.m_settings.backup_storage_path;

    bool dir_free = true;
    auto check_dir_free_func = [this, &dir_free](const ssh_util::FileInfo& info) {
        if (info.name == m_bu_name)
        {
            dir_free = false;
            return false;
        }
        return true;
    };

    if (check_directory_entries(m_target_ses, m_target_name, bu_storage_path, check_dir_free_func))
    {
        string full_path = bu_storage_path + "/" + m_bu_name;
        if (dir_free)
        {
            string mkdir = mxb::string_printf("mkdir %s", full_path.c_str());
            if (run_cmd_on_target(mkdir, "create backup directory"))
            {
                success = true;
                m_bu_path = full_path;
            }
        }
        else
        {
            PRINT_JSON_ERROR(output, "Backup storage directory '%s' already exists on %s. Cannot save "
                                     "backup.", full_path.c_str(), m_target_name.c_str());
        }
    }

    m_state = success ? State::TEST_DATALINK : State::CLEANUP;
    return true;
}

bool CreateBackup::state_test_datalink()
{
    m_state = test_datalink() ? State::SERVE_BACKUP : State::CLEANUP;
    return true;
}

bool CreateBackup::state_serve_backup()
{
    auto& cs = m_source->conn_settings();
    m_state = serve_backup(cs.username, cs.password) ? State::START_TRANSFER :
        State::CLEANUP;
    return true;
}

void CreateBackup::state_cleanup()
{
    cleanup(m_source, m_source_slaves_old);
}

bool CreateBackup::check_preconditions()
{
    bool rval = false;
    auto& error_out = m_result.output;

    MariaDBServer* source = m_mon.get_server(m_source_srv);
    if (source)
    {
        bool source_ok = check_server_is_valid_source(source);
        bool settings_ok = true;

        if (!check_ssh_settings())
        {
            settings_ok = false;
        }

        if (!check_backup_storage_settings())
        {
            settings_ok = false;
        }

        if (source_ok && settings_ok)
        {
            m_source = source;
            rval = true;
        }
    }
    else
    {
        PRINT_JSON_ERROR(error_out, "%s is not monitored by %s, cannot use it as backup source.",
                         m_source_srv->name(), m_mon.name());
    }

    return rval;
}

void CreateBackup::cancel()
{
    switch (m_state)
    {
    case State::INIT:
        // No need to do anything.
        break;

    case State::WAIT_TRANSFER:
        state_cleanup();
        break;

    default:
        mxb_assert(!true);
    }
}

bool CreateBackup::state_start_transfer()
{
    m_state = start_transfer(m_bu_path, TargetOwner::NORMAL) ? State::WAIT_TRANSFER : State::CLEANUP;
    return true;
}

bool CreateBackup::state_wait_transfer()
{
    auto res = wait_transfer();
    bool rval = false;
    switch (res)
    {
    case StateResult::OK:
        rval = true;
        m_state = State::CHECK_BACKUP_SIZE;
        break;

    case StateResult::AGAIN:
        break;

    case StateResult::ERROR:
        rval = true;
        m_state = State::CLEANUP;
        break;
    }
    return rval;
}

void CreateBackup::state_check_backup()
{
    m_state = check_datadir(m_target_ses, m_bu_path) ? State::DONE : State::CLEANUP;
}

RestoreFromBackup::RestoreFromBackup(MariaDBMonitor& mon, SERVER* target, string bu_name)
    : BackupOperation(mon)
    , m_target_srv(target)
    , m_bu_name(std::move(bu_name))
{
    mxb_assert(!m_bu_name.empty());
}

bool RestoreFromBackup::run()
{
    bool command_complete = false;
    bool advance = true;
    while (advance)
    {
        switch (m_state)
        {
        case State::INIT:
            advance = state_init();
            break;

        case State::CHECK_BACKUP_STORAGE:
            advance = state_check_backup_storage();
            break;

        case State::TEST_DATALINK:
            advance = state_test_datalink();
            break;

        case State::SERVE_BACKUP:
            advance = state_serve_backup();
            break;

        case State::PREPARE_TARGET:
            advance = state_prepare_target();
            break;

        case State::START_TRANSFER:
            advance = state_start_transfer();
            break;

        case State::WAIT_TRANSFER:
            advance = state_wait_transfer();
            break;

        case State::CHECK_DATADIR_SIZE:
            state_check_datadir();
            break;

        case State::START_BACKUP_PREPARE:
            advance = state_start_backup_prepare();
            break;

        case State::WAIT_BACKUP_PREPARE:
            advance = state_wait_backup_prepare();
            break;

        case State::START_TARGET:
            advance = state_start_target();
            break;

        case State::START_REPLICATION:
            advance = state_start_replication();
            break;

        case State::DONE:
            m_result.success = true;
            m_state = State::CLEANUP;
            break;

        case State::CLEANUP:
            state_cleanup();
            command_complete = true;
            advance = false;
            break;
        }
    }

    return command_complete;
}

void RestoreFromBackup::cancel()
{
    switch (m_state)
    {
    case State::INIT:
        // No need to do anything.
        break;

    case State::WAIT_TRANSFER:
    case State::WAIT_BACKUP_PREPARE:
        state_cleanup();
        break;

    default:
        mxb_assert(!true);
    }
}

bool RestoreFromBackup::state_init()
{
    m_state = State::CLEANUP;
    if (check_preconditions())
    {
        set_source("backup storage", m_mon.m_settings.backup_storage_addr);
        set_target(m_target->name(), m_target->server->address());

        if (init_operation())
        {
            m_state = State::CHECK_BACKUP_STORAGE;
        }
    }
    return true;
}

bool RestoreFromBackup::check_preconditions()
{
    auto& error_out = m_result.output;
    MariaDBServer* target = m_mon.get_server(m_target_srv);
    if (!target)
    {
        PRINT_JSON_ERROR(error_out, "%s is not monitored by %s, cannot rebuild it.",
                         m_target_srv->name(), m_mon.name());
    }

    bool rval = false;
    if (target)
    {
        bool target_ok = check_server_is_valid_target(target);
        bool settings_ok = true;

        if (!check_ssh_settings())
        {
            settings_ok = false;
        }

        if (!check_backup_storage_settings())
        {
            settings_ok = false;
        }

        if (target_ok && settings_ok)
        {
            m_target = target;
            rval = true;
        }
    }
    return rval;
}

bool RestoreFromBackup::state_test_datalink()
{
    m_state = test_datalink() ? State::SERVE_BACKUP : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_check_backup_storage()
{
    bool success = false;
    auto& output = m_result.output;
    const string& bu_storage_path = m_mon.m_settings.backup_storage_path;

    bool bu_dir_found = false;
    bool bu_dir_ok = true;
    auto bu_storage_check_func = [&](const ssh_util::FileInfo& info) {
        bool check_next = true;
        const string& ssh_user = m_mon.m_settings.ssh_user;

        if (info.name == m_bu_name)
        {
            bu_dir_found = true;
            if (info.owner != ssh_user)
            {
                bu_dir_ok = false;
                PRINT_JSON_ERROR(output, "Backup storage directory '%s/%s' is not owned by '%s'. "
                                         "Cannot use backup.", bu_storage_path.c_str(),
                                 m_bu_name.c_str(), ssh_user.c_str());
            }
            if (info.type != ssh_util::FileType::DIR)
            {
                bu_dir_ok = false;
                PRINT_JSON_ERROR(output, "Backup storage path '%s/%s' is not a directory. "
                                         "Cannot use backup.", bu_storage_path.c_str(),
                                 m_bu_name.c_str());
            }
            check_next = false;
        }
        return check_next;
    };

    if (check_directory_entries(m_source_ses, m_source_name, bu_storage_path, bu_storage_check_func))
    {
        if (!bu_dir_found)
        {
            PRINT_JSON_ERROR(output, "Main backup storage directory '%s' does not contain '%s'. "
                                     "Cannot use backup.", bu_storage_path.c_str(), m_bu_name.c_str());
        }
        else if (bu_dir_ok)
        {
            // Finally, check that "backup-my.cnf" exists.
            const string search_file = "backup-my.cnf";
            bool file_found = false;
            auto bu_check_func = [&file_found, &search_file](const ssh_util::FileInfo& info) {
                if (info.name == search_file)
                {
                    file_found = true;
                    return false;
                }
                return true;
            };

            string full_path = bu_storage_path + "/" + m_bu_name;
            if (check_directory_entries(m_source_ses, m_source_name, full_path, bu_check_func))
            {
                if (file_found)
                {
                    success = true;
                    m_bu_path = full_path;
                }
                else
                {
                    PRINT_JSON_ERROR(output, "Directory '%s' does not contain file '%s'. Cannot use "
                                             "backup.", full_path.c_str(), search_file.c_str());
                }
            }
        }
    }

    m_state = success ? State::TEST_DATALINK : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_serve_backup()
{
    auto source_name = m_source_name.c_str();
    // Start serving the backup stream. The source will wait for a new connection. Since MariaDB Server is
    // not running on this machine, serve the data directory as a tar archive.
    const char stream_fmt[] = "tar -zc -C %s . | socat - TCP-LISTEN:%i,reuseaddr";
    string stream_cmd = mxb::string_printf(stream_fmt, m_bu_path.c_str(), source_port());
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_source_ses, stream_cmd);

    auto& error_out = m_result.output;
    bool success = false;
    if (cmd_handle)
    {
        // Wait a bit, then check that command started successfully and is still running.
        sleep(1);
        auto status = cmd_handle->update_status();

        if (status == ssh_util::AsyncCmd::Status::BUSY)
        {
            success = true;
            m_source_cmd = std::move(cmd_handle);
            MXB_NOTICE("%s serving backup on port %i.", source_name, source_port());
        }
        else if (status == ssh_util::AsyncCmd::Status::READY)
        {
            // The stream serve operation ended before it should. Print all output available.
            int rc = cmd_handle->rc();
            string message = mxb::string_printf(
                "Failed to stream data from %s. Command '%s' stopped before data was streamed. Return "
                "value %i. Output: '%s' Error output: '%s'", source_name, stream_cmd.c_str(), rc,
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
                         source_name, ssh_errmsg.c_str());
    }

    m_state = success ? State::PREPARE_TARGET : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_prepare_target()
{
    m_state = prepare_target() ? State::START_TRANSFER : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_start_transfer()
{
    bool transfer_started = false;
    // Connect to source and start stream the backup.
    const char receive_fmt[] = "socat -u TCP:%s:%i,connect-timeout=%i STDOUT | sudo tar -xz -C %s/";
    string receive_cmd = mxb::string_printf(receive_fmt, m_source_host.c_str(), source_port(),
                                            socat_timeout_s, rebuild_datadir.c_str());

    bool rval = false;
    auto [cmd_handle, ssh_errmsg] = ssh_util::start_async_cmd(m_target_ses, receive_cmd);
    if (cmd_handle)
    {
        transfer_started = true;
        m_target_cmd = std::move(cmd_handle);
        MXB_NOTICE("Backup transfer from %s to %s started.", m_source_name.c_str(), m_target_name.c_str());
    }
    else
    {
        PRINT_JSON_ERROR(m_result.output, "Failed to start receiving data to %s. %s",
                         m_target_name.c_str(), ssh_errmsg.c_str());
    }

    m_state = transfer_started ? State::WAIT_TRANSFER : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_wait_transfer()
{
    auto res = wait_transfer();
    bool rval = false;
    switch (res)
    {
    case StateResult::OK:
        rval = true;
        m_state = State::CHECK_DATADIR_SIZE;
        break;

    case StateResult::AGAIN:
        break;

    case StateResult::ERROR:
        rval = true;
        m_state = State::CLEANUP;
        break;
    }
    return rval;
}

void RestoreFromBackup::state_check_datadir()
{
    m_state = check_datadir(m_target_ses, rebuild_datadir) ? State::START_BACKUP_PREPARE :
        State::CLEANUP;
}

bool RestoreFromBackup::state_start_backup_prepare()
{
    m_state = start_backup_prepare() ? State::WAIT_BACKUP_PREPARE : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_start_target()
{
    m_state = start_target() ? State::START_REPLICATION : State::CLEANUP;
    return true;
}

bool RestoreFromBackup::state_start_replication()
{
    // If monitor has a valid master, replicate from it. Otherwise, skip this step.
    auto master = m_mon.m_master;
    if (master && master->is_master())
    {
        m_state = start_replication(m_target, master) ? State::DONE : State::CLEANUP;
    }
    else
    {
        MXB_WARNING("No primary for monitor %s, not starting replication on %s.",
                    m_mon.name(), m_target_name.c_str());
        m_state = State::DONE;
    }
    return true;
}

void RestoreFromBackup::state_cleanup()
{
    cleanup(nullptr, {});
}
}
