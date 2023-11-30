/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
#include <fcntl.h>
#include <unistd.h>

#include <maxbase/threadpool.hh>
#include <maxscale/buffer.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/secrets.hh>

// Private headers
#include "sql.hh"


using std::chrono::duration_cast;
using Clock = std::chrono::steady_clock;
using Timepoint = Clock::time_point;
using std::chrono::milliseconds;
using std::chrono::seconds;

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

    void rotate();

    std::string gtid_pos() const
    {
        std::lock_guard guard(m_lock);
        return gtid_list_to_string(m_gtid_position);
    }

    SERVER* target() const
    {
        std::lock_guard guard(m_lock);
        return m_sql ? m_sql->server().server : nullptr;
    }

    ~Imp();

private:
    using GtidList = std::map<uint64_t, gtid_pos_t>;

    static const std::string STATEFILE_DIR;
    static const std::string STATEFILE_NAME;
    static const std::string STATEFILE_TMP_SUFFIX;

    void update_server_status();
    bool connect();
    void process_events();
    void update_gtid();
    void query_gtid();
    bool process_one_event(SQL::Event& event);
    bool load_gtid_state();
    void save_gtid_state() const;
    void wait();

    static GtidList    parse_gtid_list(const std::string& gtid_list_str);
    static std::string gtid_list_to_string(const GtidList& gtid_list);

    Config               m_cnf;                     // The configuration the stream was started with
    std::unique_ptr<SQL> m_sql;                     // Database connection
    std::atomic<bool>    m_running {true};          // Whether the stream is running
    std::atomic<bool>    m_should_stop {false};     // Set to true when doing a controlled shutdown
    std::atomic<bool>    m_safe_to_stop {false};    // Whether it safe to stop the processing
    std::atomic<bool>    m_should_rotate {false};   // If true, rotate the file when it's safe to do so
    GtidList             m_gtid_position;           // Committed GTID position
    gtid_pos_t           m_current_gtid;            // GTID of the transaction being processed
    bool                 m_implicit_commit {false}; // Commit after next query event
    Rpl                  m_rpl;                     // Class that handles the replicated events
    int                  m_state_fd {-1};           // File handle to GTID state file
    std::atomic<bool>    m_is_owner {true};
    bool                 m_warn_no_cluster {true};

    // Access to this is protected by m_lock
    std::vector<cdc::Server> m_servers;

    mutable std::mutex      m_lock;
    std::condition_variable m_cv;

    // NOTE: must be declared last
    std::thread m_thr;      // Thread that receives the replication events
};

const std::string Replicator::Imp::STATEFILE_DIR = "./";
const std::string Replicator::Imp::STATEFILE_NAME = "current_gtid.txt";
const std::string Replicator::Imp::STATEFILE_TMP_SUFFIX = ".tmp";

Replicator::Imp::Imp(const Config& cnf, SRowEventHandler handler)
    : m_cnf(cnf)
    , m_gtid_position(parse_gtid_list(cnf.gtid))    // The config value could contain multiple gtids.
    , m_rpl(cnf.service, std::move(handler), cnf.match, cnf.exclude)
    , m_thr(std::thread(&Imp::process_events, this))
{
    mxb::set_thread_name(m_thr, "Replicator");
}

bool Replicator::Imp::ok() const
{
    return m_running;
}

void Replicator::Imp::rotate()
{
    m_should_rotate.store(true, std::memory_order_relaxed);
}

void Replicator::Imp::update_server_status()
{
    mxb_assert(mxs::MainWorker::is_current());

    bool owner = true;

    if (m_cnf.cooperate)
    {
        if (auto* cluster = m_cnf.service->cluster())
        {
            owner = cluster->is_running() && cluster->is_cluster_owner();
            m_warn_no_cluster = true;
        }
        else if (m_warn_no_cluster)
        {
            MXB_WARNING("Service '%s' is using 'cooperative_replication' but it does not use 'cluster', "
                        "disabling 'cooperative_replication' until 'cluster' is configured.",
                        m_cnf.service->name());
            m_warn_no_cluster = false;
        }
    }

    m_is_owner.store(owner);

    // TODO: per-server credentials aren't exposed in the public class
    SERVICE* service = m_cnf.service;
    const auto& cfg = *service->config();
    auto pw = mxs::decrypt_password(cfg.password);

    std::unique_lock<std::mutex> guard(m_lock);
    m_servers.clear();

    for (auto s : service->reachable_servers())
    {
        if (s->is_master() || status_is_blr(s->status()))
        {
            // TODO: per-server credentials aren't exposed in the public class
            m_servers.push_back({s, cfg.user, pw});
        }
    }
}

bool Replicator::Imp::connect()
{
    std::unique_lock<std::mutex> guard(m_lock);
    cdc::Server old_server = {};

    if (m_sql)
    {
        old_server = m_sql->server();

        if (!m_sql->errnum())
        {
            for (const auto& a : m_servers)
            {
                if (a.server == old_server.server)
                {
                    // We already have a connection
                    return true;
                }
            }
        }

        m_sql.reset();
    }

    auto servers = m_servers;
    guard.unlock();

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

        if (m_gtid_position.empty())
        {
            query_gtid();
        }

        std::string gtid_list_str = gtid_list_to_string(m_gtid_position);
        std::string gtid_start_pos = "SET @slave_connect_state='" + gtid_list_str + "'";

        // Queries required to start GTID replication
        std::vector<std::string> queries =
        {
            "SET @master_heartbeat_period=1000000000",
            "SET @master_binlog_checksum = @@global.binlog_checksum",
            "SET @mariadb_slave_capability=4",
            gtid_start_pos,
            "SET @slave_gtid_strict_mode=1",
            "SET @slave_gtid_ignore_duplicates=1",
            "SET NAMES latin1",
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
            if (old_server.server != m_sql->server().server)
            {
                MXB_NOTICE("Started replicating from '%s' at GTID '%s'", m_sql->server().server->name(),
                           gtid_list_str.c_str());
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
    // This allows the concrete implementation to load a custom GTID (e.g. from Kafka)
    auto impl_gtid = m_rpl.load_gtid();

    if (!impl_gtid.empty())
    {
        // Implementation loaded a GTID, discard the one read from file.
        m_rpl.set_gtid(impl_gtid);
        std::lock_guard guard(m_lock);
        m_gtid_position = parse_gtid_list(impl_gtid.to_string());
    }
    else if (!m_gtid_position.empty())
    {
        // Implementation did not provide a GTID, use the stored one. Rpl only tracks one domain.
        m_rpl.set_gtid(m_gtid_position.begin()->second);
    }
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

    // Load the stored GTID to continue where MaxScale previously left off.
    if (!load_gtid_state())
    {
        m_running = false;
    }

    mxs::CachingParser::thread_init();
    auto& parser = MariaDBParser::get();

    parser.plugin().thread_init();

    m_rpl.load_metadata(m_cnf.statedir);
    update_gtid();

    mxs::MainWorker* main_worker = mxs::MainWorker::get();
    mxs::MainWorker::DCId dcid;
    mxb::Worker::Callable callable(main_worker);

    main_worker->call([&](){
        // Get the initial servers before we try to connect
        update_server_status();

        // Also request the servers to be updated once per second
        dcid = callable.dcall(1s, [this](){
            update_server_status();
            return true;
        });
    });

    while (m_running)
    {
        if (!m_is_owner)
        {
            if (was_active)
            {
                was_active = false;
                MXB_NOTICE("Cluster used by service '%s' lost ownership.", m_cnf.service->name());
            }

            if (m_should_stop)
            {
                break;
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
            auto it = m_gtid_position.find(m_current_gtid.domain);
            if (it != m_gtid_position.end() && m_current_gtid.is_equal(it->second))
            {
                /**
                 * The latest committed GTID points to the current GTID being processed,
                 * no transaction in progress.
                 */
                m_safe_to_stop = true;
            }
            else if (m_should_stop)
            {
                MXB_WARNING("Lost connection to server '%s' when processing GTID '%s' while a "
                            "controlled shutdown was in progress. Attempting to roll back partial "
                            "transactions.", m_sql->server().server->name(),
                            m_current_gtid.to_string().c_str());
                m_running = false;
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

        if (m_should_stop && m_safe_to_stop)
        {
            MXB_NOTICE("Stopped at GTID '%s'", gtid_list_to_string(m_gtid_position).c_str());
            break;
        }
        else if (m_safe_to_stop && m_should_rotate.exchange(false, std::memory_order_relaxed))
        {
            m_rpl.rotate_files();
        }
    }

    main_worker->call([&](){
        callable.cancel_dcall(dcid);
    });

    if (m_state_fd != -1)
    {
        close(m_state_fd);
        m_state_fd = -1;
    }

    parser.plugin().thread_end();
    mxs::CachingParser::thread_finish();
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
    int fd = open(filename.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    if (fd != -1)
    {
        m_state_fd = fd;
        char gtid[4096];
        int n = pread(m_state_fd, gtid, sizeof(gtid) - 1, 0);

        if (n != -1)
        {
            gtid[n] = '\0';
            rval = true;

            if (*gtid)
            {
                std::lock_guard guard(m_lock);
                m_gtid_position = parse_gtid_list(gtid);
                MXB_NOTICE("Continuing from GTID '%s'", gtid);
            }
        }
        else
        {
            MXB_ERROR("Failed to load current GTID state from file '%s': %d, %s",
                      filename.c_str(), errno, mxb_strerror(errno));
        }
    }
    else
    {
        MXB_ERROR("Failed to open GTID state file '%s': %d, %s",
                  filename.c_str(), errno, mxb_strerror(errno));
    }

    return rval;
}

void Replicator::Imp::save_gtid_state() const
{
    std::string s = gtid_list_to_string(m_gtid_position);

    // Include the null terminating character in the data. This way we can construct a std::string directly
    // from the buffer that contains the data.
    if (pwrite(m_state_fd, s.c_str(), s.size() + 1, 0) == -1)
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
        // Rotating to a new binlog file, a safe place to stop
        m_safe_to_stop = true;
        break;

    case GTID_EVENT:
        // Start of a new transaction, a safe place to stop
        m_safe_to_stop = true;

        if (m_should_stop)
        {
            break;
        }
        else if (event->event.gtid.flags & IMPLICIT_COMMIT_FLAG)
        {
            m_implicit_commit = true;
        }

        m_current_gtid.parse(to_gtid_string(*event).c_str());
        MXB_INFO("GTID: %s", m_current_gtid.to_string().c_str());
        break;

    case XID_EVENT:
        commit = true;
        MXB_INFO("XID for GTID '%s': %lu",
                 m_current_gtid.to_string().c_str(), event->event.xid.transaction_nr);

        // End of a transaction, a safe place to stop
        m_safe_to_stop = true;
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
        m_safe_to_stop = true;
        break;

    default:
        // Ignore the event
        break;
    }

    bool rval = true;
    REP_HEADER hdr;
    size_t ev_offset = 20;
    uint8_t* ptr = event->raw_data + ev_offset;

    MARIADB_RPL_EVENT& ev = *event;
    hdr.event_size = ev.event_length - (m_rpl.have_checksums() ? 4 : 0);
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
        std::unique_lock guard(m_lock);
        m_gtid_position[m_current_gtid.domain] = m_current_gtid;
        guard.unlock();
        save_gtid_state();

        m_rpl.try_rotate_files();
    }

    return rval;
}

Replicator::Imp::~Imp()
{
    m_should_stop = true;
    m_cv.notify_one();
    m_thr.join();
    mxb_assert(m_state_fd == -1);
}

Replicator::Imp::GtidList Replicator::Imp::parse_gtid_list(const std::string& gtid_list_str)
{
    GtidList rval;
    auto elems = mxb::strtok(gtid_list_str, ",");
    for (const auto& elem : elems)
    {
        auto trimmed = mxb::trimmed_copy(elem);
        if (!trimmed.empty())
        {
            auto gtid = gtid_pos_t::from_string(trimmed);

            if (!gtid.empty())
            {
                rval[gtid.domain] = gtid;
            }
        }
    }
    return rval;
}

std::string Replicator::Imp::gtid_list_to_string(const GtidList& gtid_list)
{
    std::string rval;
    std::string sep;
    for (const auto& it : gtid_list)
    {
        rval += sep + it.second.to_string();
        sep = ",";
    }
    return rval;
}

void Replicator::Imp::query_gtid()
{

    if (m_cnf.gtid == "newest")
    {
        auto res = m_sql->result("SELECT @@gtid_binlog_pos");

        if (!res.empty() && !res[0].empty() && !res[0][0].empty())
        {
            m_gtid_position = parse_gtid_list(res[0][0]);
        }
    }
    else if (m_cnf.gtid == "oldest")
    {
        auto res = m_sql->result("SHOW BINARY LOGS");

        if (!res.empty() && !res[0].empty() && !res[0][0].empty())
        {
            std::string show_events = "SHOW BINLOG EVENTS IN '" + res[0][0] + "' LIMIT 100;";

            res = m_sql->result(show_events);

            for (const auto& row : res)
            {
                if (row.size() >= 6 && row[2] == "Gtid_list")
                {
                    // The GTID list value looks like this: [0-3000-17]
                    m_gtid_position = parse_gtid_list(row[5].substr(1, row[5].size() - 2));
                    break;
                }
            }
        }
    }
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

void Replicator::rotate()
{
    m_imp->rotate();
}

std::string Replicator::gtid_pos() const
{
    return m_imp->gtid_pos();
}

SERVER* Replicator::target() const
{
    return m_imp->target();
}

Replicator::~Replicator()
{
}

Replicator::Replicator(const Config& cnf, SRowEventHandler handler)
    : m_imp(new Imp(cnf, std::move(handler)))
{
}
}
