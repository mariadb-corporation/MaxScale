/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "replicator.hh"

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>
#include <mutex>
#include <condition_variable>

#include <mariadb_rpl.h>
#include <errmsg.h>

#include <maxscale/query_classifier.hh>
#include <maxscale/buffer.hh>
#include <maxscale/routingworker.hh>

// Private headers
#include "sql.hh"


using std::chrono::duration_cast;
using Clock = std::chrono::steady_clock;
using Timepoint = Clock::time_point;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace
{
std::vector<cdc::Server> service_to_servers(SERVICE* service)
{
    std::vector<cdc::Server> servers;

    // Since this isn't a worker thread, execute it on one
    mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN)->call(
        [&]() {
            for (auto s : service->reachable_servers())
            {
                if (s->is_master())
                {
                    // TODO: per-server credentials aren't exposed in the public class
                    const auto& cfg = *service->config();
                    servers.push_back({s->address(), s->port(), cfg.user, cfg.password});
                }
            }
        }, mxs::RoutingWorker::EXECUTE_AUTO);

    return servers;
}
}

namespace cdc
{


// A very small daemon. The main class that drives the whole replication process
class Replicator::Imp
{
public:
    Imp& operator=(Imp&) = delete;
    Imp(Imp&) = delete;

    // Flag used in GTID events to signal statements that perform an implicit commit
    static constexpr int IMPLICIT_COMMIT_FLAG = 0x1;

    // Creates a new replication stream and starts it
    Imp(const Config& cnf, SRowEventHandler handler);

    // Check if the replicator is still OK
    bool ok() const;

    ~Imp();

private:
    static const std::string STATEFILE_DIR;
    static const std::string STATEFILE_NAME;
    static const std::string STATEFILE_TMP_SUFFIX;

    bool connect();
    void process_events();
    void update_gtid();
    bool process_one_event(SQL::Event& event);
    bool load_gtid_state();
    void save_gtid_state() const;
    bool is_owner() const;
    void wait();

    Config               m_cnf;                     // The configuration the stream was started with
    std::unique_ptr<SQL> m_sql;                     // Database connection
    std::atomic<bool>    m_running {true};          // Whether the stream is running
    std::atomic<bool>    m_should_stop {false};     // Set to true when doing a controlled shutdown
    std::atomic<bool>    m_safe_to_stop {false};    // Whether it safe to stop the processing
    std::string          m_gtid;                    // GTID position to start from
    std::string          m_current_gtid;            // GTID of the transaction being processed
    bool                 m_implicit_commit {false}; // Commit after next query event
    Rpl                  m_rpl;                     // Class that handles the replicated events

    std::mutex              m_lock;
    std::condition_variable m_cv;

    // NOTE: must be declared last
    std::thread m_thr;      // Thread that receives the replication events
};

const std::string Replicator::Imp::STATEFILE_DIR = "./";
const std::string Replicator::Imp::STATEFILE_NAME = "current_gtid.txt";
const std::string Replicator::Imp::STATEFILE_TMP_SUFFIX = ".tmp";

Replicator::Imp::Imp(const Config& cnf, SRowEventHandler handler)
    : m_cnf(cnf)
    , m_gtid(cnf.gtid)
    , m_rpl(cnf.service, std::move(handler), cnf.match, cnf.exclude)
    , m_thr(std::thread(&Imp::process_events, this))
{
}

bool Replicator::Imp::ok() const
{
    return m_running;
}

bool Replicator::Imp::connect()
{
    cdc::Server old_server = {};
    auto servers = service_to_servers(m_cnf.service);

    if (m_sql)
    {
        old_server = m_sql->server();

        if (!m_sql->errnum())
        {
            for (const auto& a : servers)
            {
                if (a.host == old_server.host && a.port == old_server.port)
                {
                    // We already have a connection
                    return true;
                }
            }
        }

        m_sql.reset();
    }

    bool rval = false;
    std::string err;

    std::tie(err, m_sql) = SQL::connect(servers, m_cnf.timeout, m_cnf.timeout);

    if (!err.empty())
    {
        if (!servers.empty())
        {
            // We had a valid master candidate but we couldn't connect to it
            MXB_ERROR("%s", err.c_str());
        }
    }
    else
    {
        mxb_assert(m_sql);
        std::string gtid_start_pos = "SET @slave_connect_state='" + m_gtid + "'";

        // Queries required to start GTID replication
        std::vector<std::string> queries =
        {
            "SET @master_heartbeat_period=1000000000",
            "SET @master_binlog_checksum = @@global.binlog_checksum",
            "SET @mariadb_slave_capability=4",
            gtid_start_pos,
            "SET @slave_gtid_strict_mode=1",
            "SET @slave_gtid_ignore_duplicates=1",
            "SET NAMES latin1"
        };

        if (!m_sql->query(queries))
        {
            MXB_ERROR("Failed to prepare connection: %s", m_sql->error().c_str());
        }
        else if (!m_sql->replicate(m_cnf.server_id))
        {
            MXB_ERROR("Failed to open replication channel: %s", m_sql->error().c_str());
        }
        else
        {
            if (old_server.host != m_sql->server().host || old_server.port != m_sql->server().port)
            {
                MXB_NOTICE("Started replicating from [%s]:%d at GTID '%s'", m_sql->server().host.c_str(),
                           m_sql->server().port, m_gtid.c_str());
            }
            rval = true;

            m_rpl.set_server(m_sql->server());
        }
    }

    if (!rval)
    {
        m_sql.reset();
    }

    return rval;
}

void Replicator::Imp::update_gtid()
{
    auto gtid = m_rpl.load_gtid();

    if (!gtid.empty())
    {
        m_rpl.set_gtid(gtid);
        m_gtid = gtid.to_string();
    }
    else if (!m_gtid.empty())
    {
        m_rpl.set_gtid(gtid_pos_t::from_string(m_gtid));
    }
}

bool Replicator::Imp::is_owner() const
{
    bool is_owner = true;

    if (m_cnf.cooperate)
    {
        mxs::MainWorker::get()->call(
            [&]() {
                if (const auto* cluster = m_cnf.service->cluster())
                {
                    is_owner = cluster->is_running() && cluster->is_cluster_owner();
                }
            }, mxs::MainWorker::EXECUTE_AUTO);
    }

    return is_owner;
}

void Replicator::Imp::wait()
{
    std::unique_lock<std::mutex> guard(m_lock);
    m_cv.wait_for(guard, seconds(5));
}

void Replicator::Imp::process_events()
{
    bool was_active = true;
    pthread_setname_np(m_thr.native_handle(), "cdc::Replicator");

    if (!load_gtid_state())
    {
        m_running = false;
    }

    qc_thread_init(QC_INIT_BOTH);

    m_rpl.load_metadata(m_cnf.statedir);
    update_gtid();

    while (m_running)
    {
        if (!is_owner())
        {
            if (was_active)
            {
                was_active = false;
                MXB_NOTICE("Cluster used by service '%s' lost ownership.", m_cnf.service->name());
            }

            m_sql.reset();
            wait();
            continue;
        }

        if (!was_active)
        {
            // Update the latest GTID position and reconnect to the database
            update_gtid();
            m_sql.reset();
            was_active = true;
            MXB_NOTICE("Cluster used by service '%s' gained ownership.", m_cnf.service->name());
        }

        if (!connect())
        {
            if (m_should_stop)
            {
                break;
            }

            // We failed to connect to any of the servers, try again in a few seconds
            wait();
            continue;
        }

        auto event = m_sql->fetch_event();

        if (event)
        {
            if (!process_one_event(event))
            {
                /**
                 * Fatal error encountered. Fixing it might require manual intervention so
                 * the safest thing to do is to stop processing data.
                 */
                m_running = false;
            }
        }
        else if (m_sql->errnum() == CR_SERVER_LOST)
        {
            if (m_should_stop)
            {
                if (m_current_gtid == m_gtid)
                {
                    /**
                     * The latest committed GTID points to the current GTID being processed,
                     * no transaction in progress.
                     */
                    m_safe_to_stop = true;
                }
                else
                {
                    MXB_WARNING("Lost connection to server '%s:%d' when processing GTID '%s' while a "
                                "controlled shutdown was in progress. Attempting to roll back partial "
                                "transactions.", m_sql->server().host.c_str(), m_sql->server().port,
                                m_current_gtid.c_str());
                    m_running = false;
                }
            }

            // The network error will be detected at the start of the next round
        }
        else
        {
            // If we don't have an error, the server stopped the replication stream with an EOF packet.
            if (m_sql->errnum())
            {
                MXB_ERROR("Failed to read replicated event: %d, %s", m_sql->errnum(), m_sql->error().c_str());
            }

            // Close the connection and reconnect after waiting for a while.
            m_sql.reset();
            wait();
        }

        if (m_safe_to_stop)
        {
            MXB_NOTICE("Stopped at GTID '%s'", m_gtid.c_str());
            break;
        }
    }

    qc_thread_end(QC_INIT_BOTH);
}

std::string to_gtid_string(const MARIADB_RPL_EVENT& event)
{
    std::stringstream ss;
    ss << event.event.gtid.domain_id << '-' << event.server_id << '-' << event.event.gtid.sequence_nr;
    return ss.str();
}

bool Replicator::Imp::load_gtid_state()
{
    bool rval = false;
    std::string filename = m_cnf.statedir + "/" + STATEFILE_NAME;
    std::ifstream statefile(filename);
    std::string gtid;
    statefile >> gtid;

    if (statefile)
    {
        rval = true;

        if (!gtid.empty())
        {
            m_gtid = gtid;
            MXB_NOTICE("Continuing from GTID '%s'", m_gtid.c_str());
        }
    }
    else
    {
        if (errno == ENOENT)
        {
            //  No GTID file, use the GTID provided in the configuration
            rval = true;
        }
        else
        {
            MXB_ERROR("Failed to load current GTID state from file '%s': %d, %s",
                      filename.c_str(), errno, mxb_strerror(errno));
        }
    }

    return rval;
}

void Replicator::Imp::save_gtid_state() const
{
    std::ofstream statefile(m_cnf.statedir + "/" + STATEFILE_NAME);
    statefile << m_current_gtid << std::endl;

    if (!statefile)
    {
        MXB_ERROR("Failed to store current GTID state inside '%s': %d, %s",
                  m_cnf.statedir.c_str(), errno, mxb_strerror(errno));
    }
}

bool Replicator::Imp::process_one_event(SQL::Event& event)
{
    bool commit = false;

    switch (event->event_type)
    {
    case ROTATE_EVENT:
        if (m_should_stop)
        {
            // Rotating to a new binlog file, a safe place to stop
            m_safe_to_stop = true;
        }
        break;

    case GTID_EVENT:
        if (m_should_stop)
        {
            // Start of a new transaction, a safe place to stop
            m_safe_to_stop = true;
            break;
        }
        else if (event->event.gtid.flags & IMPLICIT_COMMIT_FLAG)
        {
            m_implicit_commit = true;
        }

        m_current_gtid = to_gtid_string(*event);
        MXB_INFO("GTID: %s", m_current_gtid.c_str());
        break;

    case XID_EVENT:
        commit = true;
        MXB_INFO("XID for GTID '%s': %lu", m_current_gtid.c_str(), event->event.xid.transaction_nr);

        if (m_should_stop)
        {
            // End of a transaction, a safe place to stop
            m_safe_to_stop = true;
        }
        break;

    case QUERY_EVENT:
        if (strncasecmp(event->event.query.statement.str, "commit",
                        event->event.query.statement.length) == 0)
        {
            commit = true;
        }

    /* fallthrough */
    case USER_VAR_EVENT:
        if (m_implicit_commit)
        {
            m_implicit_commit = false;
            commit = true;
        }
        break;

    case HEARTBEAT_EVENT:
        if (m_should_stop)
        {
            m_safe_to_stop = true;
        }
        break;

    default:
        // Ignore the event
        break;
    }

    bool rval = true;
    REP_HEADER hdr;
    uint8_t* ptr = m_sql->event_data() + 20;

    MARIADB_RPL_EVENT& ev = *event;
    hdr.event_size = ev.event_length + (m_rpl.have_checksums() ? 4 : 0);
    hdr.event_type = ev.event_type;
    hdr.flags = ev.flags;
    hdr.next_pos = ev.next_event_pos;
    hdr.ok = ev.ok;
    hdr.payload_len = hdr.event_size + 4;
    hdr.seqno = 0;
    hdr.serverid = ev.server_id;
    hdr.timestamp = ev.timestamp;

    m_rpl.handle_event(hdr, ptr);

    if (commit)
    {
        m_rpl.flush();
        m_gtid = m_current_gtid;
        save_gtid_state();
    }

    return rval;
}

Replicator::Imp::~Imp()
{
    m_should_stop = true;
    m_cv.notify_one();
    m_thr.join();
}

//
// The public API
//

// static
std::unique_ptr<Replicator> Replicator::start(const Config& cnf, SRowEventHandler handler)
{
    return std::unique_ptr<Replicator>(new Replicator(cnf, std::move(handler)));
}

bool Replicator::ok() const
{
    return m_imp->ok();
}

Replicator::~Replicator()
{
}

Replicator::Replicator(const Config& cnf, SRowEventHandler handler)
    : m_imp(new Imp(cnf, std::move(handler)))
{
}
}
