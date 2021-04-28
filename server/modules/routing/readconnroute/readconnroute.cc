/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
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
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "A connection based router to load balance based on connections",
        "V2.0.0",
        RCAP_TYPE_RUNTIME_CONFIG,
        &RCR::s_object,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
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
static mxs::Endpoint* get_root_master(const Endpoints& endpoints)
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

bool RCR::configure(mxs::ConfigParameters* params)
{
    uint64_t bitmask = 0;
    uint64_t bitvalue = 0;
    bool ok = true;

    for (const auto& opt : mxs::strtok(params->get_string("router_options"), ", \t"))
    {
        if (!strcasecmp(opt.c_str(), "master"))
        {
            bitmask |= (SERVER_MASTER | SERVER_SLAVE);
            bitvalue |= SERVER_MASTER;
        }
        else if (!strcasecmp(opt.c_str(), "slave"))
        {
            bitmask |= (SERVER_MASTER | SERVER_SLAVE);
            bitvalue |= SERVER_SLAVE;
        }
        else if (!strcasecmp(opt.c_str(), "running"))
        {
            bitmask |= (SERVER_RUNNING);
            bitvalue |= SERVER_RUNNING;
        }
        else if (!strcasecmp(opt.c_str(), "synced"))
        {
            bitmask |= (SERVER_JOINED);
            bitvalue |= SERVER_JOINED;
        }
        else
        {
            MXS_ERROR("Unsupported router option \'%s\' for readconnroute. "
                      "Expected router options are [slave|master|synced|running]",
                      opt.c_str());
            ok = false;
        }
    }

    if (bitmask == 0 && bitvalue == 0)
    {
        /** No parameters given, use RUNNING as a valid server */
        bitmask |= (SERVER_RUNNING);
        bitvalue |= SERVER_RUNNING;
    }

    if (ok)
    {
        uint64_t mask = bitmask | (bitvalue << 32);
        atomic_store_uint64(&m_bitmask_and_bitvalue, mask);
    }

    return ok;
}


RCR::RCR(SERVICE* service)
    : mxs::Router<RCR, RCRSession>(service)
{
}

// static
RCR* RCR::create(SERVICE* service, mxs::ConfigParameters* params)
{
    RCR* inst = new(std::nothrow) RCR(service);

    if (inst && !inst->configure(params))
    {
        delete inst;
        inst = nullptr;
    }

    return inst;
}

RCRSession::RCRSession(RCR* inst, MXS_SESSION* session, mxs::Endpoint* backend,
                       const Endpoints& endpoints, uint32_t bitmask, uint32_t bitvalue)
    : mxs::RouterSession(session)
    , m_instance(inst)
    , m_bitmask(bitmask)
    , m_bitvalue(bitvalue)
    , m_backend(backend)
    , m_endpoints(endpoints)
    , m_session_stats(inst->session_stats(backend->target()))
{
}

RCRSession::~RCRSession()
{
    m_session_stats.update(m_session_timer.split(),
                           m_query_timer.total(),
                           m_session_queries);
}

RCRSession* RCR::newSession(MXS_SESSION* session, const Endpoints& endpoints)
{
    uint64_t mask = atomic_load_uint64(&m_bitmask_and_bitvalue);
    uint32_t bitmask = mask;
    uint32_t bitvalue = mask >> 32;

    /**
     * Find the Master host from available servers
     */
    mxs::Endpoint* master_host = get_root_master(endpoints);

    bool connectable_master = master_host ? master_host->target()->is_connectable() : false;

    /**
     * Find a backend server to connect to. This is the extent of the
     * load balancing algorithm we need to implement for this simple
     * connection router.
     */
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
            continue;
        }

        mxb_assert(e->target()->is_usable());

        /* Check server status bits against bitvalue from router_options */
        if (e->target()->status() & bitmask & bitvalue)
        {
            if (master_host && connectable_master)
            {
                if (e == master_host && (bitvalue & (SERVER_SLAVE | SERVER_MASTER)) == SERVER_SLAVE)
                {
                    /* Skip root master here, as it could also be slave of an external server that
                     * is not in the configuration.  Intermediate masters (Relay Servers) are also
                     * slave and will be selected as Slave(s)
                     */

                    continue;
                }
                if (e == master_host && bitvalue == SERVER_MASTER)
                {
                    /* If option is "master" return only the root Master as there could be
                     * intermediate masters (Relay Servers) and they must not be selected.
                     */

                    candidate = master_host;
                    break;
                }
            }
            else if (bitvalue == SERVER_MASTER)
            {
                /* Master_host is nullptr, no master server.  If requested router_option is 'master'
                 * candidate will be nullptr.
                 */
                candidate = nullptr;
                break;
            }

            /* If no candidate set, set first running server as our initial candidate server */
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

    /* If we haven't found a proper candidate yet but a master server is available, we'll pick that
     * with the assumption that it is "better" than a slave.
     */
    if (!candidate)
    {
        if (master_host && connectable_master)
        {
            candidate = master_host;
            // Even if we had 'router_options=slave' in the configuration file, we
            // will still end up here if there are no slaves, but a sole master. So
            // that the server will be considered valid in connection_is_valid(), we
            // turn on the SERVER_MASTER bit.
            //
            // We must do that so that readconnroute in MaxScale 2.2 will again behave
            // the same way as it did up until 2.1.12.
            if (bitvalue & SERVER_SLAVE)
            {
                bitvalue |= SERVER_MASTER;
            }
        }
        else
        {
            if (!master_host)
            {
                MXS_ERROR("Failed to create new routing session. Couldn't find eligible"
                          " candidate server. Freeing allocated resources.");
            }
            else
            {
                mxb_assert(!connectable_master);
                MXS_ERROR("The only possible candidate server (%s) is being drained "
                          "and thus cannot be used.", master_host->target()->name());
            }
            return nullptr;
        }
    }
    else
    {
        mxb_assert(candidate->target()->is_connectable());
    }

    if (!candidate->connect())
    {
        /** The failure is reported in dcb_connect() */
        return nullptr;
    }

    RCRSession* client_rses = new RCRSession(this, session, candidate, endpoints, bitmask, bitvalue);

    MXS_INFO("New session for server %s. Connections : %d",
             candidate->target()->name(),
             candidate->target()->stats().n_current);

    return client_rses;
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

    if (m_backend->target()->is_usable() && (m_backend->target()->status() & m_bitmask & m_bitvalue))
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

int RCRSession::routeQuery(GWBUF* queue)
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
    if (m_bitvalue & SERVER_MASTER)
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
    mxb::atomic::add(&m_backend->target()->stats().packets, 1, mxb::atomic::RELAXED);

    return m_backend->routeQuery(queue);
}

void RCRSession::clientReply(GWBUF* pPacket, const maxscale::ReplyRoute& down, const maxscale::Reply& pReply)
{
    RouterSession::clientReply(pPacket, down, pReply);
    m_query_timer.end_interval();
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

uint64_t RCR::getCapabilities()
{
    return RCAP_TYPE_RUNTIME_CONFIG;
}
