/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include "smartsession.hh"
#include "perf_info.hh"
#include <mysqld_error.h>
#include <maxbase/pretty_print.hh>
#include <maxscale/protocol/mariadb/client_connection.hh>

SmartRouterSession::SmartRouterSession(SmartRouter* pRouter,
                                       MXS_SESSION* pSession,
                                       Clusters clusters)
    : mxs::RouterSession(pSession)
    , m_router(*pRouter)
    , m_clusters(std::move(clusters))
    , m_qc(parser(), this, pSession, TYPE_ALL)
{
    for (auto& a : m_clusters)
    {
        a.pBackend->set_userdata(&a);
    }
}

SmartRouterSession::~SmartRouterSession()
{
}

// static
SmartRouterSession* SmartRouterSession::create(SmartRouter* pRouter, MXS_SESSION* pSession,
                                               const std::vector<mxs::Endpoint*>& pEndpoints)
{
    Clusters clusters;

    mxs::Target* pMaster = pRouter->config().master();

    int master_pos = -1;
    int i = 0;

    for (auto e : pEndpoints)
    {
        if (e->connect())
        {
            bool is_master = false;

            if (e->target() == pMaster)
            {
                master_pos = i;
                is_master = true;
            }

            clusters.push_back({e, is_master});
            ++i;
        }
    }

    SmartRouterSession* pSess = nullptr;

    if (master_pos != -1)
    {
        if (master_pos > 0)
        {   // put the master first. There must be exactly one master cluster.
            std::swap(clusters[0], clusters[master_pos]);
        }

        pSess = new SmartRouterSession(pRouter, pSession, std::move(clusters));
    }
    else
    {
        MXB_ERROR("No primary found for %s, smartrouter session cannot be created.",
                  pRouter->config().name().c_str());
    }

    return pSess;
}

bool SmartRouterSession::routeQuery(GWBUF&& buffer)
{
    bool ret = false;

    MXB_SDEBUG("routeQuery() buffer size " << maxbase::pretty_size(buffer.length()));

    if (m_mode == Mode::Kill)
    {
        // The KILL commands are still being executed, queue the query for later execution. This isn't the
        // neatest solution but it's simple enough of a fix.
        //
        // TODO: This doesn't work if the client sends multiple queries before waiting for their results.
        // If/when support for this is added for the smartrouter, the query should be placed into a queue and
        // the queue should be processed when the KILL commands have finished.
        if (!m_queued.empty())
        {
            MXB_ERROR("routeQuery() in wrong state, multiple in-progress queries are not supported.");
            mxb_assert(false);
            return 0;
        }
        else
        {
            MXB_INFO("Queuing query while KILL command is in progress: %s",
                     get_sql_string(buffer).c_str());
            m_queued = std::move(buffer);
            return 1;
        }
    }
    else if (m_mode == Mode::KillDone)
    {
        m_mode = Mode::Idle;
    }

    if (expecting_request_packets())
    {
        ret = write_split_packets(std::move(buffer));
        if (all_clusters_are_idle())
        {
            m_mode = Mode::Idle;
        }
    }
    else if (m_mode != Mode::Idle)
    {
        auto is_busy = !all_clusters_are_idle();
        MXB_SERROR("routeQuery() in wrong state (internal state "
                   << mode_to_string(m_mode) << "). clusters busy = " << std::boolalpha << is_busy);
        mxb_assert(false);
    }
    else
    {
        const auto& route_info = m_qc.update_and_commit_route_info(buffer);
        std::string canonical = std::string(parser().get_canonical(buffer));

        m_measurement = {maxbase::Clock::now(maxbase::NowType::EPollTick), canonical};

        if (m_qc.target_is_all(route_info.target()))
        {
            MXB_SDEBUG("Write all");
            ret = write_to_all(std::move(buffer), Mode::Query);
        }
        else if (m_qc.target_is_master(route_info.target()) || route_info.trx().is_trx_active())
        {
            MXB_SDEBUG("Write to primary");
            ret = write_to_master(std::move(buffer));
        }
        else
        {
            auto perf = m_router.perf_find(canonical);

            if (perf.is_valid())
            {
                MXB_SDEBUG("Smart route to " << perf.target()->name()
                                             << ", canonical = " << maxbase::show_some(canonical));
                ret = write_to_target(perf.target(), std::move(buffer));
            }
            else if (mariadb::is_com_query(buffer))
            {
                MXB_SDEBUG("Start measurement");
                ret = write_to_all(std::move(buffer), Mode::MeasureQuery);
            }
            else
            {
                MXB_SWARNING("Could not determine target (non-sql query), goes to master");
                ret = write_to_master(std::move(buffer));
            }
        }
    }

    return ret;
}

bool SmartRouterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    using maxbase::operator<<;

    Cluster& cluster = *static_cast<Cluster*>(down.endpoint()->get_userdata());

    auto tracker_state_before = cluster.tracker.state();

    m_qc.update_from_reply(reply);
    cluster.tracker.update_response(&packet);

    // these flags can all be true at the same time
    bool first_response_packet = (m_mode == Mode::Query || m_mode == Mode::MeasureQuery);
    bool last_packet_for_this_cluster = !cluster.tracker.expecting_response_packets();
    bool very_last_response_packet = !expecting_response_packets();     // last from all clusters

    MXB_SDEBUG("Reply from " << std::boolalpha
                             << cluster.pBackend->target()->name()
                             << " is_master=" << cluster.is_master
                             << " first_packet=" << first_response_packet
                             << " last_packet=" << last_packet_for_this_cluster
                             << " very_last_packet=" << very_last_response_packet
                             << " delayed_response=" << (!m_delayed_packet.empty())
                             << " tracker_state: " << tracker_state_before << " => "
                             << cluster.tracker.state());

    // marker1: If a connection is lost down the pipeline, we first get an ErrorPacket, then a call to
    // handleError(). If we only rely on the handleError() the client receiving the ErrorPacket
    // could retry using this connection/session, causing an error (or assert) in routeQuery().
    // This will change once we implement direct function calls to the Clusters (which really
    // are routers).
    if (cluster.tracker.state() == maxsql::PacketTracker::State::ErrorPacket)
    {
        auto err_code = mxs_mysql_get_mysql_errno(packet);
        switch (err_code)
        {
        case ER_CONNECTION_KILLED:      // there might be more error codes needing to be caught here
            MXB_SERROR("clientReply(): Lost connection to " << cluster.pBackend->target()->name()
                                                            << " Error code=" << err_code
                                                            << ' ' << mariadb::extract_error(packet));
            m_pSession->kill();
            return 1;
        }
    }

    if (cluster.tracker.state() == maxsql::PacketTracker::State::Error)
    {
        MXB_SERROR("ProtocolTracker from state " << tracker_state_before
                                                 << " to state " << cluster.tracker.state()
                                                 << ". Disconnect.");
        m_pSession->kill();
        return 1;
    }

    bool will_reply = false;

    if (first_response_packet)
    {
        maxbase::Duration query_dur = maxbase::Clock::now(maxbase::NowType::EPollTick) - m_measurement.start;
        MXB_SDEBUG("Target " << cluster.pBackend->target()->name()
                             << " will be responding to the client."
                             << " First packet received in time " << query_dur);
        cluster.is_replying_to_client = true;
        will_reply = true;      // tentatively, the packet might have to be delayed

        if (m_mode == Mode::MeasureQuery)
        {
            m_router.perf_update(m_measurement.canonical, {cluster.pBackend->target(), query_dur});
            // If the query is still going on, an error packet is received, else the
            // whole query might play out (and be discarded).
            kill_all_others(cluster);
            m_mode = Mode::Kill;
        }
        else
        {
            m_mode = Mode::CollectResults;
        }
    }

    if (very_last_response_packet)
    {
        will_reply = true;
        if (m_mode != Mode::Kill)
        {
            m_mode = Mode::Idle;
        }
        mxb_assert(cluster.is_replying_to_client || m_delayed_packet);
        if (m_delayed_packet)
        {
            MXB_SDEBUG("Picking up delayed packet, discarding response from "
                       << cluster.pBackend->target()->name());
            packet = std::move(m_delayed_packet);
            m_delayed_packet.clear();
        }
    }
    else if (cluster.is_replying_to_client)
    {
        if (last_packet_for_this_cluster)
        {
            // Delay sending the last packet until all clusters have responded. The code currently
            // does not allow multiple client-queries at the same time (no query buffer)
            MXB_SDEBUG("Delaying last packet");
            mxb_assert(!m_delayed_packet);
            m_delayed_packet = std::move(packet);
            will_reply = false;
        }
        else
        {
            will_reply = true;
        }
    }
    else
    {
        MXB_SDEBUG("Discarding response from " << cluster.pBackend->target()->name());
    }

    int32_t rc = 1;

    if (will_reply)
    {
        MXB_SDEBUG("Forward response to client");
        rc = RouterSession::clientReply(std::move(packet), down, reply);
    }

    return rc;
}

bool SmartRouterSession::expecting_request_packets() const
{
    return std::any_of(begin(m_clusters), end(m_clusters),
                       [](const Cluster& cluster) {
                           return cluster.tracker.expecting_request_packets();
                       });
}

bool SmartRouterSession::expecting_response_packets() const
{
    return std::any_of(begin(m_clusters), end(m_clusters),
                       [](const Cluster& cluster) {
                           return cluster.tracker.expecting_response_packets();
                       });
}

bool SmartRouterSession::all_clusters_are_idle() const
{
    return std::all_of(begin(m_clusters), end(m_clusters),
                       [](const Cluster& cluster) {
                           return !cluster.tracker.expecting_more_packets();
                       });
}

bool SmartRouterSession::write_to_master(GWBUF&& buffer)
{
    mxb_assert(!m_clusters.empty());
    auto& cluster = m_clusters[0];
    mxb_assert(cluster.is_master);
    cluster.tracker = maxsql::PacketTracker(&buffer);
    cluster.is_replying_to_client = false;

    if (cluster.tracker.expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    return cluster.pBackend->routeQuery(std::move(buffer));
}

bool SmartRouterSession::write_to_target(mxs::Target* target, GWBUF&& buffer)
{
    auto it = std::find_if(begin(m_clusters), end(m_clusters), [target](const Cluster& cluster) {
                               return cluster.pBackend->target() == target;
                           });
    mxb_assert(it != end(m_clusters));
    auto& cluster = *it;
    cluster.tracker = maxsql::PacketTracker(&buffer);
    if (cluster.tracker.expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    cluster.is_replying_to_client = false;

    return cluster.pBackend->routeQuery(std::move(buffer));
}

bool SmartRouterSession::write_to_all(GWBUF&& buffer, Mode mode)
{
    bool success = true;

    for (auto& a : m_clusters)
    {
        a.tracker = maxsql::PacketTracker(&buffer);
        a.is_replying_to_client = false;

        if (!a.pBackend->routeQuery(buffer.shallow_clone()))
        {
            success = false;
        }
    }

    if (expecting_response_packets())
    {
        m_mode = mode;
    }

    return success;
}

bool SmartRouterSession::write_split_packets(GWBUF&& buffer)
{
    bool success = true;

    for (auto& a : m_clusters)
    {
        if (a.tracker.expecting_request_packets())
        {
            a.tracker.update_request(&buffer);

            if (!a.pBackend->routeQuery(buffer.shallow_clone()))
            {
                success = false;
                break;
            }
        }
    }

    return success;
}

void SmartRouterSession::kill_all_others(const Cluster& cluster)
{
    auto protocol = static_cast<MariaDBClientConnection*>(m_pSession->client_connection());
    protocol->mxs_mysql_execute_kill(m_pSession->id(), MariaDBClientConnection::KT_QUERY, [this](){
        mxb_assert_message(m_mode == Mode::Kill, "Mode is %s instead of Kill", mode_to_string(m_mode));
        m_mode = Mode::KillDone;

        if (m_queued)
        {
            MXB_INFO("Routing queued query: %s", get_sql_string(m_queued).c_str());
            m_pSession->delay_routing(this, std::move(m_queued), 0ms);
            m_queued.clear();
        }
    });
}

bool SmartRouterSession::lock_to_master()
{
    return false;
}

bool SmartRouterSession::is_locked_to_master() const
{
    return false;
}

bool SmartRouterSession::supports_hint(Hint::Type hint_type) const
{
    return false;
}
