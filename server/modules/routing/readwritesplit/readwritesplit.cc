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

#include "readwritesplit.hh"
#include "rwsplitsession.hh"
#include <maxscale/service.hh>

using namespace maxscale;

namespace
{
const char* CN_SESSION_TRACK_SYSTEM_VARIABLES = "session_track_system_variables";

config::Specification s_spec(MXB_MODULE_NAME, config::Specification::ROUTER);

config::ParamEnum<mxs_target_t> s_use_sql_variables_in(
    &s_spec, "use_sql_variables_in",
    "Whether to route SQL variable modifications to all servers or only to the master",
    {
        {TYPE_ALL, "all"},
        {TYPE_MASTER, "master"},
    }, TYPE_ALL, config::Param::AT_RUNTIME);

config::ParamEnum<select_criteria_t> s_slave_selection_criteria(
    &s_spec, "slave_selection_criteria", "Slave selection criteria",
    {
        {LEAST_GLOBAL_CONNECTIONS, "least_global_connections"},
        {LEAST_ROUTER_CONNECTIONS, "least_router_connections"},
        {LEAST_BEHIND_MASTER, "least_behind_master"},
        {LEAST_CURRENT_OPERATIONS, "least_current_operations"},
        {ADAPTIVE_ROUTING, "adaptive_routing"},
        {LEAST_GLOBAL_CONNECTIONS, "LEAST_GLOBAL_CONNECTIONS"},
        {LEAST_ROUTER_CONNECTIONS, "LEAST_ROUTER_CONNECTIONS"},
        {LEAST_BEHIND_MASTER, "LEAST_BEHIND_MASTER"},
        {LEAST_CURRENT_OPERATIONS, "LEAST_CURRENT_OPERATIONS"},
        {ADAPTIVE_ROUTING, "ADAPTIVE_ROUTING"},
    }, LEAST_CURRENT_OPERATIONS, config::Param::AT_RUNTIME);

config::ParamEnum<failure_mode> s_master_failure_mode(
    &s_spec, "master_failure_mode", "Master failure mode behavior",
    {
        {RW_FAIL_INSTANTLY, "fail_instantly"},
        {RW_FAIL_ON_WRITE, "fail_on_write"},
        {RW_ERROR_ON_WRITE, "error_on_write"}
    }, RW_FAIL_ON_WRITE, config::Param::AT_RUNTIME);

config::ParamEnum<CausalReads> s_causal_reads(
    &s_spec, "causal_reads", "Causal reads mode",
    {
        {CausalReads::NONE, "none"},
        {CausalReads::LOCAL, "local"},
        {CausalReads::GLOBAL, "global"},
        {CausalReads::FAST_GLOBAL, "fast_global"},
        {CausalReads::FAST, "fast"},
        {CausalReads::UNIVERSAL, "universal"},
        {CausalReads::FAST_UNIVERSAL, "fast_universal"},

        // Legacy values for causal_reads
        {CausalReads::NONE, "false"},
        {CausalReads::NONE, "off"},
        {CausalReads::NONE, "0"},
        {CausalReads::LOCAL, "true"},
        {CausalReads::LOCAL, "on"},
        {CausalReads::LOCAL, "1"},
    }, CausalReads::NONE, config::Param::AT_RUNTIME);

config::ParamSeconds s_max_replication_lag(
    &s_spec, "max_replication_lag", "Maximum replication lag",
    std::chrono::seconds(0),
    config::Param::AT_RUNTIME);

config::ParamDeprecated<config::ParamAlias> s_max_slave_replication_lag(
    &s_spec, "max_slave_replication_lag", &s_max_replication_lag);

config::ParamCount s_max_slave_connections(
    &s_spec, "max_slave_connections", "Maximum number of slave connections",
    SLAVE_MAX, config::Param::AT_RUNTIME);

config::ParamCount s_slave_connections(
    &s_spec, "slave_connections", "Starting number of slave connections",
    SLAVE_MAX, config::Param::AT_RUNTIME);

config::ParamBool s_retry_failed_reads(
    &s_spec, "retry_failed_reads", "Automatically retry failed reads outside of transactions",
    true, config::Param::AT_RUNTIME);

config::ParamBool s_strict_multi_stmt(
    &s_spec, "strict_multi_stmt", "Lock connection to master after multi-statement query",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_strict_sp_calls(
    &s_spec, "strict_sp_calls", "Lock connection to master after a stored procedure is executed",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_strict_tmp_tables(
    &s_spec, "strict_tmp_tables", "Prevent reconnections if temporary tables exist",
    true, config::Param::AT_RUNTIME);

config::ParamBool s_master_accept_reads(
    &s_spec, "master_accept_reads", "Use master for reads",
    false, config::Param::AT_RUNTIME);

config::ParamSeconds s_causal_reads_timeout(
    &s_spec, "causal_reads_timeout", "Timeout for the slave synchronization",
    10s, config::Param::AT_RUNTIME);

config::ParamBool s_master_reconnection(
    &s_spec, "master_reconnection", "Reconnect to master",
    true, config::Param::AT_RUNTIME);

config::ParamBool s_delayed_retry(
    &s_spec, "delayed_retry", "Retry failed writes outside of transactions",
    false, config::Param::AT_RUNTIME);

config::ParamSeconds s_delayed_retry_timeout(
    &s_spec, "delayed_retry_timeout", "Timeout for delayed_retry",
    10s, config::Param::AT_RUNTIME);

config::ParamBool s_transaction_replay(
    &s_spec, "transaction_replay", "Retry failed transactions",
    false, config::Param::AT_RUNTIME);

config::ParamSize s_transaction_replay_max_size(
    &s_spec, "transaction_replay_max_size", "Maximum size of transaction to retry",
    1024 * 1024, config::Param::AT_RUNTIME);

config::ParamSeconds s_transaction_replay_timeout(
    &s_spec, "transaction_replay_timeout", "Timeout for transaction replay",
    30s, config::Param::AT_RUNTIME);

config::ParamCount s_transaction_replay_attempts(
    &s_spec, "transaction_replay_attempts", "Maximum number of times to retry a transaction",
    5, config::Param::AT_RUNTIME);

config::ParamBool s_transaction_replay_retry_on_deadlock(
    &s_spec, "transaction_replay_retry_on_deadlock", "Retry transaction on deadlock",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_transaction_replay_retry_on_mismatch(
    &s_spec, "transaction_replay_retry_on_mismatch", "Retry transaction on checksum mismatch",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_transaction_replay_safe_commit(
    &s_spec, "transaction_replay_safe_commit", "Prevent replaying of about-to-commit transaction",
    true, config::Param::AT_RUNTIME);

config::ParamEnum<TrxChecksum> s_transaction_replay_checksum(
    &s_spec, "transaction_replay_checksum", "Type of checksum to calculate for results",
    {
        {TrxChecksum::FULL, "full"},
        {TrxChecksum::RESULT_ONLY, "result_only"},
        {TrxChecksum::NO_INSERT_ID, "no_insert_id"},
    }, TrxChecksum::FULL, config::Param::AT_RUNTIME);

config::ParamBool s_optimistic_trx(
    &s_spec, "optimistic_trx", "Optimistically offload transactions to slaves",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_lazy_connect(
    &s_spec, "lazy_connect", "Create connections only when needed",
    false, config::Param::AT_RUNTIME);

config::ParamBool s_reuse_ps(
    &s_spec, "reuse_prepared_statements", "Reuse identical prepared statements inside the same connection",
    false, config::Param::AT_RUNTIME);
}

/**
 * The entry points for the read/write query splitting router module.
 *
 * This file contains the entry points that comprise the API to the read
 * write query splitting router. It also contains functions that are
 * directly called by the entry point functions. Some of these are used by
 * functions in other modules of the read write split router, others are
 * used only within this module.
 */

/** Maximum number of slaves */
#define MAX_SLAVE_COUNT "255"

bool RWSplit::check_causal_reads(SERVER* server) const
{
    auto var = server->get_variable_value(CN_SESSION_TRACK_SYSTEM_VARIABLES);
    return var.empty() || var == "*" || var.find("last_gtid") != std::string::npos;
}

RWSplit::RWSplit(SERVICE* service)
    : m_service(service)
    , m_config(service)
{
}

RWSplit::~RWSplit()
{
}

SERVICE* RWSplit::service() const
{
    return m_service;
}

const mxs::WorkerGlobal<RWSConfig::Values>& RWSplit::config() const
{
    return m_config.values();
}

Stats& RWSplit::stats()
{
    return m_stats;
}

const Stats& RWSplit::stats() const
{
    return m_stats;
}

TargetSessionStats& RWSplit::local_server_stats()
{
    return *m_server_stats;
}

maxscale::TargetSessionStats RWSplit::all_server_stats() const
{
    TargetSessionStats stats;
    auto children = m_service->get_children();

    for (const auto& a : m_server_stats.collect_values())
    {
        for (const auto& b : a)
        {
            auto it = std::find(children.begin(), children.end(), b.first);

            if (it != children.end() && b.first->active())
            {
                // The target is still alive and a part of this service.
                stats[b.first] += b.second;
            }
        }
    }

    return stats;
}

std::string RWSplit::last_gtid() const
{
    std::shared_lock<mxb::shared_mutex> guard(m_last_gtid_lock);
    std::string gtid;
    std::string separator = "";

    for (const auto& g : m_last_gtid)
    {
        gtid += separator + g.second.to_string();
        separator = ",";
    }

    return gtid;
}

std::map<uint32_t, RWSplit::gtid> RWSplit::last_gtid_map() const
{
    std::shared_lock<mxb::shared_mutex> guard(m_last_gtid_lock);
    return m_last_gtid;
}

void RWSplit::set_last_gtid(std::string_view str)
{
    auto gtid = gtid::from_string(str);
    std::lock_guard<mxb::shared_mutex> guard(m_last_gtid_lock);

    auto& old_gtid = m_last_gtid[gtid.domain];

    if (old_gtid.sequence < gtid.sequence)
    {
        old_gtid = gtid;
    }
}

// static
bool RWSplit::reset_last_gtid(const MODULECMD_ARG* argv, json_t** output)
{
    mxb_assert(argv->argc == 1 && MODULECMD_GET_TYPE(&argv->argv[0].type) == MODULECMD_ARG_SERVICE);

    auto* rws = static_cast<RWSplit*>(argv->argv[0].value.service->router());
    std::lock_guard<mxb::shared_mutex> guard(rws->m_last_gtid_lock);
    rws->m_last_gtid.clear();

    return true;
}

// static
RWSplit::gtid RWSplit::gtid::from_string(std::string_view str)
{
    gtid g;
    g.parse(str);
    return g;
}

void RWSplit::gtid::parse(std::string_view sv)
{
    auto tok = mxb::strtok(sv, "-");
    mxb_assert(tok.size() == 3);
    this->domain = strtoul(tok[0].c_str(), nullptr, 10);
    this->server_id = strtoul(tok[1].c_str(), nullptr, 10);
    this->sequence = strtoul(tok[2].c_str(), nullptr, 10);
}

std::string RWSplit::gtid::to_string() const
{
    return std::to_string(domain) + '-' + std::to_string(server_id) + '-' + std::to_string(sequence);
}

bool RWSplit::gtid::empty() const
{
    return domain == 0 && server_id == 0 && sequence == 0;
}

RWSConfig::RWSConfig(SERVICE* service)
    : mxs::config::Configuration(service->name(), &s_spec)
    , m_service(service)
{
    add_native(&RWSConfig::m_v, &Values::slave_selection_criteria, &s_slave_selection_criteria);
    add_native(&RWSConfig::m_v, &Values::use_sql_variables_in, &s_use_sql_variables_in);
    add_native(&RWSConfig::m_v, &Values::master_failure_mode, &s_master_failure_mode);
    add_native(&RWSConfig::m_v, &Values::master_accept_reads, &s_master_accept_reads);
    add_native(&RWSConfig::m_v, &Values::strict_multi_stmt, &s_strict_multi_stmt);
    add_native(&RWSConfig::m_v, &Values::strict_sp_calls, &s_strict_sp_calls);
    add_native(&RWSConfig::m_v, &Values::strict_tmp_tables, &s_strict_tmp_tables);
    add_native(&RWSConfig::m_v, &Values::retry_failed_reads, &s_retry_failed_reads);
    add_native(&RWSConfig::m_v, &Values::max_replication_lag, &s_max_replication_lag);
    add_native(&RWSConfig::m_v, &Values::max_slave_connections, &s_max_slave_connections);
    add_native(&RWSConfig::m_v, &Values::slave_connections, &s_slave_connections);
    add_native(&RWSConfig::m_v, &Values::causal_reads, &s_causal_reads);
    add_native(&RWSConfig::m_v, &Values::causal_reads_timeout, &s_causal_reads_timeout);
    add_native(&RWSConfig::m_v, &Values::master_reconnection, &s_master_reconnection);
    add_native(&RWSConfig::m_v, &Values::delayed_retry, &s_delayed_retry);
    add_native(&RWSConfig::m_v, &Values::delayed_retry_timeout, &s_delayed_retry_timeout);
    add_native(&RWSConfig::m_v, &Values::transaction_replay, &s_transaction_replay);
    add_native(&RWSConfig::m_v, &Values::trx_max_size, &s_transaction_replay_max_size);
    add_native(&RWSConfig::m_v, &Values::trx_max_attempts, &s_transaction_replay_attempts);
    add_native(&RWSConfig::m_v, &Values::trx_timeout, &s_transaction_replay_timeout);
    add_native(&RWSConfig::m_v, &Values::trx_retry_on_deadlock, &s_transaction_replay_retry_on_deadlock);
    add_native(&RWSConfig::m_v, &Values::trx_retry_on_mismatch, &s_transaction_replay_retry_on_mismatch);
    add_native(&RWSConfig::m_v, &Values::trx_retry_safe_commit, &s_transaction_replay_safe_commit);
    add_native(&RWSConfig::m_v, &Values::trx_checksum, &s_transaction_replay_checksum);
    add_native(&RWSConfig::m_v, &Values::optimistic_trx, &s_optimistic_trx);
    add_native(&RWSConfig::m_v, &Values::lazy_connect, &s_lazy_connect);
    add_native(&RWSConfig::m_v, &Values::reuse_ps, &s_reuse_ps);
}

bool RWSConfig::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    mxb_assert(nested_params.empty());

    m_v.backend_select_fct = get_backend_select_function(m_v.slave_selection_criteria);

    if (m_v.causal_reads != CausalReads::NONE)
    {
        m_v.retry_failed_reads = true;
    }

    if (m_v.optimistic_trx)
    {
        // Optimistic transaction routing requires transaction replay
        m_v.transaction_replay = true;
    }

    if (m_v.transaction_replay || m_v.lazy_connect)
    {
        /**
         * Replaying transactions requires that we are able to do delayed query
         * retries. Both transaction replay and lazy connection creation require
         * fail-on-write failure mode and reconnections to masters.
         */
        if (m_v.transaction_replay)
        {
            m_v.delayed_retry = true;

            // Make sure that delayed_retry_timeout is at least as large as transaction_replay_timeout, this
            // allows the duration a replay can take to be controlled with a single parameter.
            if (m_v.delayed_retry_timeout < m_v.trx_timeout)
            {
                m_v.delayed_retry_timeout = m_v.trx_timeout;
            }
        }
        m_v.master_reconnection = true;
        m_v.master_failure_mode = RW_FAIL_ON_WRITE;
    }

    if (m_v.master_reconnection && m_service->config()->disable_sescmd_history)
    {
        MXB_WARNING("Disabling 'master_reconnection' because 'disable_sescmd_history' is enabled: "
                    "Primary reconnection cannot be done without session command history.");
        m_v.master_reconnection = false;
    }

    // Configuration is OK, assign it to the shared value
    m_values.assign(m_v);

    return true;
}

/**
 * API function definitions
 */

RWSplit* RWSplit::create(SERVICE* service)
{
    service->track_variable(CN_SESSION_TRACK_SYSTEM_VARIABLES);
    return new RWSplit(service);
}

std::shared_ptr<mxs::RouterSession> RWSplit::newSession(MXS_SESSION* session, const Endpoints& endpoints)
{
    try
    {
        return std::make_shared<RWSplitSession>(this, session, RWBackend::from_endpoints(endpoints));
    }
    catch (const RWSException& e)
    {
        MXB_ERROR("%s", e.what());
    }

    return nullptr;
}

json_t* RWSplit::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "queries", json_integer(stats().n_queries));
    json_object_set_new(rval, "route_master", json_integer(stats().n_master));
    json_object_set_new(rval, "route_slave", json_integer(stats().n_slave));
    json_object_set_new(rval, "route_all", json_integer(stats().n_all));
    json_object_set_new(rval, "rw_transactions", json_integer(stats().n_rw_trx));
    json_object_set_new(rval, "ro_transactions", json_integer(stats().n_ro_trx));
    json_object_set_new(rval, "replayed_transactions", json_integer(stats().n_trx_replay));
    json_object_set_new(rval, "trx_max_size_exceeded", json_integer(stats().n_trx_too_big));

    if (config()->reuse_ps)
    {
        json_object_set_new(rval, "prepared_statements_reused", json_integer(stats().n_ps_reused));
    }

    json_t* arr = json_array();

    for (const auto& a : all_server_stats())
    {
        SessionStats::CurrentStats stats = a.second.current_stats();

        double active_pct = (100 * stats.ave_session_active_pct) / 100;

        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.first->name()));
        json_object_set_new(obj, "total", json_integer(stats.total_queries));
        json_object_set_new(obj, "read", json_integer(stats.total_read_queries));
        json_object_set_new(obj, "write", json_integer(stats.total_write_queries));
        json_object_set_new(obj, "avg_sess_duration",
                            json_string(mxb::to_string(stats.ave_session_dur).c_str()));
        json_object_set_new(obj, "avg_sess_active_pct", json_real(active_pct));
        json_object_set_new(obj, "avg_selects_per_session", json_integer(stats.ave_session_selects));
        json_array_append_new(arr, obj);
    }

    json_object_set_new(rval, "server_query_statistics", arr);

    if (config()->causal_reads != CausalReads::NONE)
    {
        auto gtid = last_gtid();
        json_object_set_new(rval, "last_gtid", gtid.empty() ? json_null() : json_string(gtid.c_str()));
    }

    return rval;
}

constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_SESSION_STATE_TRACKING
    | RCAP_TYPE_RUNTIME_CONFIG | RCAP_TYPE_QUERY_CLASSIFICATION | RCAP_TYPE_SESCMD_HISTORY
    | RCAP_TYPE_MULTI_STMT_SQL;

uint64_t RWSplit::getCapabilities() const
{
    return CAPABILITIES;
}

/**
 * The module entry point routine. It is this routine that must return
 * the structure that is referred to as the "module object". This is a
 * structure with the set of external entry points for this module.
 */
extern "C" MXB_API MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t argv[] =
    {
        {MODULECMD_ARG_SERVICE | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Readwritesplit service"},
    };

    modulecmd_register_command(MXB_MODULE_NAME, "reset-gtid", MODULECMD_TYPE_ACTIVE,
                               &RWSplit::reset_last_gtid, 1, argv,
                               "Reset global GTID state in readwritesplit.");

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::GA,
        MXS_ROUTER_VERSION,
        "A Read/Write splitting router for enhancement read scalability",
        "V1.1.0",
        CAPABILITIES,
        &mxs::RouterApi<RWSplit>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &s_spec
    };

    return &info;
}
