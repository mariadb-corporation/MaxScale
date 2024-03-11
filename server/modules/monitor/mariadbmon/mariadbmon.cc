/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file A MariaDB replication cluster monitor
 */
#include "mariadbmon.hh"

#include <future>
#include <cinttypes>
#include <mysql.h>
#include <maxbase/assert.hh>
#include <maxbase/format.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/secrets.hh>
#include <maxscale/utils.hh>

using std::string;
using maxbase::string_printf;
using maxscale::Monitor;
using maxscale::MonitorServer;

// Config parameter names
const char* const CN_AUTO_FAILOVER = "auto_failover";
const char* const CN_SWITCHOVER_ON_LOW_DISK_SPACE = "switchover_on_low_disk_space";
const char* const CN_MAINTENANCE_ON_LOW_DISK_SPACE = "maintenance_on_low_disk_space";
const char* const CN_PROMOTION_SQL_FILE = "promotion_sql_file";
const char* const CN_DEMOTION_SQL_FILE = "demotion_sql_file";
const char* const CN_HANDLE_EVENTS = "handle_events";

static const char CN_AUTO_REJOIN[] = "auto_rejoin";
static const char CN_FAILCOUNT[] = "failcount";
static const char CN_ENFORCE_READONLY[] = "enforce_read_only_slaves";
static const char CN_ENFORCE_SIMPLE_TOPOLOGY[] = "enforce_simple_topology";
static const char CN_NO_PROMOTE_SERVERS[] = "servers_no_promotion";
static const char CN_FAILOVER_TIMEOUT[] = "failover_timeout";
static const char CN_SWITCHOVER_TIMEOUT[] = "switchover_timeout";
static const char CN_ASSUME_UNIQUE_HOSTNAMES[] = "assume_unique_hostnames";


// Parameters for master failure verification and timeout
static const char CN_VERIFY_MASTER_FAILURE[] = "verify_master_failure";
static const char CN_MASTER_FAILURE_TIMEOUT[] = "master_failure_timeout";
// Replication credentials parameters for failover/switchover/join
static const char CN_REPLICATION_USER[] = "replication_user";
static const char CN_REPLICATION_PASSWORD[] = "replication_password";
static const char CN_REPLICATION_MASTER_SSL[] = "replication_master_ssl";

namespace
{
const char ENFORCE_WRITABLE_MASTER[] = "enforce_writable_master";
const char CLUSTER_OP_REQUIRE_LOCKS[] = "cooperative_monitoring_locks";
const char MASTER_CONDITIONS[] = "master_conditions";
const char SLAVE_CONDITIONS[] = "slave_conditions";
const char SCRIPT_MAX_RLAG[] = "script_max_replication_lag";

using namespace std::chrono_literals;
namespace cfg = mxs::config;

class Spec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    template<class Params>
    bool do_post_validate(Params& params) const;

    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(const cfg::Configuration* config,
                       json_t* json,
                       const std::map<std::string, json_t*>& nested_params) const override
    {
        return do_post_validate(json);
    }
};

cfg::Specification s_spec(MXB_MODULE_NAME, cfg::Specification::MONITOR);

cfg::ParamCount s_failcount(
    &s_spec, CN_FAILCOUNT,
    "Number of failures to tolerate before failover occurs",
    5, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_auto_failover(
    &s_spec, CN_AUTO_FAILOVER,
    "Enable automatic server failover",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_failover_timeout(
    &s_spec, CN_FAILOVER_TIMEOUT,
    "Timeout for failover",
    90s, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_switchover_timeout(
    &s_spec, CN_SWITCHOVER_TIMEOUT,
    "Timeout for switchover",
    90s, cfg::Param::AT_RUNTIME);

cfg::ParamString s_replication_user(
    &s_spec, CN_REPLICATION_USER,
    "User used for replication",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamPassword s_replication_password(
    &s_spec, CN_REPLICATION_PASSWORD,
    "Password for the user that is used for replication",
    "", cfg::Param::AT_RUNTIME);

cfg::ParamBool s_replication_master_ssl(
    &s_spec, CN_REPLICATION_MASTER_SSL,
    "Enable SSL when configuring replication",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamReplOpts s_replication_custom_opts(
    &s_spec, "replication_custom_options",
    "Custom CHANGE MASTER TO options", cfg::Param::AT_RUNTIME);

cfg::ParamBool s_verify_master_failure(
    &s_spec, CN_VERIFY_MASTER_FAILURE,
    "Verify master failure",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamSeconds s_master_failure_timeout(
    &s_spec, CN_MASTER_FAILURE_TIMEOUT,
    "Master failure timeout",
    10s, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_auto_rejoin(
    &s_spec, CN_AUTO_REJOIN,
    "Enable automatic server rejoin",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_enforce_read_only_slaves(
    &s_spec, CN_ENFORCE_READONLY,
    "Enable read_only on all slave servers",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_enforce_writable_master(
    &s_spec, ENFORCE_WRITABLE_MASTER,
    "Disable read_only on the current master server",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamServerList s_server_no_promotion(
    &s_spec, CN_NO_PROMOTE_SERVERS,
    "List of servers that are never promoted",
    cfg::Param::OPTIONAL, cfg::Param::AT_RUNTIME);

cfg::ParamPath s_promotion_sql_file(
    &s_spec, CN_PROMOTION_SQL_FILE,
    "Path to SQL file that is executed during node promotion",
    cfg::ParamPath::R, "", cfg::Param::AT_RUNTIME);

cfg::ParamPath s_demotion_sql_file(
    &s_spec, CN_DEMOTION_SQL_FILE,
    "Path to SQL file that is executed during node demotion",
    cfg::ParamPath::R, "", cfg::Param::AT_RUNTIME);

cfg::ParamBool s_switchover_on_low_disk_space(
    &s_spec, CN_SWITCHOVER_ON_LOW_DISK_SPACE,
    "Perform a switchover when a server runs out of disk space",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_maintenance_on_low_disk_space(
    &s_spec, CN_MAINTENANCE_ON_LOW_DISK_SPACE,
    "Put the server into maintenance mode when it runs out of disk space",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_handle_events(
    &s_spec, CN_HANDLE_EVENTS,
    "Manage server-side events",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_assume_unique_hostnames(
    &s_spec, CN_ASSUME_UNIQUE_HOSTNAMES,
    "Assume that hostnames are unique",
    true, cfg::Param::AT_RUNTIME);

cfg::ParamBool s_enforce_simple_topology(
    &s_spec, CN_ENFORCE_SIMPLE_TOPOLOGY,
    "Enforce a simple topology",
    false, cfg::Param::AT_RUNTIME);

cfg::ParamEnum<MariaDBMonitor::RequireLocks> s_cooperative_monitoring_locks(
    &s_spec, CLUSTER_OP_REQUIRE_LOCKS,
    "Cooperative monitoring type",
    {
        {MariaDBMonitor::LOCKS_NONE, "none"},
        {MariaDBMonitor::LOCKS_MAJORITY_RUNNING, "majority_of_running"},
        {MariaDBMonitor::LOCKS_MAJORITY_ALL, "majority_of_all"},
    }, MariaDBMonitor::LOCKS_NONE, cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<uint32_t> s_master_conditions(
    &s_spec, MASTER_CONDITIONS,
    "Conditions that the master servers must meet",
    {
        {MasterConds::MCOND_NONE, "none"},
        {MasterConds::MCOND_CONNECTING_S, "connecting_slave"},
        {MasterConds::MCOND_CONNECTED_S, "connected_slave"},
        {MasterConds::MCOND_RUNNING_S, "running_slave"},
        {MasterConds::MCOND_COOP_M, "primary_monitor_master"},
        {MasterConds::MCOND_DISK_OK, "disk_space_ok"}
    },
    MasterConds::MCOND_COOP_M | MasterConds::MCOND_DISK_OK, cfg::Param::AT_RUNTIME);

cfg::ParamEnumMask<uint32_t> s_slave_conditions(
    &s_spec, SLAVE_CONDITIONS,
    "Conditions that the slave servers must meet",
    {
        {SlaveConds::SCOND_NONE, "none"},
        {SlaveConds::SCOND_LINKED_M, "linked_master"},
        {SlaveConds::SCOND_RUNNING_M, "running_master"},
        {SlaveConds::SCOND_WRITABLE_M, "writable_master"},
        {SlaveConds::SCOND_COOP_M, "primary_monitor_master"},
        {SlaveConds::SCOND_DISK_OK, "disk_space_ok"}
    },
    SlaveConds::SCOND_NONE, cfg::Param::AT_RUNTIME);

cfg::ParamInteger s_script_max_rlag(
    &s_spec, SCRIPT_MAX_RLAG,
    "Replication lag limit at which the script is run",
    -1, cfg::Param::AT_RUNTIME);

cfg::ParamCount s_cs_admin_port(
    &s_spec, "cs_admin_port", "Port of the ColumnStore administrative daemon.", 8640);

const char CS_ADMIN_BASE_PATH_DESC[] =
    "The base path to be used when accessing the ColumnStore administrative daemon. "
    "If, for instance, a daemon URL is https://localhost:8640/cmapi/0.4.0/node/start "
    "then the admin_base_path is \"/cmapi/0.4.0\".";
cfg::ParamString s_cs_admin_base_path(&s_spec, "cs_admin_base_path", CS_ADMIN_BASE_PATH_DESC, "/cmapi/0.4.0");

cfg::ParamString s_cs_admin_api_key(&s_spec, "cs_admin_api_key", "The API key used in communication with the "
                                                                 "ColumnStore admin daemon.", "");

cfg::ParamString s_ssh_user(&s_spec, CONFIG_SSH_USER,
                            "SSH username. Used for running remote commands on servers.", "");
cfg::ParamPath s_ssh_keyfile(&s_spec, CONFIG_SSH_KEYFILE,
                             "SSH keyfile. Used for running remote commands on servers.",
                             cfg::ParamPath::R | cfg::ParamPath::F, "");
cfg::ParamBool s_ssh_check_host_key(&s_spec, "ssh_check_host_key", "Is SSH host key check enabled.", true,
                                    cfg::Param::AT_RUNTIME);
cfg::ParamSeconds s_ssh_timeout(&s_spec, "ssh_timeout", "SSH connection and command timeout", 10s,
                                cfg::Param::AT_RUNTIME);
cfg::ParamCount s_ssh_port(&s_spec, "ssh_port", "SSH port. Used for running remote commands on servers.",
                           22, 0, 65535, cfg::Param::AT_RUNTIME);
cfg::ParamCount s_rebuild_port(&s_spec, "rebuild_port", "Listen port used for transferring server backup.",
                               4444, 0, 65535, cfg::Param::AT_RUNTIME);
cfg::ParamString s_mbu_use_memory(&s_spec, "mariabackup_use_memory", "Mariabackup buffer pool size.",
                                  "1G", cfg::Param::AT_RUNTIME);
cfg::ParamInteger s_mbu_parallel(&s_spec, "mariabackup_parallel", "Mariabackup thread count.",
                                 1, 1, 1000, cfg::Param::AT_RUNTIME);
cfg::ParamString s_backup_storage_addr(&s_spec, CONFIG_BACKUP_ADDR, "Address of backup storage.", "",
                                       cfg::Param::AT_RUNTIME);
cfg::ParamString s_backup_storage_path(&s_spec, CONFIG_BACKUP_PATH, "Backup storage directory path.", "",
                                       cfg::Param::AT_RUNTIME);

template<class Params>
bool Spec::do_post_validate(Params& params) const
{
    bool ok = true;
    auto repl_user = s_replication_user.get(params);
    auto repl_pw = s_replication_password.get(params);

    if (repl_user.empty() != repl_pw.empty())
    {
        MXB_ERROR("Both '%s' and '%s' must be defined.",
                  s_replication_user.name().c_str(), s_replication_password.name().c_str());
        ok = false;
    }

    if (!s_assume_unique_hostnames.get(params))
    {
        const char PARAM_REQUIRES[] = "'%s' requires that '%s' is enabled.";
        if (s_auto_failover.get(params))
        {
            MXB_ERROR(PARAM_REQUIRES, s_auto_failover.name().c_str(),
                      s_assume_unique_hostnames.name().c_str());
            ok = false;
        }
        if (s_switchover_on_low_disk_space.get(params))
        {
            MXB_ERROR(PARAM_REQUIRES, s_switchover_on_low_disk_space.name().c_str(),
                      s_assume_unique_hostnames.name().c_str());
            ok = false;
        }
        if (s_auto_rejoin.get(params))
        {
            MXB_ERROR(PARAM_REQUIRES, s_auto_rejoin.name().c_str(), s_assume_unique_hostnames.name().c_str());
            ok = false;
        }
    }

    return ok;
}

auto mo_relaxed = std::memory_order_relaxed;
auto mo_acquire = std::memory_order_acquire;
auto mo_release = std::memory_order_release;
}

MariaDBMonitor::MariaDBMonitor(const string& name, const string& module)
    : Monitor(name, module)
    , m_settings(name, this)
{
}

/**
 * Reset and initialize server arrays and related data.
 */
void MariaDBMonitor::reset_server_info()
{
    m_servers_by_id.clear();
    assign_new_master(nullptr);
    m_next_master = nullptr;
    m_master_gtid_domain = GTID_DOMAIN_UNKNOWN;
    m_resolver = DNSResolver();     // Erases result cache.
}

void MariaDBMonitor::reset_node_index_info()
{
    for (auto server : m_servers)
    {
        server->m_node.reset_indexes();
    }
}

MariaDBServer* MariaDBMonitor::get_server_by_addr(const EndPoint& search_ep)
{
    MariaDBServer* found = nullptr;
    for (auto* server : m_servers)
    {
        if (search_ep.points_to_server(*server->server))
        {
            found = server;
            break;
        }
    }

    if (!found)
    {
        // Server was not found with simple string compare. Try DNS resolving for endpoints with
        // matching ports. Name lookup both the search target and server normal and private addresses.
        DNSResolver::StringSet search_addrs = m_resolver.resolve_server(search_ep.host());
        if (!search_addrs.empty())
        {
            auto server_host_matches_addr = [this, &search_addrs](const char* server_host) {
                if (*server_host)
                {
                    auto server_addresses = m_resolver.resolve_server(server_host);
                    auto server_address_matches = [&search_addrs](const string& srv_addr) {
                        return search_addrs.count(srv_addr) > 0;
                    };
                    return std::any_of(server_addresses.begin(), server_addresses.end(),
                                       server_address_matches);
                }
                return false;
            };

            for (auto* server : m_servers)
            {
                SERVER* srv = server->server;
                if (srv->port() == search_ep.port())
                {
                    if (server_host_matches_addr(srv->address())
                        || server_host_matches_addr(srv->private_address()))
                    {
                        found = server;
                        break;
                    }
                }
            }
        }
    }
    return found;
}

MariaDBServer* MariaDBMonitor::get_server(int64_t id)
{
    auto found = m_servers_by_id.find(id);
    return (found != m_servers_by_id.end()) ? (*found).second : NULL;
}

MariaDBServer* MariaDBMonitor::get_server(MonitorServer* mon_server) const
{
    return get_server(mon_server->server);
}

MariaDBServer* MariaDBMonitor::get_server(SERVER* server) const
{
    for (auto& srv : m_servers)
    {
        if (srv->server == server)
        {
            return srv;
        }
    }
    return nullptr;
}

MariaDBMonitor* MariaDBMonitor::create(const string& name, const string& module)
{
    return new MariaDBMonitor(name, module);
}

mxs::config::Configuration& MariaDBMonitor::configuration()
{
    return m_settings;
}

MariaDBMonitor::Settings::Settings(const std::string& name, MariaDBMonitor* monitor)
    : mxs::config::Configuration(name, &s_spec)
    , m_monitor(monitor)
{
    using Shared = MariaDBServer::SharedSettings;

    add_native(&Settings::assume_unique_hostnames, &s_assume_unique_hostnames);
    add_native(&Settings::failcount, &s_failcount);
    add_native(&Settings::failover_timeout, &s_failover_timeout);
    add_native(&Settings::shared, &Shared::switchover_timeout, &s_switchover_timeout);
    add_native(&Settings::auto_failover, &s_auto_failover);
    add_native(&Settings::auto_rejoin, &s_auto_rejoin);
    add_native(&Settings::enforce_read_only_slaves, &s_enforce_read_only_slaves);
    add_native(&Settings::enforce_writable_master, &s_enforce_writable_master);
    add_native(&Settings::enforce_simple_topology, &s_enforce_simple_topology);
    add_native(&Settings::verify_master_failure, &s_verify_master_failure);
    add_native(&Settings::master_failure_timeout, &s_master_failure_timeout);
    add_native(&Settings::switchover_on_low_disk_space, &s_switchover_on_low_disk_space);
    add_native(&Settings::maintenance_on_low_disk_space, &s_maintenance_on_low_disk_space);
    add_native(&Settings::require_server_locks, &s_cooperative_monitoring_locks);
    add_native(&Settings::shared, &Shared::master_conds, &s_master_conditions);
    add_native(&Settings::shared, &Shared::slave_conds, &s_slave_conditions);
    add_native(&Settings::script_max_rlag, &s_script_max_rlag);
    add_native(&Settings::servers_no_promotion, &s_server_no_promotion);
    add_native(&Settings::shared, &Shared::promotion_sql_file, &s_promotion_sql_file);
    add_native(&Settings::shared, &Shared::demotion_sql_file, &s_demotion_sql_file);
    add_native(&Settings::shared, &Shared::handle_event_scheduler, &s_handle_events);
    add_native(&Settings::shared, &Shared::replication_ssl, &s_replication_master_ssl);
    add_native(&Settings::shared, &Shared::replication_custom_opts, &s_replication_custom_opts);
    add_native(&Settings::shared, &Shared::replication_user, &s_replication_user);
    add_native(&Settings::shared, &Shared::replication_password, &s_replication_password);
    add_native(&Settings::cs_admin_port, &s_cs_admin_port);
    add_native(&Settings::cs_admin_base_path, &s_cs_admin_base_path);
    add_native(&Settings::cs_admin_api_key, &s_cs_admin_api_key);
    add_native(&Settings::ssh_user, &s_ssh_user);
    add_native(&Settings::ssh_keyfile, &s_ssh_keyfile);
    add_native(&Settings::ssh_host_check, &s_ssh_check_host_key);
    add_native(&Settings::ssh_timeout, &s_ssh_timeout);
    add_native(&Settings::ssh_port, &s_ssh_port);
    add_native(&Settings::rebuild_port, &s_rebuild_port);
    add_native(&Settings::mbu_use_memory, &s_mbu_use_memory);
    add_native(&Settings::mbu_parallel, &s_mbu_parallel);
    add_native(&Settings::backup_storage_addr, &s_backup_storage_addr);
    add_native(&Settings::backup_storage_path, &s_backup_storage_path);
}

bool MariaDBMonitor::Settings::post_configure(const std::map<std::string,
                                                             mxs::ConfigParameters>& nested_params)
{
    shared.server_locks_enabled = require_server_locks != RequireLocks::LOCKS_NONE;

    if (enforce_simple_topology)
    {
        // This is a "mega-setting" which turns on several other features regardless of their individual
        // settings.
        auto warn_and_enable = [](bool* setting, const char* setting_name) {
            const char setting_activated[] = "%s enables %s, overriding any existing setting or default.";
            if (*setting == false)
            {
                *setting = true;
                MXB_WARNING(setting_activated, CN_ENFORCE_SIMPLE_TOPOLOGY, setting_name);
            }
        };

        warn_and_enable(&assume_unique_hostnames, CN_ASSUME_UNIQUE_HOSTNAMES);
        warn_and_enable(&auto_failover, CN_AUTO_FAILOVER);
        warn_and_enable(&auto_rejoin, CN_AUTO_REJOIN);
    }

    return m_monitor->post_configure();
}

/**
 * Load config parameters
 *
 * @param params Config parameters
 * @return True if settings are ok
 */
bool MariaDBMonitor::post_configure()
{
    /* Reset all monitored state info. The server dependent values must be reset as servers could have been
     * added, removed and modified. */
    reset_server_info();
    // Operation data is cleared in post_run.

    if (m_settings.shared.replication_user.empty())
    {
        m_settings.shared.replication_user = conn_settings().username;
        m_settings.shared.replication_password = conn_settings().password;
    }

    auto [ok, excluded] = get_monitored_serverlist(m_settings.servers_no_promotion);

    if (ok)
    {
        m_excluded_servers.clear();

        for (auto* srv : excluded)
        {
            m_excluded_servers.push_back(static_cast<MariaDBServer*>(srv));
        }

        m_http_config.headers["x-api-key"] = m_settings.cs_admin_api_key;
        m_http_config.headers["content-type"] = "application/json";

        // The CS daemon uses a self-signed certificate.
        m_http_config.ssl_verifypeer = false;
        m_http_config.ssl_verifyhost = false;
    }

    return ok;
}

json_t* MariaDBMonitor::diagnostics() const
{
    mxb_assert(mxs::MainWorker::is_current());
    return to_json();
}

json_t* MariaDBMonitor::diagnostics(MonitorServer* srv) const
{
    mxb_assert(mxs::MainWorker::is_current());
    json_t* result = nullptr;

    if (auto server = get_server(srv))
    {
        result = server->to_json();
    }

    return result;
}

json_t* MariaDBMonitor::to_json(State op)
{
    switch (op)
    {
    case State::IDLE:
        return json_string("Idle");

    case State::MONITOR:
        return json_string("Monitoring servers");

    case State::EXECUTE_SCRIPTS:
        return json_string("Executing scripts");

    case State::DEMOTE:
        return json_string("Demoting old primary");

    case State::WAIT_FOR_TARGET_CATCHUP:
        return json_string("Waiting for candidate primary to catch up");

    case State::PROMOTE_TARGET:
        return json_string("Promoting candidate primary");

    case State::REJOIN:
        return json_string("Rejoining slave servers");

    case State::CONFIRM_REPLICATION:
        return json_string("Confirming that replication works");

    case State::RESET_REPLICATION:
        return json_string("Resetting replication on all servers");
    }

    mxb_assert(!true);
    return nullptr;
}

json_t* MariaDBMonitor::to_json() const
{
    mxb_assert(mxs::MainWorker::is_current());
    json_t* rval = Monitor::diagnostics();

    // The m_master-pointer can be modified during a tick, but the pointed object cannot be deleted.
    auto master = mxb::atomic::load(&m_master, mxb::atomic::RELAXED);
    json_object_set_new(rval, "master", master == nullptr ? json_null() : json_string(master->name()));
    json_object_set_new(rval,
                        "master_gtid_domain_id",
                        m_master_gtid_domain == GTID_DOMAIN_UNKNOWN ? json_null() :
                        json_integer(m_master_gtid_domain));
    json_object_set_new(rval, "state", to_json(m_state));
    json_object_set_new(rval, "primary",
                        server_locks_in_use() ? json_boolean(is_cluster_owner()) : json_null());

    json_t* server_info = json_array();
    // Accessing servers-array is ok since it's only changed from main worker thread.
    for (auto& server : m_servers)
    {
        json_array_append_new(server_info, server->to_json());
    }
    json_object_set_new(rval, "server_info", server_info);
    return rval;
}

bool MariaDBMonitor::can_be_disabled(const mxs::MonitorServer& mserver, DisableType type,
                                     std::string* errmsg_out) const
{
    // If the server is the master, it cannot be disabled.
    bool can_be = !status_is_master(mserver.server->status());

    if (!can_be)
    {
        *errmsg_out =
            "The server is primary, so it cannot be set in maintenance or draining mode. "
            "First perform a switchover and then retry the operation.";
    }

    return can_be;
}

bool MariaDBMonitor::is_cluster_owner() const
{
    return m_locks_info.have_lock_majority.load(std::memory_order_relaxed);
}

void MariaDBMonitor::pre_loop()
{
    // Read the journal and the last known master.
    read_journal();

    m_locks_info.reset();
    m_op_info.monitor_stopping = false;
}

void MariaDBMonitor::post_loop()
{
    write_journal();
    // The operation data needs to be cleared on stop. The monitor may be reconfigured, and the operation
    // data may not be compatible with the new config. This will also cut any SSH connections used by
    // server rebuild.
    m_running_op = nullptr;
    m_op_info.monitor_stopping = true;

    for (auto srv : m_servers)
    {
        srv->close_conn();
    }
}

std::tuple<bool, std::string> MariaDBMonitor::prepare_to_stop()
{
    using ExecState = mon_op::ExecState;
    mxb_assert(Monitor::is_main_worker());
    mxb_assert(is_running());

    bool stopping = false;
    string errmsg;
    std::unique_lock<std::mutex> lock(m_op_info.lock);
    auto op_state = m_op_info.exec_state.load(mo_acquire);
    if (op_state == ExecState::SCHEDULED || op_state == ExecState::RUNNING)
    {
        const char* state_str = (op_state == ExecState::SCHEDULED) ? "pending" : "running";
        errmsg = mxb::string_printf("Command '%s' is %s.", m_op_info.op_name.c_str(), state_str);
    }
    else
    {
        stopping = true;
        // Ensure that monitor cannot start another operation during shutdown.
        m_op_info.monitor_stopping = true;
    }
    lock.unlock();

    return {stopping, errmsg};
}

void MariaDBMonitor::tick()
{
    m_state = State::MONITOR;
    check_maintenance_requests();

    for (auto srv : m_servers)
    {
        srv->stash_current_status();
    }

    // Query all servers for their status.
    bool first_tick = ticks_complete() == 0;
    bool should_update_disk_space = check_disk_space_this_tick();

    // Concurrently query all servers for their status.
    auto update_task = [this, should_update_disk_space, first_tick](MariaDBServer* server) {
        server->update_server(should_update_disk_space, first_tick, server == m_master);
    };
    execute_task_all_servers(update_task);

    update_cluster_lock_status();

    for (MariaDBServer* server : m_servers)
    {
        if (server->m_topology_changed)
        {
            m_cluster_topology_changed = true;
            server->m_topology_changed = false;
        }
    }
    update_topology();

    if (m_cluster_topology_changed)
    {
        m_cluster_topology_changed = false;
        // If cluster operations are enabled, check topology support and disable if needed.
        if (m_settings.auto_failover || m_settings.switchover_on_low_disk_space || m_settings.auto_rejoin)
        {
            check_cluster_operations_support();
        }
    }

    // Always re-assign master, slave etc bits as these depend on other factors outside topology
    // (e.g. slave sql state).
    assign_server_roles();

    if (m_master && m_master->is_master())
    {
        // Update cluster-wide values dependant on the current master.
        update_gtid_domain();

        if (m_settings.auto_failover)
        {
            m_master->check_semisync_master_status();
        }
    }

    /* Set low disk space slaves to maintenance. This needs to happen after roles have been assigned.
     * Is not a real cluster operation, since nothing on the actual backends is changed. */
    if (m_settings.maintenance_on_low_disk_space)
    {
        set_low_disk_slaves_maintenance();
    }

    // Sanity check. Master may not be both slave and master.
    mxb_assert(m_master == nullptr || !m_master->has_status(SERVER_SLAVE | SERVER_MASTER));

    if (server_locks_in_use() && is_cluster_owner())
    {
        check_acquire_masterlock();
    }

    flush_mdb_server_status();
    process_state_changes();
    hangup_failed_servers();
    write_journal_if_needed();
    if (m_cluster_modified)
    {
        request_fast_ticks();
    }
    m_state = State::IDLE;
}

void MariaDBMonitor::process_state_changes()
{
    using ExecState = mon_op::ExecState;
    m_state = State::EXECUTE_SCRIPTS;
    detect_handle_state_changes();

    m_cluster_modified = false;
    if (cluster_operation_disable_timer > 0)
    {
        cluster_operation_disable_timer--;
    }

    auto run_operation = [this]() {
        if (m_op_info.cancel_op.load())
        {
            m_running_op->cancel();
            m_running_op = nullptr;
            std::lock_guard<std::mutex> guard(m_op_info.lock);
            MXB_NOTICE("Running %s canceled.", m_op_info.op_name.c_str());
            m_op_info.op_name.clear();
            m_op_info.exec_state = ExecState::DONE;
            m_op_info.current_op_is_manual = false;
        }
        else
        {
            auto complete = m_running_op->run();
            if (complete)
            {
                bool running_cmd_is_manual = false;
                // Operation complete. If it was user-triggered, save results and notify the waiting thread
                // (if any) that it can continue.
                std::unique_lock<std::mutex> lock(m_op_info.lock);

                mxb_assert(m_op_info.exec_state == ExecState::RUNNING);
                running_cmd_is_manual = m_op_info.current_op_is_manual;
                if (running_cmd_is_manual)
                {
                    // Should not have any previous results, since previous results were erased when
                    // the current command was scheduled.
                    auto& res_storage = m_op_info.result_info;
                    mxb_assert(!res_storage);
                    res_storage = std::make_unique<mon_op::ResultInfo>();
                    res_storage->res = m_running_op->result();
                    res_storage->cmd_name = m_op_info.op_name;
                }
                m_op_info.exec_state = ExecState::DONE;
                m_op_info.op_name.clear();

                lock.unlock();

                if (running_cmd_is_manual)
                {
                    m_op_info.result_ready_notifier.notify_one();
                }
                m_running_op = nullptr;
            }
        }
    };

    if (m_running_op)
    {
        // An operation (user or self-triggered) is in progress, continue it.
        run_operation();
    }
    else
    {
        // No current running op, check if a command is scheduled.
        std::unique_lock<std::mutex> lock(m_op_info.lock);
        if (m_op_info.exec_state == ExecState::SCHEDULED)
        {
            // Move the scheduled command into execution.
            m_running_op = move(m_op_info.scheduled_op);
            m_op_info.exec_state = ExecState::RUNNING;
        }
        lock.unlock();

        if (m_running_op)
        {
            // An operation was just scheduled, run it.
            run_operation();
        }
    }

    // Run automatic operations. Only start an op if no op is currently running and cluster is
    // stable.
    if (!m_running_op && can_perform_cluster_ops())
    {
        if (m_settings.auto_failover)
        {
            handle_auto_failover();
        }

        // Lock status or "passive" cannot change between these functions, but operation delay or
        // modification state can.

        // Do not auto-join servers on this monitor loop if a failover (or any other cluster modification)
        // has been performed, as server states have not been updated yet. It will happen next iteration.
        if (m_settings.auto_rejoin && cluster_can_be_joined() && !cluster_operations_disabled_short())
        {
            // Check if any servers should be autojoined to the cluster and try to join them.
            handle_auto_rejoin();
        }

        /* Check if the master server is on low disk space and act on it. */
        if (m_settings.switchover_on_low_disk_space && !cluster_operations_disabled_short())
        {
            handle_low_disk_space_master();
        }

        /* Check if the master has read-only on and turn it off if user so wishes. */
        if (m_settings.enforce_writable_master && !cluster_operations_disabled_short())
        {
            enforce_writable_on_master();
        }

        /* Check if any slave servers have read-only off and turn it on if user so wishes. Again, do not
         * perform this if cluster has been modified this loop since it may not be clear which server
         * should be a slave. */
        if (m_settings.enforce_read_only_slaves && !cluster_operations_disabled_short())
        {
            enforce_read_only_on_slaves();
        }
    }

    m_state = State::MONITOR;
}

std::string MariaDBMonitor::annotate_state_change(MonitorServer* server)
{
    std::string rval;

    if (server->get_event_type() == LOST_SLAVE_EVENT)
    {
        MariaDBServer* srv = get_server(server);
        rval = srv->print_changed_slave_connections();
    }

    return rval;
}

/**
 * Save info on the master server's multimaster group, if any. This is required when checking for changes
 * in the topology.
 */
void MariaDBMonitor::update_master_cycle_info()
{
    if (m_master)
    {
        int new_cycle_id = m_master->m_node.cycle;
        m_master_cycle_status.cycle_id = new_cycle_id;
        if (new_cycle_id == NodeData::CYCLE_NONE)
        {
            m_master_cycle_status.cycle_members.clear();
        }
        else
        {
            m_master_cycle_status.cycle_members = m_cycles[new_cycle_id];
        }
    }
    else
    {
        m_master_cycle_status.cycle_id = NodeData::CYCLE_NONE;
        m_master_cycle_status.cycle_members.clear();
    }
}

void MariaDBMonitor::update_gtid_domain()
{
    int64_t old_domain = m_master_gtid_domain;
    int64_t new_domain = m_master->m_gtid_domain_id;

    if (new_domain != old_domain)
    {
        if (old_domain != GTID_DOMAIN_UNKNOWN)
        {
            MXB_NOTICE("Gtid domain id of primary has changed: %li -> %li.",
                       old_domain, new_domain);
        }
        request_journal_update();
        m_master_gtid_domain = new_domain;
    }
}

void MariaDBMonitor::assign_new_master(MariaDBServer* new_master)
{
    if (m_master != new_master)
    {
        mxb::atomic::store(&m_master, new_master, mxb::atomic::RELAXED);
        request_journal_update();
    }
    update_master_cycle_info();
    m_warn_current_master_invalid = true;
    m_warn_cannot_find_master = true;
}

/**
 * Run a manual command. It will be ran during the next monitor tick. This method waits
 * for the command to have finished running.
 *
 * @param command Function object containing the method the monitor should execute.
 * @param cmd_name Command name, for logging
 * @param error_out Json error output
 * @return True if command execution succeeded. False if monitor was in an invalid state
 * to run the command or command failed.
 */
bool MariaDBMonitor::execute_manual_command(mon_op::CmdMethod command, const string& cmd_name,
                                            json_t** error_out)
{
    bool rval = false;
    if (schedule_manual_command(std::move(command), cmd_name, error_out))
    {
        // Wait for the result.
        std::unique_lock<std::mutex> lock(m_op_info.lock);
        auto cmd_complete = [this] {
            return m_op_info.exec_state == mon_op::ExecState::DONE;
        };
        m_op_info.result_ready_notifier.wait(lock, cmd_complete);

        // Copy results similar to fetch-results.
        mon_op::Result res = m_op_info.result_info->res.deep_copy();

        // There should not be any existing errors in the error output.
        mxb_assert(*error_out == nullptr);
        rval = res.success;
        if (res.output.object_size() > 0)
        {
            *error_out = res.output.release();
        }
    }
    return rval;
}

/**
 * Schedule a manual command for execution. Does not wait for the command to complete.
 *
 * @param command Function object containing the method the monitor should execute.
 * @param cmd_name Command name, for logging
 * @param error_out Json error output
 * @return True if command execution was attempted. False if monitor was in an invalid state
 * to run the command.
 */
bool MariaDBMonitor::schedule_manual_command(mon_op::CmdMethod command, const string& cmd_name,
                                             json_t** error_out)
{
    auto op = std::make_unique<mon_op::SimpleOp>(move(command));
    return schedule_manual_command(move(op), cmd_name, error_out);
}

bool MariaDBMonitor::schedule_manual_command(mon_op::SOperation op, const std::string& cmd_name,
                                             json_t** error_out)
{
    using ExecState = mon_op::ExecState;
    mxb_assert(is_main_worker() && !cmd_name.empty());
    bool cmd_sent = false;
    if (!is_running())
    {
        PRINT_MXS_JSON_ERROR(error_out, "The monitor is not running, cannot execute manual command.");
    }
    else
    {
        const char prev_cmd[] = "Cannot run manual %s, previous %s is still %s.";
        std::lock_guard<std::mutex> guard(m_op_info.lock);
        auto op_state = m_op_info.exec_state.load(mo_acquire);
        if (op_state == ExecState::SCHEDULED)
        {
            PRINT_MXS_JSON_ERROR(error_out, prev_cmd, cmd_name.c_str(), m_op_info.op_name.c_str(), "pending");
        }
        else if (op_state == ExecState::RUNNING)
        {
            PRINT_MXS_JSON_ERROR(error_out, prev_cmd, cmd_name.c_str(), m_op_info.op_name.c_str(), "running");
        }
        else if (m_op_info.monitor_stopping)
        {
            // Should be very rare, if not impossible to get.
            PRINT_MXS_JSON_ERROR(error_out, "Cannot run manual %s, monitor is stopping.", cmd_name.c_str());
        }
        else
        {
            // Write the command. No need to notify monitor thread, as it checks for commands every tick.
            mxb_assert(!m_op_info.scheduled_op);
            m_op_info.scheduled_op = move(op);
            m_op_info.op_name = cmd_name;
            m_op_info.exec_state.store(ExecState::SCHEDULED, mo_release);
            m_op_info.current_op_is_manual = true;
            m_op_info.cancel_op = false;

            // Remove any previous results.
            m_op_info.result_info = nullptr;
            cmd_sent = true;
        }
    }

    if (cmd_sent)
    {
        request_fast_ticks();
    }
    return cmd_sent;
}

bool MariaDBMonitor::start_long_running_op(mon_op::SOperation op, const std::string& cmd_name)
{
    using ExecState = mon_op::ExecState;
    bool cmd_sent = false;
    std::lock_guard<std::mutex> lock(m_op_info.lock);
    auto op_state = m_op_info.exec_state.load(mo_acquire);
    // Can only start a long op if no op is scheduled or running.
    if (!m_op_info.monitor_stopping && op_state == ExecState::DONE)
    {
        mxb_assert(!m_op_info.scheduled_op && !m_running_op && m_op_info.op_name.empty());
        m_op_info.op_name = cmd_name;
        m_running_op = move(op);
        m_op_info.exec_state.store(ExecState::RUNNING, mo_release);
        m_op_info.current_op_is_manual = false;
        m_op_info.cancel_op = false;
        cmd_sent = true;
    }
    return cmd_sent;
}

bool MariaDBMonitor::server_locks_in_use() const
{
    return (m_settings.require_server_locks == LOCKS_MAJORITY_ALL)
           || (m_settings.require_server_locks == LOCKS_MAJORITY_RUNNING);
}

bool MariaDBMonitor::try_acquire_locks_this_tick()
{
    mxb_assert(server_locks_in_use());

    auto calc_interval = [this](int base_intervals, int deviation_max_intervals) {
        // Scale the interval calculation by the monitor interval.
        int mon_interval_ms = settings().interval.count();
        int deviation = m_random_gen.b_to_e_co(0, deviation_max_intervals);
        return (base_intervals + deviation) * mon_interval_ms;
    };

    bool try_acquire_locks = false;
    if (m_locks_info.time_to_update())
    {
        try_acquire_locks = true;
        // Calculate time until next update check. Randomize a bit to reduce possibility that multiple
        // monitors would attempt to get locks simultaneously. The randomization parameters are not user
        // configurable, but the correct values are not obvious.
        int next_check_interval = calc_interval(5, 3);
        m_locks_info.next_lock_attempt_delay = std::chrono::milliseconds(next_check_interval);
        m_locks_info.last_locking_attempt.restart();
    }
    return try_acquire_locks;
}

bool MariaDBMonitor::cluster_operations_disabled_short() const
{
    return cluster_operation_disable_timer > 0 || m_cluster_modified;
}

void MariaDBMonitor::check_acquire_masterlock()
{
    // Check that the correct server has the masterlock. If not, release and reacquire.
    // The lock status has already been fetched by the server update code.
    const MariaDBServer* masterlock_target = nullptr;
    if (m_master && m_master->is_master())
    {
        masterlock_target = m_master;
    }

    const auto ml = MariaDBServer::LockType::MASTER;
    for (auto server : m_servers)
    {
        if (server != masterlock_target)
        {
            if (server->lock_owned(ml))
            {
                // Should not have the lock, release.
                server->release_lock(ml);
            }
        }
        else if (server == masterlock_target)
        {
            auto masterlock = server->masterlock_status();
            if (masterlock.is_free())
            {
                // Don't have the lock when should.
                server->get_lock(ml);
            }
            else if (masterlock.status() == ServerLock::Status::OWNED_OTHER)
            {
                // Someone else is holding the masterlock, even when this monitor has lock majority.
                // Not a problem for this monitor, but secondary MaxScales may select a wrong master.
                MXB_ERROR("Cannot acquire lock '%s' on '%s' as it's claimed by another connection (id %li). "
                          "Secondary MaxScales may select the wrong primary.",
                          MASTER_LOCK_NAME, name(), masterlock.owner());
            }
        }
    }
}

bool MariaDBMonitor::is_slave_maxscale() const
{
    return server_locks_in_use() && !is_cluster_owner();
}

void MariaDBMonitor::execute_task_all_servers(const ServerFunction& task)
{
    execute_task_on_servers(task, m_servers);
}

void MariaDBMonitor::execute_task_on_servers(const ServerFunction& task, const ServerArray& servers)
{
    mxb::Semaphore task_complete;
    for (auto server : servers)
    {
        auto server_task = [&task, &task_complete, server]() {
            task(server);
            task_complete.post();
        };
        m_threadpool.execute(server_task, "monitor-task");
    }
    task_complete.wait_n(servers.size());
}

bool MariaDBMonitor::ClusterLocksInfo::time_to_update() const
{
    return last_locking_attempt.split() > next_lock_attempt_delay;
}

bool MariaDBMonitor::cluster_ops_configured() const
{
    return m_settings.auto_failover || m_settings.auto_rejoin
           || m_settings.enforce_read_only_slaves || m_settings.enforce_writable_master
           || m_settings.switchover_on_low_disk_space;
}

namespace journal_fields
{
const char MASTER[] = "master_server";
const char MASTER_GTID_DOMAIN[] = "master_gtid_domain";
}

void MariaDBMonitor::save_monitor_specific_journal_data(mxb::Json& data)
{
    data.set_string(journal_fields::MASTER, m_master ? m_master->name() : "");
    data.set_int(journal_fields::MASTER_GTID_DOMAIN, m_master_gtid_domain);
}

void MariaDBMonitor::load_monitor_specific_journal_data(const mxb::Json& data)
{
    string master_name = data.get_string(journal_fields::MASTER);
    for (auto* elem : m_servers)
    {
        if (strcmp(elem->name(), master_name.c_str()) == 0)
        {
            assign_new_master(elem);
            break;
        }
    }
    m_master_gtid_domain = data.get_int(journal_fields::MASTER_GTID_DOMAIN);
}

void MariaDBMonitor::flush_mdb_server_status()
{
    // Update shared status.
    bool status_changed = false;
    auto rlag_limit = m_settings.script_max_rlag;
    for (auto server : m_servers)
    {
        SERVER* srv = server->server;
        srv->set_replication_lag(server->m_replication_lag);
        if (server->flush_status())
        {
            status_changed = true;
        }

        if (rlag_limit >= 0)
        {
            server->update_rlag_state(rlag_limit);
        }
    }

    if (status_changed)
    {
        request_journal_update();
    }
}

void MariaDBMonitor::configured_servers_updated(const std::vector<SERVER*>& servers)
{
    for (auto srv : m_servers)
    {
        delete srv;
    }

    auto& shared_settings = settings().shared;
    m_servers.resize(servers.size());
    for (size_t i = 0; i < servers.size(); i++)
    {
        m_servers[i] = new MariaDBServer(servers[i], i, shared_settings, m_settings.shared);
    }

    // The configured servers and the active servers are the same.
    set_active_servers(std::vector<MonitorServer*>(m_servers.begin(), m_servers.end()));
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
            rval += string("'") + servers[i]->name() + "'";
            separator = ", ";
        }
    }
    return rval;
}

/*
 * Register module commands. Should only be called once when loading the module.
 */
void register_monitor_commands();

/**
 * The module entry point routine. This routine populates the module object structure.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    register_monitor_commands();

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::MONITOR,
        mxs::ModuleStatus::GA,
        MXS_MONITOR_VERSION,
        "A MariaDB Primary/Replica replication monitor",
        "V1.5.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<MariaDBMonitor>::s_api,
        nullptr,                                    /* Process init. */
        nullptr,                                    /* Process finish. */
        nullptr,                                    /* Thread init. */
        nullptr,                                    /* Thread finish. */
        &s_spec
    };
    return &info;
}
