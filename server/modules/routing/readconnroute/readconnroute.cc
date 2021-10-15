/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file readconnroute.c - Read Connection Load Balancing Query Router
 *
 * This is the implementation of a simple query router that balances
 * read connections. It assumes the service is configured with a set
 * of slaves and that the application clients already split read and write
 * queries. It offers a service to balance the client read connections
 * over this set of slave servers. It does this once only, at the time
 * the connection is made. It chooses the server that currently has the least
 * number of connections by keeping a count for each server of how
 * many connections the query router has made to the server.
 *
 * When two servers have the same number of current connections the one with
 * the least number of connections since startup will be used.
 *
 * The router may also have options associated to it that will limit the
 * choice of backend server. Currently two options are supported, the "master"
 * option will cause the router to only connect to servers marked as masters
 * and the "slave" option will limit connections to routers that are marked
 * as slaves. If neither option is specified the router will connect to either
 * masters or slaves.
 */

#include "readconnroute.hh"

#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>

config::Specification RCR::Config::s_specification(MXS_MODULE_NAME, config::Specification::ROUTER);

config::ParamEnumMask<uint32_t> RCR::Config::s_router_options(
    &s_specification,
    "router_options",
    "A comma separated list of server roles",
{
    {SERVER_MASTER, "master"},
    {SERVER_SLAVE, "slave"},
    {SERVER_RUNNING, "running"},
    {SERVER_JOINED, "synced"},
},
    SERVER_RUNNING,
    config::Param::AT_RUNTIME
    );

config::ParamBool RCR::Config::s_master_accept_reads(
    &s_specification,
    "master_accept_reads",
    "Use master for reads",
    true,
    config::Param::AT_RUNTIME
    );

config::ParamSeconds RCR::Config::s_max_replication_lag(
    &s_specification,
    "max_replication_lag",
    "Maximum acceptable replication lag",
    config::INTERPRET_AS_SECONDS,
    std::chrono::seconds(0),
    config::Param::AT_RUNTIME
    );

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::GA,
        MXS_ROUTER_VERSION,
        "A connection based router to load balance based on connections",
        "V2.0.0",
        RCAP_TYPE_RUNTIME_CONFIG,
        &mxs::RouterApi<RCR>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    RCR::Config::populate(info);

    return &info;
}

RCR::Config::Config(const std::string& name)
    : config::Configuration(name, &s_specification)
    , router_options(this, &s_router_options)
    , master_accept_reads(this, &s_master_accept_reads)
    , max_replication_lag(this, &s_max_replication_lag)
{
}

void RCR::Config::populate(MXS_MODULE& module)
{
    module.specification = &s_specification;
}

/*
 * This routine returns the master server from a MariaDB replication tree. The server must be
 * running, not in maintenance and have the master bit set. If multiple masters are found,
 * the one with the highest weight is chosen.
 *
 * @param servers The list of servers
 *
 * @return The Master server
 */
static mxs::Endpoint* get_root_master(const mxs::Endpoints& endpoints)
{
    auto best_rank = std::numeric_limits<int64_t>::max();
    mxs::Endpoint* master_host = nullptr;

    for (auto e : endpoints)
    {
        if (e->target()->is_master())
        {
            auto rank = e->target()->rank();

            if (!master_host)
            {
                // No master found yet
                master_host = e;
            }
            else if (rank < best_rank)
            {
                best_rank = rank;
                master_host = e;
            }
        }
    }

    return master_host;
}

// static
RCR* RCR::create(SERVICE* service)
{
    return new RCR(service);
}

RCRSession::RCRSession(RCR* inst, MXS_SESSION* session, mxs::Endpoint* backend,
                       const mxs::Endpoints& endpoints, uint32_t bitvalue)
    : mxs::RouterSession(session)
    , m_instance(inst)
    , m_bitvalue(bitvalue)
    , m_backend(backend)
    , m_endpoints(endpoints)
    , m_session_stats(inst->session_stats(backend->target()))
{
    if (backend->target()->is_master() && (m_bitvalue & SERVER_SLAVE))
    {
        // Even if we have 'router_options=slave' in the configuration file, we can end up selecting the
        // master if there are no slaves. In order for the server to be considered valid in
        // connection_is_valid(), we turn on the SERVER_MASTER bit.
        m_bitvalue |= SERVER_MASTER;
    }
}

RCRSession::~RCRSession()
{
    m_session_stats.update(m_session_timer.split(),
                           m_query_timer.total(),
                           m_session_queries);
}

mxs::RouterSession* RCR::newSession(MXS_SESSION* session, const mxs::Endpoints& endpoints)
{
    RCRSession* rses = nullptr;

    if (mxs::Endpoint* candidate = get_connection(endpoints))
    {
        mxb_assert(candidate->target()->is_connectable());

        if (candidate->connect())
        {
            rses = new RCRSession(this, session, candidate, endpoints, m_config.router_options.get());

            MXS_INFO("New session for server %s. Connections : %d",
                     candidate->target()->name(),
                     candidate->target()->stats().n_current);
        }
    }
    else
    {
        MXS_ERROR("Failed to create new routing session: Couldn't find eligible candidate server.");
    }

    return rses;
}

mxs::Endpoint* RCR::get_connection(const mxs::Endpoints& endpoints)
{
    uint64_t bitvalue = m_config.router_options.get();
    mxs::Endpoint* master_host = get_root_master(endpoints);
    bool connectable_master = master_host ? master_host->target()->is_connectable() : false;
    int64_t max_lag = m_config.max_replication_lag.get().count();

    if (bitvalue == SERVER_MASTER)
    {
        if (connectable_master)
        {
            return master_host;
        }
        else
        {
            return nullptr;     // No master server, cannot continue.
        }
    }

    // Do not include the master in ranking if the master option is not set (anything but master), and reads
    // should not be routed to the master. The master will still be selected, if it is the last man standing.
    bool do_not_rank_master = !(bitvalue & SERVER_MASTER) && m_config.master_accept_reads.get() == false;

    // Find a backend server to connect to. This is the extent of the load balancing algorithm we need to
    // implement for this simple connection router.
    mxs::Endpoint* candidate = nullptr;
    auto best_rank = std::numeric_limits<int64_t>::max();

    /*
     * Loop over all the servers and find any that have fewer connections
     * than the candidate server.
     *
     * If a server has less connections than the current candidate we mark this
     * as the new candidate to connect to.
     *
     * If a server has the same number of connections currently as the candidate
     * and has had less connections over time than the candidate it will also
     * become the new candidate. This has the effect of spreading the
     * connections over different servers during periods of very low load.
     */
    for (auto e : endpoints)
    {
        if (!e->target()->is_connectable())
        {
            continue;   // Server is down, can't use it.
        }

        if (do_not_rank_master && e == master_host)
        {
            continue;   // If no other servers are available the master will still selected
        }

        mxb_assert(e->target()->is_usable());

        // Check server status bits against bitvalue from router_options
        if (e->target()->status() & bitvalue)
        {
            if (e == master_host && connectable_master
                && (bitvalue & (SERVER_SLAVE | SERVER_MASTER)) == SERVER_SLAVE)
            {
                // Skip root master here, as it could also be slave of an external server that is not in the
                // configuration.  Intermediate masters (Relay Servers) are also slave and will be selected as
                // Slave(s)
                continue;
            }
            else if (max_lag && e->target()->replication_lag() >= max_lag)
            {
                // This server is lagging too far behind
                continue;
            }

            // If no candidate set, set first running server as our initial candidate server
            if (!candidate || e->target()->rank() < best_rank)
            {
                best_rank = e->target()->rank();
                candidate = e;
            }
            else if (e->target()->rank() == best_rank
                     && e->target()->stats().n_current < candidate->target()->stats().n_current)
            {
                // This one is better
                candidate = e;
            }
        }
    }

    // If we haven't found a proper candidate yet but a master server is available, we'll pick that with the
    // assumption that it is "better" than a slave.
    if (!candidate && connectable_master)
    {
        candidate = master_host;
    }

    return candidate;
}

/** Log routing failure due to closed session */
static void log_closed_session(uint8_t mysql_command, mxs::Target* t)
{
    char msg[1024 + 200] = "";      // Extra space for message

    if (t->is_down())
    {
        sprintf(msg, "Server '%s' is down.", t->name());
    }
    else if (t->is_in_maint())
    {
        sprintf(msg, "Server '%s' is in maintenance.", t->name());
    }
    else
    {
        sprintf(msg, "Server '%s' no longer qualifies as a target server.", t->name());
    }

    MXS_ERROR("Failed to route MySQL command %d to backend server. %s", mysql_command, msg);
}

/**
 * Check if the server we're connected to is still valid
 *
 * @param inst           Router instance
 * @param router_cli_ses Router session
 *
 * @return True if the backend connection is still valid
 */
bool RCRSession::connection_is_valid() const
{
    bool rval = false;

    // m_instance->bitvalue and m_bitvalue are different, if we had
    // 'router_options=slave' in the configuration file and there was only
    // the sole master available at session creation time.

    if (m_backend->target()->is_usable() && (m_backend->target()->status() & m_bitvalue))
    {
        // Note the use of '==' and not '|'. We must use the former to exclude a
        // 'router_options=slave' that uses the master due to no slave having been
        // available at session creation time. Its bitvalue is (SERVER_MASTER | SERVER_SLAVE).
        if (m_bitvalue == SERVER_MASTER && m_backend->target()->active())
        {
            // If we're using an active master server, verify that it is still a master
            rval = m_backend == get_root_master(m_endpoints);
        }
        else
        {
            /**
             * Either we don't use master type servers or the server reference
             * is deactivated. We let deactivated connection close gracefully
             * so we simply assume it is OK. This allows a server to be taken
             * out of use in a manner that won't cause errors to the connected
             * clients.
             */
            rval = true;
        }
    }

    return rval;
}

bool RCRSession::routeQuery(GWBUF* queue)
{
    uint8_t mysql_command = mxs_mysql_get_command(queue);

    if (!connection_is_valid())
    {
        log_closed_session(mysql_command, m_backend->target());
        gwbuf_free(queue);
        return 0;
    }

    MXS_INFO("Routed [%s] to '%s' %s",
             STRPACKETTYPE(mysql_command),
             m_backend->target()->name(),
             mxs::extract_sql(queue).c_str());

    m_query_timer.start_interval();

    m_session_stats.inc_total();
    if ((m_bitvalue & (SERVER_MASTER | SERVER_SLAVE)) == SERVER_MASTER)
    {
        // not necessarily a write, but explicitely routed to a master
        m_session_stats.inc_write();
    }
    else
    {
        // could be a write, in which case the user has other problems
        m_session_stats.inc_read();
    }

    ++m_session_queries;

    return m_backend->routeQuery(queue);
}

bool RCRSession::clientReply(GWBUF* pPacket,
                             const maxscale::ReplyRoute& down,
                             const maxscale::Reply& pReply)
{
    auto rc = RouterSession::clientReply(pPacket, down, pReply);
    m_query_timer.end_interval();
    return rc;
}

RCR::RCR(SERVICE* service)
    : m_config(service->name())
{
}

maxscale::SessionStats& RCR::session_stats(maxscale::Target* pTarget)
{
    return (*m_target_stats)[pTarget];
}

maxscale::TargetSessionStats RCR::combined_target_stats() const
{
    maxscale::TargetSessionStats stats;

    for (const auto& a : m_target_stats.values())
    {
        for (const auto& b : a)
        {
            if (b.first->active())
            {
                stats[b.first] += b.second;
            }
        }
    }

    return stats;
}

json_t* RCR::diagnostics() const
{
    json_t* arr = json_array();
    int64_t total_packets = 0;

    for (const auto& a : combined_target_stats())
    {
        maxscale::SessionStats::CurrentStats stats = a.second.current_stats();

        total_packets += stats.total_queries;

        double active_pct = std::round(100 * stats.ave_session_active_pct) / 100;

        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.first->name()));
        json_object_set_new(obj, "total", json_integer(stats.total_queries));
        json_object_set_new(obj, "read", json_integer(stats.total_read_queries));
        json_object_set_new(obj, "write", json_integer(stats.total_write_queries));
        json_object_set_new(obj, "avg_sess_duration",
                            json_string(mxb::to_string(stats.ave_session_dur).c_str()));
        json_object_set_new(obj, "avg_sess_active_pct", json_real(active_pct));
        json_object_set_new(obj, "avg_queries_per_session", json_integer(stats.ave_session_selects));
        json_array_append_new(arr, obj);
    }

    json_t* rval = json_object();

    json_object_set_new(rval, "queries", json_integer(total_packets));
    json_object_set_new(rval, "server_query_statistics", arr);

    return rval;
}

uint64_t RCR::getCapabilities() const
{
    return RCAP_TYPE_RUNTIME_CONFIG;
}
