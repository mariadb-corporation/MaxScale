/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
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
#include <inttypes.h>
#include <mysql.h>
#include <maxbase/assert.h>
#include <maxbase/format.hh>
#include <maxbase/alloc.h>
#include <maxscale/dcb.hh>
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
static const char CN_DETECT_STANDALONE_MASTER[] = "detect_standalone_master";
static const char CN_MAINTENANCE_ON_LOW_DISK_SPACE[] = "maintenance_on_low_disk_space";
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
const char DETECT_STALE_MASTER[] = "detect_stale_master";
const char DETECT_STALE_SLAVE[] = "detect_stale_slave";

const char failover_cmd[] = "failover";
const char switchover_cmd[] = "switchover";
const char rejoin_cmd[] = "rejoin";
const char reset_repl_cmd[] = "reset-replication";
const char release_locks_cmd[] = "release-locks";

const char CLUSTER_OP_REQUIRE_LOCKS[] = "cooperative_monitoring_locks";
const MXS_ENUM_VALUE lock_none = {"none", MariaDBMonitor::LOCKS_NONE};

const MXS_ENUM_VALUE require_lock_values[] =
{
    lock_none,
    {"majority_of_running",MariaDBMonitor::LOCKS_MAJORITY_RUNNING              },
    {"majority_of_all",MariaDBMonitor::LOCKS_MAJORITY_ALL                  },
    {nullptr}
};

const char MASTER_CONDITIONS[] = "master_conditions";
const MXS_ENUM_VALUE master_conds_def = {"primary_monitor_master", MariaDBMonitor::MCOND_COOP_M};
MXS_ENUM_VALUE master_conds_values[] =
{
    {"none",             MariaDBMonitor::MCOND_NONE        },
    {"connecting_slave", MariaDBMonitor::MCOND_CONNECTING_S},
    {"connected_slave",  MariaDBMonitor::MCOND_CONNECTED_S },
    {"running_slave",    MariaDBMonitor::MCOND_RUNNING_S   },
    master_conds_def,
    {nullptr}
};

const char SLAVE_CONDITIONS[] = "slave_conditions";
const MXS_ENUM_VALUE slave_conds_def = {"none", MariaDBMonitor::SCOND_NONE};

MXS_ENUM_VALUE slave_conds_values[] =
{
    {"linked_master",          MariaDBMonitor::SCOND_LINKED_M  },
    {"running_master",         MariaDBMonitor::SCOND_RUNNING_M },
    {"writable_master",        MariaDBMonitor::SCOND_WRITABLE_M},
    {"primary_monitor_master", MariaDBMonitor::SCOND_COOP_M    },
    slave_conds_def,
    {nullptr}
};

const char SCRIPT_MAX_RLAG[] = "script_max_replication_lag";
auto mo_relaxed = std::memory_order_relaxed;
auto mo_acquire = std::memory_order_acquire;
auto mo_release = std::memory_order_release;
}

MariaDBMonitor::MariaDBMonitor(const string& name, const string& module)
    : MonitorWorker(name, module)
{
}

mxs::MonitorServer*
MariaDBMonitor::create_server(SERVER* server, const mxs::MonitorServer::SharedSettings& shared)
{
    return new MariaDBServer(server, servers().size(), shared, m_settings.shared);
}

const ServerArray& MariaDBMonitor::servers() const
{
    return reinterpret_cast<const ServerArray&>(Monitor::servers());
}

/**
 * Reset and initialize server arrays and related data.
 */
void MariaDBMonitor::reset_server_info()
{
    m_servers_by_id.clear();
    assign_new_master(NULL);
    m_next_master = NULL;
    m_master_gtid_domain = GTID_DOMAIN_UNKNOWN;
    m_resolver = DNSResolver();     // Erases result cache.
}

void MariaDBMonitor::reset_node_index_info()
{
    for (auto server : servers())
    {
        server->m_node.reset_indexes();
    }
}

MariaDBServer* MariaDBMonitor::get_server(const EndPoint& search_ep)
{
    MariaDBServer* found = NULL;
    // Phase 1: Direct string compare
    for (auto server : servers())
    {
        EndPoint srv(server->server);
        if (srv == search_ep)
        {
            found = server;
            break;
        }
    }

    if (!found)
    {
        // Phase 2: Was not found with simple string compare. Try DNS resolving for endpoints with
        // matching ports.
        DNSResolver::StringSet target_addresses = m_resolver.resolve_server(search_ep.host());
        if (!target_addresses.empty())
        {
            for (auto server : servers())
            {
                SERVER* srv = server->server;
                if (srv->port() == search_ep.port())
                {
                    auto server_addresses = m_resolver.resolve_server(srv->address());
                    // The number of elements in the arrays is rarely over 1.
                    for (auto& address : server_addresses)
                    {
                        if (target_addresses.count(address) > 0)
                        {
                            found = server;
                            goto breakout;
                        }
                    }
                }
            }
breakout:   ;
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
    for (auto iter : servers())
    {
        if (iter->server == server)
        {
            return iter;
        }
    }
    return NULL;
}

bool MariaDBMonitor::set_replication_credentials(const mxs::ConfigParameters* params)
{
    bool repl_user_exists = params->contains(CN_REPLICATION_USER);
    bool repl_pw_exists = params->contains(CN_REPLICATION_PASSWORD);

    // Because runtime modifications are performed 1-by-1, we must be less strict here and allow
    // partial setups. Password is not required even if username is set. This is contrary to the
    // general monitor username & pw, which are both required. Even this assumes that the username
    // is given first in a "alter monitor"-command. Note: This is obsolete now that multi-parameter updates
    // are possible.

    string repl_user;
    string repl_pw;
    if (repl_user_exists)
    {
        repl_user = params->get_string(CN_REPLICATION_USER);
        if (repl_pw_exists)
        {
            // Ok, both set.
            repl_pw = params->get_string(CN_REPLICATION_PASSWORD);
        }
        // Password not set is ok. This needs to be accepted so that runtime modifications work.
        // Hopefully the password is set later on.
    }
    else
    {
        if (repl_pw_exists)
        {
            MXS_ERROR("'%s' is defined while '%s' is not. If performing an \"alter monitor\"-command, "
                      "give '%s' first.", CN_REPLICATION_PASSWORD, CN_REPLICATION_USER, CN_REPLICATION_USER);
            return false;
        }
        else
        {
            // Ok, neither is set. Use monitor credentials.
            repl_user = conn_settings().username;
            repl_pw = conn_settings().password;
        }
    }

    m_settings.shared.replication_user = repl_user;
    m_settings.shared.replication_password = mxs::decrypt_password(repl_pw);
    return true;
}

MariaDBMonitor* MariaDBMonitor::create(const string& name, const string& module)
{
    return new MariaDBMonitor(name, module);
}

/**
 * Load config parameters
 *
 * @param params Config parameters
 * @return True if settings are ok
 */
bool MariaDBMonitor::configure(const mxs::ConfigParameters* params)
{
    if (!MonitorWorker::configure(params))
    {
        return false;
    }

    m_settings.ignore_external_masters = params->get_bool("ignore_external_masters");
    m_settings.assume_unique_hostnames = params->get_bool(CN_ASSUME_UNIQUE_HOSTNAMES);
    m_settings.failcount = params->get_integer(CN_FAILCOUNT);
    m_settings.failover_timeout = params->get_duration<std::chrono::seconds>(CN_FAILOVER_TIMEOUT).count();
    m_settings.switchover_timeout = params->get_duration<std::chrono::seconds>(CN_SWITCHOVER_TIMEOUT).count();
    m_settings.auto_failover = params->get_bool(CN_AUTO_FAILOVER);
    m_settings.auto_rejoin = params->get_bool(CN_AUTO_REJOIN);
    m_settings.enforce_read_only_slaves = params->get_bool(CN_ENFORCE_READONLY);
    m_settings.enforce_simple_topology = params->get_bool(CN_ENFORCE_SIMPLE_TOPOLOGY);
    m_settings.verify_master_failure = params->get_bool(CN_VERIFY_MASTER_FAILURE);
    m_settings.master_failure_timeout =
        params->get_duration<std::chrono::seconds>(CN_MASTER_FAILURE_TIMEOUT).count();
    m_settings.shared.promotion_sql_file = params->get_string(CN_PROMOTION_SQL_FILE);
    m_settings.shared.demotion_sql_file = params->get_string(CN_DEMOTION_SQL_FILE);
    m_settings.switchover_on_low_disk_space = params->get_bool(CN_SWITCHOVER_ON_LOW_DISK_SPACE);
    m_settings.maintenance_on_low_disk_space = params->get_bool(CN_MAINTENANCE_ON_LOW_DISK_SPACE);
    m_settings.shared.handle_event_scheduler = params->get_bool(CN_HANDLE_EVENTS);
    m_settings.shared.replication_ssl = params->get_bool(CN_REPLICATION_MASTER_SSL);
    m_settings.require_server_locks =
        static_cast<RequireLocks>(params->get_enum(CLUSTER_OP_REQUIRE_LOCKS, require_lock_values));
    m_settings.shared.server_locks_enabled = (m_settings.require_server_locks != RequireLocks::LOCKS_NONE);

    m_settings.master_conds = params->get_enum(MASTER_CONDITIONS, master_conds_values);
    m_settings.slave_conds = params->get_enum(SLAVE_CONDITIONS, slave_conds_values);

    m_settings.script_max_rlag = params->get_integer(SCRIPT_MAX_RLAG);
    m_settings.excluded_servers.clear();
    /* Reset all monitored state info. The server dependent values must be reset as servers could have been
     * added, removed and modified. */
    reset_server_info();

    bool settings_ok = true;
    bool list_error = false;
    auto excluded = get_monitored_serverlist(CN_NO_PROMOTE_SERVERS, &list_error);
    if (list_error)
    {
        settings_ok = false;
    }
    else
    {
        for (auto elem : excluded)
        {
            m_settings.excluded_servers.push_back(get_server(elem));
        }
    }

    if (!check_sql_files())
    {
        settings_ok = false;
    }
    if (!set_replication_credentials(params))
    {
        settings_ok = false;
    }

    if (m_settings.enforce_simple_topology)
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

        warn_and_enable(&m_settings.assume_unique_hostnames, CN_ASSUME_UNIQUE_HOSTNAMES);
        warn_and_enable(&m_settings.auto_failover, CN_AUTO_FAILOVER);
        warn_and_enable(&m_settings.auto_rejoin, CN_AUTO_REJOIN);
    }

    if (!m_settings.assume_unique_hostnames)
    {
        const char requires[] = "%s requires that %s is on.";
        if (m_settings.auto_failover)
        {
            MXB_ERROR(requires, CN_AUTO_FAILOVER, CN_ASSUME_UNIQUE_HOSTNAMES);
            settings_ok = false;
        }
        if (m_settings.switchover_on_low_disk_space)
        {
            MXB_ERROR(requires, CN_SWITCHOVER_ON_LOW_DISK_SPACE, CN_ASSUME_UNIQUE_HOSTNAMES);
            settings_ok = false;
        }
        if (m_settings.auto_rejoin)
        {
            MXB_ERROR(requires, CN_AUTO_REJOIN, CN_ASSUME_UNIQUE_HOSTNAMES);
            settings_ok = false;
        }
    }

    // Check if conflicting settings are in use.
    auto check_if_both_set = [&params, &settings_ok](bool s1_modified, const string& s1, const string& s2) {
            if (params->contains(s2))
            {
                if (s1_modified)
                {
                    MXB_ERROR("'%s' and '%s' cannot both be defined.", s1.c_str(), s2.c_str());
                    settings_ok = false;
                }
                else
                {
                    MXB_WARNING("'%s' is deprecated and should not be used. Use '%s' instead.",
                                s2.c_str(), s1.c_str());
                }
            }
        };

    bool master_conds_modified = (m_settings.master_conds != (int64_t)master_conds_def.enum_value);
    check_if_both_set(master_conds_modified, MASTER_CONDITIONS, CN_DETECT_STANDALONE_MASTER);
    check_if_both_set(master_conds_modified, MASTER_CONDITIONS, DETECT_STALE_MASTER);
    bool slave_conds_modified = (m_settings.slave_conds != (int64_t)slave_conds_def.enum_value);
    check_if_both_set(slave_conds_modified, SLAVE_CONDITIONS, DETECT_STALE_SLAVE);

    if (settings_ok)
    {
        // If any of the settings 'detect_standalone_master' or 'detect_stale_master/slave' are set,
        // add the equivalent master/slave conditions.
        if (params->contains(CN_DETECT_STANDALONE_MASTER)
            && params->get_bool(CN_DETECT_STANDALONE_MASTER) == false)
        {
            m_settings.master_conds |= MasterConds::MCOND_CONNECTING_S;
        }
        if (params->contains(DETECT_STALE_MASTER) && params->get_bool(DETECT_STALE_MASTER) == false)
        {
            m_settings.master_conds |= MasterConds::MCOND_RUNNING_S;
        }
        if (params->contains(DETECT_STALE_SLAVE) && params->get_bool(DETECT_STALE_SLAVE) == false)
        {
            m_settings.slave_conds |= SlaveConds::SCOND_LINKED_M;
        }
    }
    return settings_ok;
}

json_t* MariaDBMonitor::diagnostics() const
{
    mxb_assert(mxs::MainWorker::is_main_worker());
    return to_json();
}

json_t* MariaDBMonitor::diagnostics(MonitorServer* srv) const
{
    mxb_assert(mxs::MainWorker::is_main_worker());
    json_t* result = nullptr;

    if (auto server = get_server(srv))
    {
        result = server->to_json();

        json_object_set_new(result, "state_details",
                            !m_settings.ignore_external_masters && !server->m_node.external_masters.empty() ?
                            json_string("Slave of External Server") : json_null());
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
        return json_string("Demoting old master");

    case State::WAIT_FOR_TARGET_CATCHUP:
        return json_string("Waiting for candidate master to catch up");

    case State::PROMOTE_TARGET:
        return json_string("Promoting candidate master");

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
    json_t* rval = MonitorWorker::diagnostics();

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
    for (MariaDBServer* server : servers())
    {
        json_array_append_new(server_info, server->to_json());
    }
    json_object_set_new(rval, "server_info", server_info);
    return rval;
}

bool MariaDBMonitor::can_be_disabled(const mxs::MonitorServer& mserver, std::string* errmsg_out) const
{
    // If the server is the master, it cannot be disabled.
    bool can_be = !status_is_master(mserver.server->status());

    if (!can_be)
    {
        *errmsg_out =
            "The server is master, so it cannot be set in maintenance or draining mode. "
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
    // Write the corresponding MariaDBServer into the class-specific m_master variable.
    MonitorServer* journal_master = nullptr;
    read_journal();
    if (journal_master)
    {
        // This is somewhat questionable, as the journal only contains status bits but no actual topology
        // info. In a fringe case the actual queried topology may not match the journal data, freezing the
        // master to a suboptimal choice.
        assign_new_master(get_server(journal_master));
    }

    /* This loop can be removed if/once the replication check code is inside tick. It's required so that
     * the monitor makes new connections when starting. */
    for (MariaDBServer* server : servers())
    {
        if (server->con)
        {
            mysql_close(server->con);
            server->con = NULL;
        }
    }

    m_locks_info.reset();
}

void MariaDBMonitor::post_loop()
{
    write_journal();
}

void MariaDBMonitor::tick()
{
    m_state = State::MONITOR;
    check_maintenance_requests();

    /* Update MonitorServer->pending_status. This is where the monitor loop writes it's findings.
     * Also, backup current status so that it can be compared to any deduced state. */
    for (auto srv : servers())
    {
        auto status = srv->server->status();
        srv->pending_status = status;
        srv->mon_prev_status = status;
    }

    // Query all servers for their status.
    bool should_update_disk_space = check_disk_space_this_tick();

    // Concurrently query all servers for their status.
    auto update_task = [should_update_disk_space](MariaDBServer* server) {
            server->update_server(should_update_disk_space);
        };
    execute_task_all_servers(update_task);

    update_cluster_lock_status();

    for (MariaDBServer* server : servers())
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

    if (m_master != NULL && m_master->is_master())
    {
        // Update cluster-wide values dependant on the current master.
        update_gtid_domain();
    }

    /* Set low disk space slaves to maintenance. This needs to happen after roles have been assigned.
     * Is not a real cluster operation, since nothing on the actual backends is changed. */
    if (m_settings.maintenance_on_low_disk_space)
    {
        set_low_disk_slaves_maintenance();
    }

    // Sanity check. Master may not be both slave and master.
    mxb_assert(m_master == NULL || !m_master->has_status(SERVER_SLAVE | SERVER_MASTER));

    if (server_locks_in_use() && is_cluster_owner())
    {
        check_acquire_masterlock();
    }

    flush_server_status();
    process_state_changes();
    hangup_failed_servers();
    write_journal_if_needed();
    m_state = State::IDLE;
}

void MariaDBMonitor::process_state_changes()
{
    using ExecState = ManualCommand::ExecState;
    m_state = State::EXECUTE_SCRIPTS;
    MonitorWorker::process_state_changes();

    m_cluster_modified = false;
    if (cluster_operation_disable_timer > 0)
    {
        cluster_operation_disable_timer--;
    }

    // Check for manual commands
    if (m_manual_cmd.exec_state.load(mo_acquire) == ExecState::SCHEDULED)
    {
        // Looks like a command is waiting. Lock mutex, check again.
        bool scheduled = false;
        ManualCommand::CmdMethod command_method;

        std::unique_lock<std::mutex> lock(m_manual_cmd.lock);
        scheduled = m_manual_cmd.exec_state.load(mo_acquire) == ExecState::SCHEDULED;
        if (scheduled)
        {
            command_method = std::move(m_manual_cmd.method);
            m_manual_cmd.exec_state.store(ExecState::RUNNING, mo_release);
        }
        lock.unlock();

        // Run the command outside the lock. Enables checking result state while command is running.
        if (scheduled)
        {
            auto cmd_result = command_method();
            // Manual command ran, signal the sender to continue.
            lock.lock();
            m_manual_cmd.exec_state.store(ExecState::DONE, mo_release);
            // Remove any previous saved errors and replace with new ones.
            json_decref(m_manual_cmd.cmd_result.errors);
            m_manual_cmd.cmd_result = cmd_result;
            lock.unlock();
            m_manual_cmd.cmd_complete_notifier.notify_one();
        }
    }

    if (m_settings.auto_failover)
    {
        handle_auto_failover();
    }

    // Do not auto-join servers on this monitor loop if a failover (or any other cluster modification)
    // has been performed, as server states have not been updated yet. It will happen next iteration.
    if (m_settings.auto_rejoin && cluster_can_be_joined() && can_perform_cluster_ops())
    {
        // Check if any servers should be autojoined to the cluster and try to join them.
        handle_auto_rejoin();
    }

    /* Check if any slave servers have read-only off and turn it on if user so wishes. Again, do not
     * perform this if cluster has been modified this loop since it may not be clear which server
     * should be a slave. */
    if (m_settings.enforce_read_only_slaves && can_perform_cluster_ops())
    {
        enforce_read_only_on_slaves();
    }

    /* Check if the master server is on low disk space and act on it. */
    if (m_settings.switchover_on_low_disk_space && can_perform_cluster_ops())
    {
        handle_low_disk_space_master();
    }

    m_state = State::MONITOR;
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
            MXS_NOTICE("Gtid domain id of master has changed: %li -> %li.",
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
 * Check sql text file parameters. A parameter should either be empty or a valid file which can be opened.
 *
 * @return True if no errors occurred when opening the files
 */
bool MariaDBMonitor::check_sql_files()
{
    const char ERRMSG[] = "%s ('%s') does not exist or cannot be accessed for reading: '%s'.";

    bool rval = true;
    auto prom_file = m_settings.shared.promotion_sql_file;
    if (!prom_file.empty() && access(prom_file.c_str(), R_OK) != 0)
    {
        rval = false;
        MXS_ERROR(ERRMSG, CN_PROMOTION_SQL_FILE, prom_file.c_str(), mxs_strerror(errno));
    }

    auto dem_file = m_settings.shared.demotion_sql_file;
    if (!dem_file.empty() && access(dem_file.c_str(), R_OK) != 0)
    {
        rval = false;
        MXS_ERROR(ERRMSG, CN_DEMOTION_SQL_FILE, dem_file.c_str(), mxs_strerror(errno));
    }
    return rval;
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
bool MariaDBMonitor::execute_manual_command(ManualCommand::CmdMethod command, const string& cmd_name,
                                            json_t** error_out)
{
    bool rval = false;
    if (schedule_manual_command(std::move(command), cmd_name, error_out))
    {
        // Wait for the result.
        std::unique_lock<std::mutex> lock(m_manual_cmd.lock);
        auto cmd_complete = [this] {
                return m_manual_cmd.exec_state == ManualCommand::ExecState::DONE;
            };
        m_manual_cmd.cmd_complete_notifier.wait(lock, cmd_complete);

        // Copy results similar to fetch-results.
        ManualCommand::Result res;
        res.deep_copy_from(m_manual_cmd.cmd_result);

        // There should not be any existing errors in the error output.
        mxb_assert(*error_out == nullptr);
        rval = res.success;
        *error_out = res.errors;
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
bool MariaDBMonitor::schedule_manual_command(ManualCommand::CmdMethod command, const string& cmd_name,
                                             json_t** error_out)
{
    mxb_assert(is_main_worker());
    using ExecState = ManualCommand::ExecState;
    bool cmd_sent = false;
    if (!is_running())
    {
        PRINT_MXS_JSON_ERROR(error_out, "The monitor is not running, cannot execute manual command.");
    }
    else
    {
        auto seen_state = ExecState::NONE;
        string current_cmd_name;
        // The lock is not essential, but does remove dependency on variable write order.
        std::unique_lock<std::mutex> lock(m_manual_cmd.lock);
        seen_state = m_manual_cmd.exec_state.load(mo_acquire);
        if (seen_state == ExecState::NONE || seen_state == ExecState::DONE)
        {
            // Write the command. No need to signal monitor thread, as it checks for commands every loop.
            m_manual_cmd.method = std::move(command);
            m_manual_cmd.cmd_name = cmd_name;
            m_manual_cmd.exec_state.store(ExecState::SCHEDULED, mo_release);
            cmd_sent = true;
        }
        else
        {
            current_cmd_name = m_manual_cmd.cmd_name;
        }
        lock.unlock();

        if (!cmd_sent)
        {
            auto seen_state_str = (seen_state == ExecState::SCHEDULED) ? "pending" : "running";
            PRINT_MXS_JSON_ERROR(error_out, "Cannot run manual %s, previous %s is still %s.",
                                 cmd_name.c_str(), current_cmd_name.c_str(), seen_state_str);
        }
    }
    return cmd_sent;
}

bool MariaDBMonitor::immediate_tick_required() const
{
    return (m_manual_cmd.exec_state.load(mo_relaxed) == ManualCommand::ExecState::SCHEDULED)
           || m_cluster_modified;
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
            int mon_interval_ms = settings().interval;
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

bool MariaDBMonitor::run_manual_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out)
{
    auto func = [this, new_master, current_master]() {
            return manual_switchover(new_master, current_master);
        };
    return execute_manual_command(func, switchover_cmd, error_out);
}

bool MariaDBMonitor::schedule_async_switchover(SERVER* new_master, SERVER* current_master, json_t** error_out)
{
    auto func = [this, new_master, current_master]() {
            return manual_switchover(new_master, current_master);
        };
    return schedule_manual_command(func, switchover_cmd, error_out);
}

bool MariaDBMonitor::run_manual_failover(json_t** error_out)
{
    auto func = [this]() {
            return manual_failover();
        };
    return execute_manual_command(func, failover_cmd, error_out);
}

bool MariaDBMonitor::run_manual_rejoin(SERVER* rejoin_server, json_t** error_out)
{
    auto func = [this, rejoin_server]() {
            return manual_rejoin(rejoin_server);
        };
    return execute_manual_command(func, rejoin_cmd, error_out);
}

bool MariaDBMonitor::run_manual_reset_replication(SERVER* master_server, json_t** error_out)
{
    auto func = [this, master_server]() {
            return manual_reset_replication(master_server);
        };
    return execute_manual_command(func, reset_repl_cmd, error_out);
}

bool MariaDBMonitor::run_release_locks(json_t** error_out)
{
    auto func = [this]() {
            return manual_release_locks();
        };
    return execute_manual_command(func, release_locks_cmd, error_out);
}

bool MariaDBMonitor::fetch_cmd_result(json_t** output)
{
    using ExecState = ManualCommand::ExecState;
    auto current_state = ExecState::NONE;
    string current_cmd_name;
    ManualCommand::Result cmd_result;

    std::unique_lock<std::mutex> lock(m_manual_cmd.lock);
    // Copy the manual command related fields to local variables under the lock.
    current_state = m_manual_cmd.exec_state.load(mo_acquire);
    if (current_state != ExecState::NONE)
    {
        current_cmd_name = m_manual_cmd.cmd_name;
        if (current_state == ExecState::DONE)
        {
            // Deep copy the json, as another manual command may start writing to the container
            // right after mutex is released.
            cmd_result.deep_copy_from(m_manual_cmd.cmd_result);
        }
    }
    lock.unlock();

    // The string contents here must match with GUI code.
    const char cmd_running_fmt[] = "No manual command results are available, %s is still %s.";
    switch (current_state)
    {
    case ExecState::NONE:
        // Command has not been ran.
        *output = mxs_json_error_append(*output, "No manual command results are available.");
        break;

    case ExecState::SCHEDULED:
        *output = mxs_json_error_append(*output, cmd_running_fmt, current_cmd_name.c_str(), "pending");
        break;

    case ExecState::RUNNING:
        *output = mxs_json_error_append(*output, cmd_running_fmt, current_cmd_name.c_str(), "running");
        break;

    case ExecState::DONE:
        if (cmd_result.success)
        {
            *output = json_sprintf("%s completed successfully.", current_cmd_name.c_str());
        }
        else if (cmd_result.errors)
        {
            *output = cmd_result.errors;
        }
        else
        {
            // Command failed, but printed no results.
            *output = json_sprintf("%s failed.", current_cmd_name.c_str());
        }
        break;
    }
    return true;
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
    for (auto server : servers())
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
                          "Secondary MaxScales may select the wrong master.",
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
    execute_task_on_servers(task, servers());
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
        m_threadpool.execute(server_task);
    }
    task_complete.wait_n(servers.size());
}

MariaDBMonitor::ManualCommand::Result MariaDBMonitor::manual_release_locks()
{
    // Manual commands should only run in the main monitor thread.
    mxb_assert(mxb::Worker::get_current()->id() == this->id());
    mxb_assert(m_manual_cmd.exec_state == ManualCommand::ExecState::RUNNING);

    ManualCommand::Result rval;
    auto error_out = &rval.errors;

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
            MXS_NOTICE("Released %i lock(s). %s", released, LOCK_DELAY_MSG);
            success = true;
        }
        else
        {
            PRINT_MXS_JSON_ERROR(error_out, "Did not release any locks. %s", LOCK_DELAY_MSG);
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(error_out, "Server locks are not in use, cannot release them.");
    }
    rval.success = success;
    return rval;
}

bool MariaDBMonitor::ClusterLocksInfo::time_to_update() const
{
    return last_locking_attempt.split() > next_lock_attempt_delay;
}

MariaDBMonitor::~MariaDBMonitor()
{
    json_decref(m_manual_cmd.cmd_result.errors);
}

void MariaDBMonitor::ManualCommand::Result::deep_copy_from(const MariaDBMonitor::ManualCommand::Result& rhs)
{
    // If command succeeded, errors should be empty.
    mxb_assert(!(rhs.success && rhs.errors));
    success = rhs.success;
    errors = json_deep_copy(rhs.errors);
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
    for (auto& elem : m_servers_by_id)
    {
        if (strcmp(elem.second->name(), master_name.c_str()) == 0)
        {
            assign_new_master(elem.second);
            break;
        }
    }
    m_master_gtid_domain = data.get_int(journal_fields::MASTER_GTID_DOMAIN);
}

void MariaDBMonitor::flush_server_status()
{
    // Update shared status.
    bool status_changed = false;
    auto rlag_limit = m_settings.script_max_rlag;
    for (auto server : servers())
    {
        SERVER* srv = server->server;
        srv->set_replication_lag(server->m_replication_lag);
        if (server->pending_status != srv->status())
        {
            status_changed = true;
            srv->assign_status(server->pending_status);
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

/**
 * Command handler for 'switchover'
 *
 * @param args    The provided arguments.
 * @param output  Pointer where to place output object.
 *
 * @return True, if the command was executed, false otherwise.
 */
bool handle_manual_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    mxb_assert((args->argc >= 1) && (args->argc <= 3));
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert((args->argc < 2) || (MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER));
    mxb_assert((args->argc < 3) || (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER));

    bool rval = false;
    if (mxs::Config::get().passive.get())
    {
        const char* const MSG = "Switchover requested but not performed, as MaxScale is in passive mode.";
        PRINT_MXS_JSON_ERROR(error_out, MSG);
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon);
        SERVER* promotion_server = (args->argc >= 2) ? args->argv[1].value.server : NULL;
        SERVER* demotion_server = (args->argc == 3) ? args->argv[2].value.server : NULL;
        rval = handle->run_manual_switchover(promotion_server, demotion_server, error_out);
    }
    return rval;
}

/**
 * Command handler for 'async-switchover'
 *
 * @param args    The provided arguments.
 * @param output  Pointer where to place output object.
 *
 * @return True, if the command was executed, false otherwise.
 */
bool handle_async_switchover(const MODULECMD_ARG* args, json_t** error_out)
{
    mxb_assert((args->argc >= 1) && (args->argc <= 3));
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert((args->argc < 2) || (MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER));
    mxb_assert((args->argc < 3) || (MODULECMD_GET_TYPE(&args->argv[2].type) == MODULECMD_ARG_SERVER));

    bool rval = false;
    if (mxs::Config::get().passive.get())
    {
        const char* const MSG = "Switchover requested but not performed, as MaxScale is in passive mode.";
        PRINT_MXS_JSON_ERROR(error_out, MSG);
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon);
        SERVER* promotion_server = (args->argc >= 2) ? args->argv[1].value.server : NULL;
        SERVER* demotion_server = (args->argc == 3) ? args->argv[2].value.server : NULL;
        rval = handle->schedule_async_switchover(promotion_server, demotion_server, error_out);
    }
    return rval;
}

/**
 * Command handler for 'failover'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool handle_manual_failover(const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    bool rv = false;

    if (mxs::Config::get().passive.get())
    {
        PRINT_MXS_JSON_ERROR(output, "Failover requested but not performed, as MaxScale is in passive mode.");
    }
    else
    {
        Monitor* mon = args->argv[0].value.monitor;
        auto handle = static_cast<MariaDBMonitor*>(mon);
        rv = handle->run_manual_failover(output);
    }
    return rv;
}

/**
 * Command handler for 'rejoin'
 *
 * @param args Arguments given by user
 * @param output Json error output
 * @return True on success
 */
bool handle_manual_rejoin(const MODULECMD_ARG* args, json_t** output)
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
        rv = handle->run_manual_rejoin(server, output);
    }
    return rv;
}

bool handle_manual_reset_replication(const MODULECMD_ARG* args, json_t** output)
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
        rv = handle->run_manual_reset_replication(server, output);
    }
    return rv;
}

bool handle_release_locks(const MODULECMD_ARG* args, json_t** output)
{
    mxb_assert(args->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    Monitor* mon = args->argv[0].value.monitor;
    auto mariamon = static_cast<MariaDBMonitor*>(mon);
    return mariamon->run_release_locks(output);
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

/**
 * The module entry point routine. This routine populates the module object structure.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char ARG_MONITOR_DESC[] = "Monitor name";
    static modulecmd_arg_type_t switchover_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC           },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "New master (optional)"    },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Current master (optional)"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "switchover", MODULECMD_TYPE_ACTIVE,
                               handle_manual_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Perform master switchover");

    modulecmd_register_command(MXS_MODULE_NAME, "async-switchover", MODULECMD_TYPE_ACTIVE,
                               handle_async_switchover, MXS_ARRAY_NELEMS(switchover_argv), switchover_argv,
                               "Schedule master switchover without waiting for completion");

    static modulecmd_arg_type_t failover_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXS_MODULE_NAME, failover_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_failover, MXS_ARRAY_NELEMS(failover_argv), failover_argv,
                               "Perform master failover");

    static modulecmd_arg_type_t rejoin_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
        {MODULECMD_ARG_SERVER,                                      "Joining server"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, rejoin_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_rejoin, MXS_ARRAY_NELEMS(rejoin_argv), rejoin_argv,
                               "Rejoin server to a cluster");

    static modulecmd_arg_type_t reset_gtid_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC          },
        {MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL,             "Master server (optional)"}
    };

    modulecmd_register_command(MXS_MODULE_NAME, reset_repl_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_manual_reset_replication,
                               MXS_ARRAY_NELEMS(reset_gtid_argv), reset_gtid_argv,
                               "Delete slave connections, delete binary logs and "
                               "set up replication (dangerous)");

    static modulecmd_arg_type_t release_locks_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    modulecmd_register_command(MXS_MODULE_NAME, release_locks_cmd, MODULECMD_TYPE_ACTIVE,
                               handle_release_locks,
                               MXS_ARRAY_NELEMS(release_locks_argv), release_locks_argv,
                               "Release any held server locks for 1 minute.");

    static modulecmd_arg_type_t fetch_cmd_result_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC},
    };

    static const char fetch_desc[] = "Fetch result of the last scheduled command.";
    modulecmd_register_command(MXS_MODULE_NAME, "fetch-cmd-results", MODULECMD_TYPE_PASSIVE,
                               handle_fetch_cmd_result,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               fetch_desc);

    modulecmd_register_command(MXS_MODULE_NAME, "fetch-cmd-result", MODULECMD_TYPE_PASSIVE,
                               handle_fetch_cmd_result,
                               MXS_ARRAY_NELEMS(fetch_cmd_result_argv), fetch_cmd_result_argv,
                               fetch_desc);

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::MONITOR,
        mxs::ModuleStatus::GA,
        MXS_MONITOR_VERSION,
        "A MariaDB Master/Slave replication monitor",
        "V1.5.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<MariaDBMonitor>::s_api,
        NULL,                                       /* Process init. */
        NULL,                                       /* Process finish. */
        NULL,                                       /* Thread init. */
        NULL,                                       /* Thread finish. */
        {
            {
                "detect_replication_lag",            MXS_MODULE_PARAM_BOOL,      "false",
                MXS_MODULE_OPT_DEPRECATED
            },
            {
                DETECT_STALE_MASTER,                 MXS_MODULE_PARAM_BOOL,      nullptr
            },
            {
                DETECT_STALE_SLAVE,                  MXS_MODULE_PARAM_BOOL,      nullptr
            },
            {
                CN_DETECT_STANDALONE_MASTER,         MXS_MODULE_PARAM_BOOL,      nullptr
            },
            {
                CN_FAILCOUNT,                        MXS_MODULE_PARAM_COUNT,     "5"
            },
            {
                "ignore_external_masters",           MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_AUTO_FAILOVER,                    MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_FAILOVER_TIMEOUT,                 MXS_MODULE_PARAM_DURATION,  "90s",
                MXS_MODULE_OPT_DURATION_S
            },
            {
                CN_SWITCHOVER_TIMEOUT,               MXS_MODULE_PARAM_DURATION,  "90s",
                MXS_MODULE_OPT_DURATION_S
            },
            {
                CN_REPLICATION_USER,                 MXS_MODULE_PARAM_STRING
            },
            {
                CN_REPLICATION_PASSWORD,             MXS_MODULE_PARAM_PASSWORD
            },
            {
                CN_REPLICATION_MASTER_SSL,           MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_VERIFY_MASTER_FAILURE,            MXS_MODULE_PARAM_BOOL,      "true"
            },
            {
                CN_MASTER_FAILURE_TIMEOUT,           MXS_MODULE_PARAM_DURATION,  "10s",
                MXS_MODULE_OPT_DURATION_S
            },
            {
                CN_AUTO_REJOIN,                      MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_ENFORCE_READONLY,                 MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_NO_PROMOTE_SERVERS,               MXS_MODULE_PARAM_SERVERLIST
            },
            {
                CN_PROMOTION_SQL_FILE,               MXS_MODULE_PARAM_PATH
            },
            {
                CN_DEMOTION_SQL_FILE,                MXS_MODULE_PARAM_PATH
            },
            {
                CN_SWITCHOVER_ON_LOW_DISK_SPACE,     MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CN_MAINTENANCE_ON_LOW_DISK_SPACE,    MXS_MODULE_PARAM_BOOL,      "true"
            },
            {
                CN_HANDLE_EVENTS,                    MXS_MODULE_PARAM_BOOL,      "true"
            },
            {
                CN_ASSUME_UNIQUE_HOSTNAMES,          MXS_MODULE_PARAM_BOOL,      "true"
            },
            {
                CN_ENFORCE_SIMPLE_TOPOLOGY,          MXS_MODULE_PARAM_BOOL,      "false"
            },
            {
                CLUSTER_OP_REQUIRE_LOCKS,            MXS_MODULE_PARAM_ENUM,      lock_none.name,
                MXS_MODULE_OPT_ENUM_UNIQUE,          require_lock_values
            },
            {
                MASTER_CONDITIONS,                   MXS_MODULE_PARAM_ENUM,      master_conds_def.name,
                MXS_MODULE_OPT_NONE,                 master_conds_values
            },
            {
                SLAVE_CONDITIONS,                    MXS_MODULE_PARAM_ENUM,      slave_conds_def.name,
                MXS_MODULE_OPT_NONE,                 slave_conds_values
            },
            {
                // TODO: May change to duration type once -1 accepted.
                SCRIPT_MAX_RLAG,                     MXS_MODULE_PARAM_INT,       "-1"
            },
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &info;
}
