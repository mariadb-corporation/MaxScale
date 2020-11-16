/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "smartsession.hh"
#include "smartrouter.hh"
#include "performance.hh"

#include <maxscale/modutil.hh>
#include <maxscale/mysql_plus.hh>

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

// TODO Another thing to move to maxutils
static std::array<const char*, 7> size_suffix {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
constexpr size_t KiloByte {1024};

/** return a pair {double_value, suffix} for humanizing a size.
 *  Example: pretty_size_split(2000) => {1.953125, "KB"}
 */
std::pair<double, const char*> pretty_size_split(size_t sz)
{
    double dsize = sz;
    size_t i {0};

    for (; i < size_suffix.size() && dsize >= KiloByte; ++i, dsize /= KiloByte)
    {
    }

    return {dsize, size_suffix[i]};
}

/** Pretty string from a size_t, e.g pretty_size(2000) => "1.95KB"
 */
std::string pretty_size(size_t sz, const char* separator = "")
{
    char buf[64];
    double dsize;
    const char* suffix;

    std::tie(dsize, suffix) = pretty_size_split(sz);

    // format with two decimals
    auto len = std::sprintf(buf, "%.2f", dsize);

    // remove trailing 0-decimals
    char* ptr = buf + len - 1;
    for (; *ptr == '0'; --ptr)
    {
    }
    if (*ptr != '.')
    {
        ++ptr;
    }

    // append suffix
    sprintf(ptr, "%s%s", separator, suffix);

    return buf;
}

SmartRouterSession::SmartRouterSession(SmartRouter* pRouter,
                                       MXS_SESSION* pSession,
                                       Clusters clusters)
    : mxs::RouterSession(pSession)
    , m_router(*pRouter)
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

    int master_pos = -1;
    int i = 0;

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
            bool is_master = (ref->server == pMaster);

            clusters.push_back({ref, dcb, is_master});

            if (is_master)
            {
                master_pos = i;
            }

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

    MXS_SDEBUG("routeQuery() buffer size " << pretty_size(gwbuf_length(pBuf)));

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

        m_measurement = {maxbase::Clock::now(), canonical};

        if (m_qc.target_is_all(route_info.target()))
        {
            MXS_SDEBUG("Write all");
            ret = write_to_all(pBuf, Mode::Query);
        }
        else if (m_qc.target_is_master(route_info.target()) || session_trx_is_active(m_pClient_dcb->session))
        {
            MXS_SDEBUG("Write to master");
            ret = write_to_master(pBuf);
        }
        else
        {
            auto perf = m_router.perf_find(canonical);

            if (perf.is_valid())
            {
                MXS_SDEBUG("Smart route to " << perf.host()
                                             << ", canonical = " << show_some(canonical));
                ret = write_to_host(perf.host(), pBuf);
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

void SmartRouterSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    mxb_assert(GWBUF_IS_CONTIGUOUS(pPacket));

    auto it = std::find_if(begin(m_clusters), end(m_clusters),
                           [pDcb](const Cluster& cluster) {
                               return cluster.pDcb == pDcb;
                           });

    mxb_assert(it != end(m_clusters));

    Cluster& cluster = *it;

    auto tracker_state_before = cluster.tracker.state();

    cluster.tracker.update_response(pPacket);

    // these flags can all be true at the same time
    bool first_response_packet = (m_mode == Mode::Query || m_mode == Mode::MeasureQuery);
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
    // could retry using this connection/session, causing an error (or assert) in routeQuery().
    // This will change once we implement direct function calls to the Clusters (which really
    // are routers).
    if (cluster.tracker.state() == maxsql::PacketTracker::State::ErrorPacket)
    {
        auto err_code = mxs_mysql_get_mysql_errno(pPacket);
        switch (err_code)
        {
        case ER_CONNECTION_KILLED:      // there might be more error codes needing to be caught here
            MXS_SERROR("clientReply(): Lost connection to " << cluster.host
                                                            << " Error code=" << err_code
                                                            << ' ' << extract_error(pPacket));
            poll_fake_hangup_event(m_pClient_dcb);
            return;
        }
    }

    if (cluster.tracker.state() == maxsql::PacketTracker::State::Error)
    {
        MXS_SERROR("ProtocolTracker from state " << tracker_state_before
                                                 << " to state " << cluster.tracker.state()
                                                 << ". Disconnect.");
        poll_fake_hangup_event(m_pClient_dcb);
        return;
    }

    bool will_reply = false;

    if (first_response_packet)
    {
        maxbase::Duration query_dur = maxbase::Clock::now() - m_measurement.start;
        MXS_SDEBUG("Host " << cluster.host
                           << " will be responding to the client."
                           << " First packet received in time " << query_dur);
        cluster.is_replying_to_client = true;
        will_reply = true;      // tentatively, the packet might have to be delayed

        if (m_mode == Mode::MeasureQuery)
        {
            m_router.perf_update(m_measurement.canonical, {cluster.host, query_dur});
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
            MXS_SDEBUG("Picking up delayed packet, discarding response from " << cluster.host);
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

bool SmartRouterSession::write_to_all(GWBUF* pBuf, Mode mode)
{
    bool success = true;

    for (auto it = begin(m_clusters); it != end(m_clusters); ++it)
    {
        auto& cluster = *it;
        cluster.tracker = maxsql::PacketTracker(pBuf);
        cluster.is_replying_to_client = false;
        auto pBuf_send = (next(it) == end(m_clusters)) ? pBuf : gwbuf_clone(pBuf);
        if (!cluster.pDcb->func.write(cluster.pDcb, pBuf_send))
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

    bool success = true;

    for (auto it = begin(active); it != end(active); ++it)
    {
        auto& cluster = **it;

        cluster.tracker.update_request(pBuf);

        auto pBuf_send = (next(it) == end(active)) ? pBuf : gwbuf_clone(pBuf);
        if (!cluster.pDcb->func.write(cluster.pDcb, pBuf_send))
        {
            success = false;
            break;
        }
    }

    return success;
}

void SmartRouterSession::kill_all_others(const Cluster& cluster)
{
    MySQLProtocol* proto = static_cast<MySQLProtocol*>(cluster.pDcb->protocol);
    int keep_protocol_thread_id = proto->thread_id;

    mxs_mysql_execute_kill_all_others(cluster.pDcb->session, cluster.pDcb->session->ses_id,
                                      keep_protocol_thread_id, KT_QUERY);
}

void SmartRouterSession::handleError(GWBUF* pPacket,
                                     DCB* pProblem,
                                     mxs_error_action_t action,
                                     bool* pSuccess)
{
    // One of the clusters closed the connection. In terms of SmartRouter this is a hopeless situation.
    // Close the shop, and let the client retry. Also see marker1.
    auto it = std::find_if(begin(m_clusters), end(m_clusters),
                           [pProblem](const Cluster& cluster) {
                               return cluster.pDcb == pProblem;
                           });

    mxb_assert(it != end(m_clusters));
    Cluster& cluster = *it;

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
