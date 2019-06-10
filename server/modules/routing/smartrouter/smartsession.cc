/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "smartsession.hh"
#include "smartrouter.hh"

#include <maxscale/modutil.hh>
#include <maxsql/mysql_plus.hh>

// TO_REVIEW:
// This is the base for Smart Router. It will currently route any normal query to all
// configured routers and only use the first response to forward back to the client.
// However, it should be reviewed as if it could actually be put in front of
// several current routers (several readwritesplits and readconnroutes) and
// succeed for anything but local infile.

// TO_REVIEW. There is no need to go through the functionality with a very, very fine-comb at this point,
//            maria-test will be used to do that. The idea is really to look for the totality of the
//            router and how it will interact with the rest of the system.

// TO_REVIEW routeQuery() and clientReply() are the obvious functions to check for correctness.

// TO_REVIEW my use of mxs::QueryClassifier might be overly simple. The use should
//           be as simple as possible, but no simpler.


// TODO, missing error handling. I did not add overly many asserts, which make reading code harder.
//       But please note any that may be missing.

// TODO, for m_qc.target_is_all(), check that responses from all routers match.

// TODO Smart Query is not here yet, this is just a stupid router-router.

// COPY-PASTED error-extraction functions from rwsplit. TODO move to lib.
inline void extract_error_state(uint8_t* pBuffer, uint8_t** ppState, uint16_t* pnState)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state. In this context
    // the marker and the state itself are combined.
    *ppState = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    *pnState = 6;
}

inline void extract_error_message(uint8_t* pBuffer, uint8_t** ppMessage, uint16_t* pnMessage)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    int packet_len = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(pBuffer);

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state, followed by
    // a message until the end of the packet.
    *ppMessage = pBuffer + MYSQL_HEADER_LEN + 1 + 2 + 1 + 5;
    *pnMessage = packet_len - MYSQL_HEADER_LEN - 1 - 2 - 1 - 5;
}

std::string extract_error(GWBUF* buffer)
{
    std::string rval;

    if (MYSQL_IS_ERROR_PACKET(((uint8_t*)GWBUF_DATA(buffer))))
    {
        size_t replylen = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
        uint8_t replybuf[replylen];
        gwbuf_copy_data(buffer, 0, sizeof(replybuf), replybuf);

        uint8_t* pState;
        uint16_t nState;
        extract_error_state(replybuf, &pState, &nState);

        uint8_t* pMessage;
        uint16_t nMessage;
        extract_error_message(replybuf, &pMessage, &nMessage);

        std::string err(reinterpret_cast<const char*>(pState), nState);
        std::string msg(reinterpret_cast<const char*>(pMessage), nMessage);

        rval = err + ": " + msg;
    }

    return rval;
}

SmartRouterSession::SmartRouterSession(SmartRouter*,
                                       MXS_SESSION* pSession,
                                       Clusters clusters)
    : mxs::RouterSession(pSession)
    , m_pClient_dcb(pSession->client_dcb)
    , m_clusters(std::move(clusters))
    , m_qc(this, pSession, TYPE_ALL)
{
}

std::vector<maxbase::Host> SmartRouterSession::hosts() const
{
    std::vector<maxbase::Host> ret;
    for (const auto& c : m_clusters)
    {
        ret.push_back(c.host);
    }
    return ret;
}

SmartRouterSession::~SmartRouterSession()
{
}

// static
SmartRouterSession* SmartRouterSession::create(SmartRouter* pRouter, MXS_SESSION* pSession)
{
    Clusters clusters;

    SERVER* pMaster = pRouter->config().master();
    // TODO: Use pMaster below.

    bool is_master = true;  // TODO this will be read from config
    int master_pos = 0;     //      and this will be initialized to the position of the master

    for (SERVER_REF* ref = pRouter->service()->dbref; ref; ref = ref->next)
    {
        if (!server_ref_is_active(ref) || !ref->server->is_connectable())
        {
            continue;
        }

        mxb_assert(ref->server->is_usable());

        DCB* dcb = dcb_connect(ref->server, pSession, ref->server->protocol().c_str());
        if (dcb)
        {
            clusters.push_back({ref, dcb, is_master});
            is_master = false;      // TODO, will come from config, there must be exactly one!
        }
    }

    if (master_pos)
    {   // put the master first. There must be exactly one master cluster.
        std::swap(clusters[0], clusters[master_pos]);
    }

    SmartRouterSession* pSess = new SmartRouterSession(pRouter, pSession, std::move(clusters));

    return pSess;
}

int SmartRouterSession::routeQuery(GWBUF* pBuf)
{
    bool ret = false;

    if (expecting_request_packets())
    {
        write_split_packets(pBuf);
        if (all_clusters_are_idle())
        {
            m_mode = Mode::Idle;
        }
    }
    else if (m_mode != Mode::Idle)
    {
        auto is_busy = !all_clusters_are_idle();
        // TODO add more detail, operator<< to PacketRouter.
        MXS_SERROR("routeQuery() in wrong state. clusters busy = " << std::boolalpha << is_busy);
        mxb_assert(false);
        ret = false;
    }
    else
    {
        auto route_info = m_qc.update_route_info(mxs::QueryClassifier::CURRENT_TARGET_UNDEFINED, pBuf);
        if (m_qc.target_is_all(route_info.target()))
        {
            MXS_SDEBUG("Write all");
            ret = write_to_all(pBuf);
        }
        else if (m_qc.target_is_master(route_info.target()) || session_trx_is_active(m_pClient_dcb->session))
        {
            MXS_SDEBUG("Write to master");
            ret = write_to_master(pBuf);
        }
        else
        {
            // TODO: This is where canonical performance data will be used, and measurements initiated
            //       Currently writing to all for clientReply testing purposes.
            ret = write_to_all(pBuf);
        }
    }

    return ret;
}

void SmartRouterSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(pPacket));   // TODO, do non-contiguous for slightly better speed?

    auto it = std::find_if(begin(m_clusters), end(m_clusters),
                           [pDcb](const Cluster& cluster) {
                               return cluster.pDcb == pDcb;
                           });

    mxb_assert(it != end(m_clusters));

    Cluster& cluster = *it;

    auto tracker_state_before = cluster.tracker.state();

    cluster.tracker.update_response(pPacket);

    // these flags can all be true at the same time
    bool first_response_packet = m_mode != Mode::CollectResults;
    bool last_packet_for_this_cluster = !cluster.tracker.expecting_response_packets();
    bool very_last_response_packet = !expecting_response_packets();     // last from all clusters

    MXS_SDEBUG("Reply from " << std::boolalpha
                             << cluster.host
                             << " is_master=" << cluster.is_master
                             << " first_packet=" << first_response_packet
                             << " last_packet=" << last_packet_for_this_cluster
                             << " very_last_packet=" << very_last_response_packet
                             << " delayed_response=" << (m_pDelayed_packet != nullptr)
                             << " tracker_state: " << tracker_state_before << " => "
                             << cluster.tracker.state());

    // marker1: If a connection is lost down the pipeline, we first get an ErrorPacket, then a call to
    // handleError(). If we only rely on the handleError() the client receiving the ErrorPacket
    // can retry using this connection/session, causing a an error (or assert) in routeQuery().
    // This will change once we implement direct function calls to the Clusters (which really
    // are routers).
    if (cluster.tracker.state() == maxsql::PacketTracker::State::ErrorPacket)
    {
        auto err_code = mxs_mysql_get_mysql_errno(pPacket);
        switch (err_code)
        {
        case ER_CONNECTION_KILLED:      // there might be more error codes needing to be caught here
            MXS_SERROR("clientReply(): Lost connection to "
                       << cluster.host << " Error code=" << err_code << " "
                       << extract_error(pPacket));
            poll_fake_hangup_event(m_pClient_dcb);
            return;
        }
    }

    if (cluster.tracker.state() == maxsql::PacketTracker::State::Error)
    {
        // TODO add more info
        MXS_SERROR("ProtocolTracker error in state " << tracker_state_before);
        poll_fake_hangup_event(m_pClient_dcb);
        return;
    }

    bool will_reply = false;

    if (first_response_packet)
    {
        MXS_SDEBUG("Host " << cluster.host << " will be responding to the client");
        cluster.is_replying_to_client = true;
        m_mode = Mode::CollectResults;
        will_reply = true;      // tentatively, the packet might have to be delayed
    }

    if (very_last_response_packet)
    {
        will_reply = true;
        m_mode = Mode::Idle;
        mxb_assert(cluster.is_replying_to_client || m_pDelayed_packet);
        if (m_pDelayed_packet)
        {
            MXS_SDEBUG("Picking up delayed packet, discarding response from" << cluster.host);
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
        MXS_SDEBUG("Discarding response from " << cluster.host);
        gwbuf_free(pPacket);
    }

    if (will_reply)
    {
        MXS_SDEBUG("Forward response to client");
        MXS_SESSION_ROUTE_REPLY(pDcb->session, pPacket);
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

    return cluster.pDcb->func.write(cluster.pDcb, pBuf);
}

bool SmartRouterSession::write_to_host(const maxbase::Host& host, GWBUF* pBuf)
{
    auto it = std::find_if(begin(m_clusters), end(m_clusters), [host](const Cluster& cluster) {
                               return cluster.host == host;
                           });
    mxb_assert(it != end(m_clusters));
    auto& cluster = *it;
    cluster.tracker = maxsql::PacketTracker(pBuf);
    if (cluster.tracker.expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    cluster.is_replying_to_client = false;

    return cluster.pDcb->func.write(cluster.pDcb, pBuf);
}

bool SmartRouterSession::write_to_all(GWBUF* pBuf)
{
    for (auto it = begin(m_clusters); it != end(m_clusters); ++it)
    {
        auto& cluster = *it;
        cluster.tracker = maxsql::PacketTracker(pBuf);
        cluster.is_replying_to_client = false;
        auto pBuf_send = (std::next(it) == end(m_clusters)) ? pBuf : gwbuf_clone(pBuf);
        cluster.pDcb->func.write(cluster.pDcb, pBuf_send);
    }

    if (expecting_response_packets())
    {
        m_mode = Mode::Query;
    }

    return true;    // TODO. What could possibly go wrong?
}

bool SmartRouterSession::write_split_packets(GWBUF* pBuf)
{
    std::vector<Cluster*> active;

    for (auto it = begin(m_clusters); it != end(m_clusters); ++it)
    {
        if (it->tracker.expecting_request_packets())
        {
            active.push_back(&*it);
        }
    }

    for (auto it = begin(active); it != end(active); ++it)
    {
        auto& cluster = **it;
        auto pBuf_send = (std::next(it) == end(active)) ? pBuf : gwbuf_clone(pBuf);
        cluster.pDcb->func.write(cluster.pDcb, pBuf_send);
    }

    return true;    // TODO. What could possibly go wrong?
}

void SmartRouterSession::handleError(GWBUF* pPacket,
                                     DCB* pProblem,
                                     mxs_error_action_t action,
                                     bool* pSuccess)
{
    // One of the clusters closed the connection, in terms of SmartRouter this is a hopeless situation.
    // Close the shop, and let the client retry. Also see marker1.
    auto it = std::find_if(begin(m_clusters), end(m_clusters),
                           [pProblem](const Cluster& cluster) {
                               return cluster.pDcb == pProblem;
                           });

    mxb_assert(it != end(m_clusters));
    Cluster& cluster = *it;
    // TODO: Will the session close gracefully, or is some more checking needed here.

    auto err_code = mxs_mysql_get_mysql_errno(pPacket);
    MXS_SERROR("handleError(): Lost connection to " << cluster.host << " Error code=" << err_code << " "
                                                    << extract_error(pPacket));

    MXS_SESSION* pSession = pProblem->session;
    mxs_session_state_t sesstate = pSession->state;

    /* Send error report to client */
    GWBUF* pCopy = gwbuf_clone(pPacket);
    if (pCopy)
    {
        DCB* pClient = pSession->client_dcb;
        pClient->func.write(pClient, pCopy);
    }


    // This will lead to the rest of the connections to be closed.
    *pSuccess = false;
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

void SmartRouterSession::close()
{
    for (auto& cluster : m_clusters)
    {
        if (cluster.pDcb)
        {
            dcb_close(const_cast<DCB*>(cluster.pDcb));
        }
    }
}
