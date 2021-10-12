/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#include "smartsession.hh"
#include "perf_info.hh"
#include <mysqld_error.h>
#include <maxscale/mysql_plus.hh>
#include <maxscale/modutil.hh>
#include <maxbase/pretty_print.hh>
#include <maxscale/protocol/mariadb/client_connection.hh>

SmartRouterSession::SmartRouterSession(SmartRouter* pRouter,
                                       MXS_SESSION* pSession,
                                       Clusters clusters)
    : mxs::RouterSession(pSession)
    , m_router(*pRouter)
    , m_clusters(std::move(clusters))
    , m_qc(this, pSession, TYPE_ALL)
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
        MXS_ERROR("No master found for %s, smartrouter session cannot be created.",
                  pRouter->config().name().c_str());
    }

    return pSess;
}

int SmartRouterSession::routeQuery(GWBUF* pBuf)
{
    bool ret = false;

    MXS_SDEBUG("routeQuery() buffer size " << maxbase::pretty_size(gwbuf_length(pBuf)));

    if (expecting_request_packets())
    {
        ret = write_split_packets(pBuf);
        if (all_clusters_are_idle())
        {
            m_mode = Mode::Idle;
        }
    }
    else if (m_mode != Mode::Idle)
    {
        auto is_busy = !all_clusters_are_idle();
        MXS_SERROR("routeQuery() in wrong state. clusters busy = " << std::boolalpha << is_busy);
        mxb_assert(false);
    }
    else
    {
        auto route_info = m_qc.update_route_info(mxs::QueryClassifier::CURRENT_TARGET_UNDEFINED, pBuf);
        std::string canonical = maxscale::get_canonical(pBuf);

        m_measurement = {maxbase::Clock::now(maxbase::NowType::EPollTick), canonical};

        if (m_qc.target_is_all(route_info.target()))
        {
            MXS_SDEBUG("Write all");
            ret = write_to_all(pBuf, Mode::Query);
        }
        else if (m_qc.target_is_master(route_info.target()) || m_pSession->is_trx_active())
        {
            MXS_SDEBUG("Write to master");
            ret = write_to_master(pBuf);
        }
        else
        {
            auto perf = m_router.perf_find(canonical);

            if (perf.is_valid())
            {
                MXS_SDEBUG("Smart route to " << perf.target()->name()
                                             << ", canonical = " << show_some(canonical));
                ret = write_to_target(perf.target(), pBuf);
            }
            else if (modutil_is_SQL(pBuf))
            {
                MXS_SDEBUG("Start measurement");
                ret = write_to_all(pBuf, Mode::MeasureQuery);
            }
            else
            {
                MXS_SWARNING("Could not determine target (non-sql query), goes to master");
                ret = write_to_master(pBuf);
            }
        }
    }

    return ret;
}

void SmartRouterSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    using maxbase::operator<<;

    mxb_assert(gwbuf_is_contiguous(pPacket));
    Cluster& cluster = *static_cast<Cluster*>(down.back()->get_userdata());

    auto tracker_state_before = cluster.tracker.state();

    cluster.tracker.update_response(pPacket);

    // these flags can all be true at the same time
    bool first_response_packet = (m_mode == Mode::Query || m_mode == Mode::MeasureQuery);
    bool last_packet_for_this_cluster = !cluster.tracker.expecting_response_packets();
    bool very_last_response_packet = !expecting_response_packets();     // last from all clusters

    MXS_SDEBUG("Reply from " << std::boolalpha
                             << cluster.pBackend->target()->name()
                             << " is_master=" << cluster.is_master
                             << " first_packet=" << first_response_packet
                             << " last_packet=" << last_packet_for_this_cluster
                             << " very_last_packet=" << very_last_response_packet
                             << " delayed_response=" << (m_pDelayed_packet != nullptr)
                             << " tracker_state: " << tracker_state_before << " => "
                             << cluster.tracker.state());

    // marker1: If a connection is lost down the pipeline, we first get an ErrorPacket, then a call to
    // handleError(). If we only rely on the handleError() the client receiving the ErrorPacket
    // could retry using this connection/session, causing an error (or assert) in routeQuery().
    // This will change once we implement direct function calls to the Clusters (which really
    // are routers).
    if (cluster.tracker.state() == maxsql::PacketTracker::State::ErrorPacket)
    {
        auto err_code = mxs_mysql_get_mysql_errno(pPacket);
        switch (err_code)
        {
        case ER_CONNECTION_KILLED:      // there might be more error codes needing to be caught here
            MXS_SERROR("clientReply(): Lost connection to " << cluster.pBackend->target()->name()
                                                            << " Error code=" << err_code
                                                            << ' ' << mxs::extract_error(pPacket));
            m_pSession->kill();
            return;
        }
    }

    if (cluster.tracker.state() == maxsql::PacketTracker::State::Error)
    {
        MXS_SERROR("ProtocolTracker from state " << tracker_state_before
                                                 << " to state " << cluster.tracker.state()
                                                 << ". Disconnect.");
        m_pSession->kill();
        return;
    }

    bool will_reply = false;

    if (first_response_packet)
    {
        maxbase::Duration query_dur = maxbase::Clock::now(maxbase::NowType::EPollTick) - m_measurement.start;
        MXS_SDEBUG("Target " << cluster.pBackend->target()->name()
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
        }

        m_mode = Mode::CollectResults;
    }

    if (very_last_response_packet)
    {
        will_reply = true;
        m_mode = Mode::Idle;
        mxb_assert(cluster.is_replying_to_client || m_pDelayed_packet);
        if (m_pDelayed_packet)
        {
            MXS_SDEBUG("Picking up delayed packet, discarding response from "
                       << cluster.pBackend->target()->name());
            gwbuf_free(pPacket);
            pPacket = m_pDelayed_packet;
            m_pDelayed_packet = nullptr;
        }
    }
    else if (cluster.is_replying_to_client)
    {
        if (last_packet_for_this_cluster)
        {
            // Delay sending the last packet until all clusters have responded. The code currently
            // does not allow multiple client-queries at the same time (no query buffer)
            MXS_SDEBUG("Delaying last packet");
            mxb_assert(!m_pDelayed_packet);
            m_pDelayed_packet = pPacket;
            will_reply = false;
        }
        else
        {
            will_reply = true;
        }
    }
    else
    {
        MXS_SDEBUG("Discarding response from " << cluster.pBackend->target()->name());
        gwbuf_free(pPacket);
    }

    if (will_reply)
    {
        MXS_SDEBUG("Forward response to client");
        RouterSession::clientReply(pPacket, down, reply);
    }
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

bool SmartRouterSession::write_to_master(GWBUF* pBuf)
{
    mxb_assert(!m_clusters.empty());
    auto& cluster = m_clusters[0];
    mxb_assert(cluster.is_master);
    cluster.tracker = maxsql::PacketTracker(pBuf);
    cluster.is_replying_to_client = false;

    if (cluster.tracker.expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    return cluster.pBackend->routeQuery(pBuf);
}

bool SmartRouterSession::write_to_target(mxs::Target* target, GWBUF* pBuf)
{
    auto it = std::find_if(begin(m_clusters), end(m_clusters), [target](const Cluster& cluster) {
                               return cluster.pBackend->target() == target;
                           });
    mxb_assert(it != end(m_clusters));
    auto& cluster = *it;
    cluster.tracker = maxsql::PacketTracker(pBuf);
    if (cluster.tracker.expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    cluster.is_replying_to_client = false;

    return cluster.pBackend->routeQuery(pBuf);
}

bool SmartRouterSession::write_to_all(GWBUF* pBuf, Mode mode)
{
    bool success = true;

    for (auto& a : m_clusters)
    {
        a.tracker = maxsql::PacketTracker(pBuf);
        a.is_replying_to_client = false;

        if (!a.pBackend->routeQuery(gwbuf_clone(pBuf)))
        {
            success = false;
        }
    }

    gwbuf_free(pBuf);

    if (expecting_response_packets())
    {
        m_mode = mode;
    }

    return success;
}

bool SmartRouterSession::write_split_packets(GWBUF* pBuf)
{
    bool success = true;

    for (auto& a : m_clusters)
    {
        if (a.tracker.expecting_request_packets())
        {
            a.tracker.update_request(pBuf);

            if (!a.pBackend->routeQuery(gwbuf_clone(pBuf)))
            {
                success = false;
                break;
            }
        }
    }

    gwbuf_free(pBuf);

    return success;
}

void SmartRouterSession::kill_all_others(const Cluster& cluster)
{
    auto protocol = static_cast<MariaDBClientConnection*>(m_pSession->client_connection());
    protocol->mxs_mysql_execute_kill(m_pSession->id(), KT_QUERY);
}

bool SmartRouterSession::handleError(mxs::ErrorType type,
                                     GWBUF* pPacket,
                                     mxs::Endpoint* pProblem,
                                     const mxs::Reply& pReply)
{
    auto err_code = mxs_mysql_get_mysql_errno(pPacket);
    MXS_SERROR("handleError(): Lost connection to "
               << pProblem->target()->name() << " Error code=" << err_code << " "
               << mxs::extract_error(pPacket));

    m_pSession->kill(gwbuf_clone(pPacket));
    return false;
}

bool SmartRouterSession::lock_to_master()
{
    return false;
}

bool SmartRouterSession::is_locked_to_master() const
{
    return false;
}

bool SmartRouterSession::supports_hint(HINT_TYPE hint_type) const
{
    return false;
}
