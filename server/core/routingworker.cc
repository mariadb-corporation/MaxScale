/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/routingworker.hh>

#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <sys/epoll.h>
#include <maxbase/atomic.hh>
#include <maxbase/average.hh>
#include <maxbase/pretty_print.hh>
#include <maxbase/semaphore.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/clock.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/maxscale.hh>
#include <maxscale/statistics.hh>
#include <maxscale/utils.hh>

#include "internal/modules.hh"
#include "internal/server.hh"
#include "internal/session.hh"

using maxbase::AverageN;
using maxbase::Semaphore;
using maxbase::Worker;
using maxbase::WorkerLoad;
using maxscale::RoutingWorker;
using maxscale::Closer;
using std::lock_guard;
using std::shared_ptr;
using std::stringstream;
using std::unique_ptr;
using std::vector;

namespace
{

/**
 * Unit variables.
 *
 * The variables related to the management of the threads are:
 *
 * nMax:        The hard maximum number of threads. Cannot be changed at runtime and
 *              currently specified using a compile time constant.
 * nRunning:    The number of running threads, when viewed from the outside. Running threads
 *              are all active and draining threads, and also inactive threads that are waiting
 *              for other draining threads to become inactive before they can be removed.
 *              The following will always hold: 1 <= nRunning <= nMax.
 * nConfigured: The configured number of threads. When a user issues 'maxctrl alter maxscale threads=N'
 *              nConfigured will be immediately set to N (subject to some possible failures), but
 *              when the number of threads is reduced nRunning will become N, only when the
 *              threads have properly been deactivated.
 *              The following will always hold: 1 <= nConfigured <= nRunning.
 *
 * Right after startup and when any thread operations have reached their conclusion, the
 * following will hold: nConfigured == nRunning.
 */
class ThisUnit
{
public:
    using WN = mxb::WatchdogNotifier;

    bool init(mxb::WatchdogNotifier* pNotifier)
    {
        mxb_assert(!this->initialized);

        this->nMax = mxs::Config::get().n_threads_max;

        bool rv = false;
        int fd = epoll_create(Worker::MAX_EVENTS);

        if (fd != -1)
        {
            std::unique_ptr<RoutingWorker* []> spWorkers(new(std::nothrow) RoutingWorker* [this->nMax]());

            if (spWorkers)
            {
                this->epoll_listener_fd = fd;
                this->ppWorkers = spWorkers.release();
                this->pNotifier = pNotifier;
                this->initialized = true;
            }
            else
            {
                close(fd);
                MXB_OOM();
            }
        }
        else
        {
            MXB_ALERT("Could not allocate an epoll instance.");
        }

        return this->initialized;
    }

    void finish()
    {
        mxb_assert(this->initialized);

        for (int i = this->nRunning - 1; i >= 0; --i)
        {
            RoutingWorker* pWorker = this->ppWorkers[i];
            mxb_assert(pWorker);
            delete pWorker;
            this->ppWorkers[i] = nullptr;
        }

        this->nRunning.store(0, std::memory_order_relaxed);
        this->nConfigured.store(0, std::memory_order_relaxed);

        delete[] this->ppWorkers;
        this->ppWorkers = nullptr;

        close(this->epoll_listener_fd);
        this->epoll_listener_fd = -1;

        this->pNotifier = nullptr;

        this->initialized = false;
    }

    bool             initialized {false};            // Whether the initialization has been performed.
    bool             running {false};                // True if worker threads are running
    int              nMax {0};                       // Hard maximum of workers
    std::atomic<int> nRunning {0};                   // "Running" amount of workers.
    std::atomic<int> nConfigured {0};                // The configured amount of workers.
    RoutingWorker**  ppWorkers {nullptr};            // Array of routing worker instances.
    int              epoll_listener_fd {-1};         // Shared epoll descriptor for listening descriptors.
    WN*              pNotifier {nullptr};            // Watchdog notifier.
    bool             termination_in_process {false}; // Is a routing worker being terminated.
} this_unit;

thread_local struct this_thread
{
    RoutingWorker* pCurrent_worker; // The worker of the current thread
} this_thread =
{
    nullptr
};

bool can_close_dcb(mxs::BackendConnection* b)
{
    mxb_assert(b->dcb()->role() == DCB::Role::BACKEND);
    const int SHOW_SHUTDOWN_TIMEOUT = 2;
    auto idle = MXS_CLOCK_TO_SEC(mxs_clock() - b->dcb()->last_read());
    return idle > SHOW_SHUTDOWN_TIMEOUT || b->can_close();
}

}

namespace maxscale
{

//static
RoutingWorker::Datas RoutingWorker::s_datas;
//static
std::mutex RoutingWorker::s_datas_lock;

RoutingWorker::Data::Data()
{
    mxb_assert(mxs::MainWorker::is_current());

    RoutingWorker::register_data(this);
}

RoutingWorker::Data::~Data()
{
    RoutingWorker::deregister_data(this);
}

void RoutingWorker::Data::initialize_workers()
{
    mxb_assert(MainWorker::is_current());

    if (RoutingWorker::is_running())
    {
        int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);

        for (int i = 0; i < nRunning; ++i)
        {
            auto* pWorker = this_unit.ppWorkers[i];

            pWorker->call([pWorker, this]() {
                    this->init_for(pWorker);
                }, mxb::Worker::EXECUTE_QUEUED);
        }
    }
}

json_t* RoutingWorker::MemoryUsage::to_json() const
{
    json_t* pMu = json_object();

    json_object_set_new(pMu, "query_classifier", json_integer(this->query_classifier));
    json_object_set_new(pMu, "zombies", json_integer(this->zombies));
    json_object_set_new(pMu, "sessions", json_integer(this->sessions));
    json_object_set_new(pMu, "total", json_integer(this->total));

    return pMu;
}

RoutingWorker::ConnPoolEntry::ConnPoolEntry(mxs::BackendConnection* pConn)
    : m_created(time(nullptr))
    , m_pConn(pConn)
{
    mxb_assert(m_pConn);
}

RoutingWorker::ConnPoolEntry::~ConnPoolEntry()
{
    mxb_assert(!m_pConn);
}

RoutingWorker::DCBHandler::DCBHandler(RoutingWorker* pOwner)
    : m_owner(*pOwner)
{
}

// Any activity on a backend DCB that is in the persistent pool, will
// cause the dcb to be evicted.
void RoutingWorker::DCBHandler::ready_for_reading(DCB* pDcb)
{
    m_owner.evict_dcb(static_cast<BackendDCB*>(pDcb));
}

void RoutingWorker::DCBHandler::write_ready(DCB* pDcb)
{
    m_owner.evict_dcb(static_cast<BackendDCB*>(pDcb));
}

void RoutingWorker::DCBHandler::error(DCB* pDcb)
{
    m_owner.evict_dcb(static_cast<BackendDCB*>(pDcb));
}

void RoutingWorker::DCBHandler::hangup(DCB* pDcb)
{
    m_owner.evict_dcb(static_cast<BackendDCB*>(pDcb));
}


RoutingWorker::RoutingWorker(int index, size_t rebalance_window)
    : mxb::WatchedWorker(this_unit.pNotifier)
    , m_index(index)
    , m_state(State::DORMANT)
    , m_listening(false)
    , m_routing(false)
    , m_callable(this)
    , m_pool_handler(this)
    , m_average_load(rebalance_window)
{
}

RoutingWorker::~RoutingWorker()
{
}

// static
bool RoutingWorker::init(mxb::WatchdogNotifier* pNotifier)
{
    mxb_assert(!this_unit.initialized);

    return this_unit.init(pNotifier);
}

// static
void RoutingWorker::finish()
{
    mxb_assert(this_unit.initialized);

    this_unit.finish();
}

//static
void RoutingWorker::register_data(Data* pData)
{
    std::unique_lock guard(s_datas_lock);

    mxb_assert(std::find(s_datas.begin(), s_datas.end(), pData) == s_datas.end());
    s_datas.push_back(pData);

    guard.unlock();
}

//static
void RoutingWorker::deregister_data(Data* pData)
{
    std::unique_lock guard(s_datas_lock);

    auto it = std::find(s_datas.begin(), s_datas.end(), pData);
    mxb_assert(it != s_datas.end());
    s_datas.erase(it);

    guard.unlock();
}

//static
bool RoutingWorker::adjust_threads(int nCount)
{
    mxb_assert(MainWorker::is_current());
    mxb_assert(this_unit.initialized);
    mxb_assert(this_unit.running);

    bool rv = false;

    int nConfigured = this_unit.nConfigured.load(std::memory_order_relaxed);

    if (nCount < 1)
    {
        MXB_ERROR("The number of threads must be at least 1.");
    }
    else if (nCount > this_unit.nMax)
    {
        MXB_ERROR("The number of threads can be at most %d.", this_unit.nMax);
    }
    else if (nCount < nConfigured)
    {
        rv = decrease_workers(nConfigured - nCount);
        mxb_assert(nCount == this_unit.nConfigured);
    }
    else if (nCount > nConfigured)
    {
        rv = increase_workers(nCount - nConfigured);
        mxb_assert(nCount == this_unit.nConfigured);
    }
    else
    {
        rv = true;
    }

    return rv;
}

void RoutingWorker::init_datas()
{
    mxb_assert(Worker::is_current());

    lock_guard guard(s_datas_lock);

    for (Data* pData : s_datas)
    {
        pData->init_for(this);
    }
}

void RoutingWorker::finish_datas()
{
    mxb_assert(Worker::is_current());

    lock_guard guard(s_datas_lock);

    for (Data* pData : s_datas)
    {
        pData->finish_for(this);
    }
}

//static
bool RoutingWorker::increase_workers(int nDelta)
{
    mxb_assert(MainWorker::is_current());
    mxb_assert(nDelta > 0);

    bool rv = true;

    int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    int nConfigured = this_unit.nConfigured.load(std::memory_order_relaxed);
    int nAvailable = nRunning - nConfigured;

    if (nAvailable > 0)
    {
        int n = std::min(nDelta, nAvailable);

        int nActivated = activate_workers(n);

        if (n == nActivated)
        {
            nDelta -= n;
        }
        else
        {
            // activate_workers(...) may change, must be fetched again.
            int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);

            MXB_ERROR("Could activate %d threads of %d required. %d workers "
                      "currently available.", nActivated, nDelta, nRunning);
            rv = false;
        }
    }

    if (rv && (nDelta != 0))
    {
        rv = create_workers(nDelta);
    }

    return rv;
}

//static
int RoutingWorker::activate_workers(int n)
{
    mxb_assert(MainWorker::is_current());
    mxb_assert(this_unit.nRunning - this_unit.nConfigured >= n);

    int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    int nBefore = this_unit.nConfigured.load(std::memory_order_relaxed);
    int i = nBefore;
    n += i;

    auto listeners = Listener::get_started_listeners();

    for (; i < n; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];

        bool success = false;
        pWorker->call([pWorker, &listeners, &success]() {
                success = pWorker->activate(listeners);
            }, Worker::EXECUTE_QUEUED);

        if (!success)
        {
            break;
        }
    }

    if (i > nRunning)
    {
        this_unit.nRunning.store(i, std::memory_order_relaxed);
    }
    this_unit.nConfigured.store(i, std::memory_order_relaxed);

    return i - nBefore;
}

bool RoutingWorker::start_listening(const std::vector<SListener>& listeners)
{
    mxb_assert(Worker::is_current());

    bool rv = false;

    for (auto sListener : listeners)
    {
        // Other listener types are handled implicitly by the routing
        // worker reacting to events on the shared routing worker fd.
        if (sListener->type() == Listener::Type::UNIQUE_TCP)
        {
            if (!sListener->listen(*this))
            {
                MXB_ERROR("Could not add listener to routing worker %d, some listeners "
                          "will not be handled by this worker.", index());
            }
        }
    }

    rv = start_polling_on_shared_fd();
    mxb_assert(rv); // Should not ever fail.

    if (rv)
    {
        set_listening(true);
    }

    return rv;
}

bool RoutingWorker::stop_listening(const std::vector<SListener>& listeners)
{
    mxb_assert(Worker::is_current());

    bool rv = true;

    for (auto sListener : listeners)
    {
        // Other listener types are handled via the stop_polling_on_shared_fd() below.
        if (sListener->type() == Listener::Type::UNIQUE_TCP)
        {
            if (!sListener->unlisten(*this))
            {
                MXB_ERROR("Could not remove listener from routing worker %d.", index());
                rv = false;
                break;
            }
        }
    }

    if (rv)
    {
        rv = stop_polling_on_shared_fd();
        mxb_assert(rv); // Should not ever fail.
    }

    if (rv)
    {
        set_listening(false);
    }

    return rv;
}

void RoutingWorker::deactivate()
{
    mxb_assert(Worker::is_current());
    mxb_assert(state() == State::ACTIVE || state() == State::DRAINING);

    set_state(State::DORMANT);
    set_routing(false);

    MainWorker* pMain = MainWorker::get();
    mxb_assert(pMain);

    auto proceed_in_main = [this]() {
        mxb_assert(MainWorker::is_current());

        if (!this_unit.termination_in_process)
        {
            auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
            if (index() == nRunning - 1)
            {
                // We are in the last last worker.
                terminate_last_if_dormant(true);
            }
        }
    };

    if (!pMain->execute(proceed_in_main, Worker::EXECUTE_QUEUED))
    {
        MXB_ERROR("Could not post cleanup function from worker (%s, %p) to Main.",
                  thread_name().c_str(), this);
    }
}

//static
void RoutingWorker::terminate_last_if_dormant(bool first_attempt)
{
    mxb_assert(MainWorker::is_current());
    mxb_assert((first_attempt && !this_unit.termination_in_process)
               || (!first_attempt && this_unit.termination_in_process));

    if (maxscale_is_shutting_down())
    {
        // Already going down, in which case there is no need for further action.
        this_unit.termination_in_process = false;
        return;
    }

    auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    mxb_assert(nRunning > 0);
    auto i = nRunning - 1;

    RoutingWorker* pWorker = this_unit.ppWorkers[i];

    if (!pWorker->is_dormant())
    {
        mxb_assert(!first_attempt);
        // Not dormant, must not proceed with the termination.
        this_unit.termination_in_process = false;
        return;
    }

    mxb_assert(i > 0); // Should not be possible to terminate the first worker.

    if (first_attempt)
    {
        this_unit.termination_in_process = true;
    }

    MXB_NOTICE("Routing worker %d is dormant and last, and will be terminated.", i);

    --nRunning;
    this_unit.nRunning.store(nRunning, std::memory_order_relaxed);
    mxb_assert(this_unit.nRunning >= this_unit.nConfigured);
    this_unit.ppWorkers[nRunning] = nullptr;

    if (!pWorker->execute([pWorker]() {
                pWorker->terminate();
            }, Worker::EXECUTE_QUEUED))
    {
        MXB_ERROR("Could not post to (%s, %p), the worker will not go down.",
                  pWorker->thread_name().c_str(), pWorker);

        this_unit.ppWorkers[nRunning] = pWorker;
        this_unit.nRunning.store(nRunning + 1, std::memory_order_relaxed);
        this_unit.termination_in_process = false;
    }
}

bool RoutingWorker::activate(const std::vector<SListener>& listeners)
{
    mxb_assert(Worker::is_current());
    mxb_assert(is_draining() || is_dormant());

    bool success = start_listening(listeners);

    if (success)
    {
        set_state(State::ACTIVE);
    }

    return success;
}

//static
bool RoutingWorker::create_workers(int n)
{
    // Not all unit tests have a MainWorker.
    mxb_assert(!Worker::get_current() || MainWorker::is_current());
    mxb_assert(n > 0);

    size_t rebalance_window = mxs::Config::get().rebalance_window.get();

    int nBefore = this_unit.nRunning.load(std::memory_order_acquire);
    int nAfter = nBefore + n;

    auto listeners = Listener::get_started_listeners();

    int i = nBefore;
    for (; i < nAfter; ++i)
    {
        unique_ptr<RoutingWorker> sWorker(RoutingWorker::create(i, rebalance_window, listeners));

        if (sWorker)
        {
            this_unit.ppWorkers[i] = sWorker.release();
        }
        else
        {
            MXB_ERROR("Could not create routing worker %d.", i);
            break;
        }
    }

    if (i != nAfter)
    {
        MXB_WARNING("Could create %d new routing workers, the number of active "
                    "routing workers is now %d.", i - nBefore, i);
        nAfter = i;
    }

    this_unit.nRunning.store(nAfter, std::memory_order_release);
    this_unit.nConfigured.store(nAfter, std::memory_order_release);

    return i != nBefore;
}

//static
bool RoutingWorker::decrease_workers(int n)
{
    mxb_assert(MainWorker::is_current());
    mxb_assert(n > 0);

    int nBefore = this_unit.nConfigured.load(std::memory_order_relaxed);
    int nAfter = nBefore - n;
    mxb_assert(nAfter > 0);

    auto listeners = Listener::get_started_listeners();

    int i = nBefore - 1;
    for (; i >= nAfter; --i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];

        bool success = false;
        pWorker->call([pWorker, &listeners, &success]() {
                // If an active worker has explicitly been made not to listen,
                // the listening need not be stopped.
                if (pWorker->is_listening())
                {
                    success = pWorker->stop_listening(listeners);
                }
                else
                {
                    success = true;
                }

                if (success)
                {
                    if (pWorker->can_deactivate())
                    {
                        pWorker->deactivate();
                    }
                    else
                    {
                        pWorker->set_state(State::DRAINING);
                    }
                }
            }, Worker::EXECUTE_QUEUED);

        if (!success)
        {
            break;
        }
    }

    ++i;
    if (i != nAfter)
    {
        MXB_WARNING("Could remove %d new routing workers, the number of active "
                    "routing workers is now %d.", nBefore - i, i);
        nAfter = i;
    }

    this_unit.nConfigured.store(nAfter, std::memory_order_relaxed);

    return i != nBefore;
}

bool RoutingWorker::start_polling_on_shared_fd()
{
    mxb_assert(Worker::is_current());
    mxb_assert(!is_listening());

    bool rv = false;

    // The shared epoll instance descriptor is *not* added using EPOLLET (edge-triggered)
    // because we want it to be level-triggered. That way, as long as there is a single
    // active (accept() can be called) listening socket, epoll_wait() will return an event
    // for it.
    if (add_pollable(EPOLLIN, this))
    {
        MXB_INFO("Epoll instance for listening sockets added to worker epoll instance.");
        rv = true;
    }
    else
    {
        MXB_ERROR("Could not add epoll instance for listening sockets to "
                  "epoll instance of worker: %s",
                  mxb_strerror(errno));
    }

    return rv;
}

bool RoutingWorker::stop_polling_on_shared_fd()
{
    mxb_assert(Worker::is_current());
    mxb_assert(m_listening);

    bool rv = remove_pollable(this);

    if (rv)
    {
        m_listening = false;
    }

    return rv;
}

// static
int RoutingWorker::nRunning()
{
    return this_unit.nRunning.load(std::memory_order_relaxed);
}

// static
bool RoutingWorker::add_listener(Listener* pListener)
{
    mxb_assert(MainWorker::is_current());

    bool rv = true;

    int fd = pListener->poll_fd();

    // This must be level-triggered (i.e. the default). Since this is
    // intended for listening sockets and each worker will call accept()
    // just once before going back the epoll_wait(), using EPOLLET would
    // mean that if there are more clients to be accepted than there are
    // threads returning from epoll_wait() for an event, then some clients
    // would be accepted only when a new client has connected, thus causing
    // a new EPOLLIN event.
    uint32_t events = EPOLLIN;
    Pollable* pPollable = pListener;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pPollable;

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

// static
bool RoutingWorker::remove_listener(Listener* pListener)
{
    mxb_assert(MainWorker::is_current());

    bool rv = true;

    int fd = pListener->poll_fd();

    struct epoll_event ev = {};

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_DEL, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

//static
RoutingWorker* RoutingWorker::get_current()
{
    return this_thread.pCurrent_worker;
}

int RoutingWorker::index() const
{
    return m_index;
}

// static
RoutingWorker* RoutingWorker::get_by_index(int index)
{
    return (index >= 0 && index < this_unit.nMax) ? this_unit.ppWorkers[index] : nullptr;
}

// static
bool RoutingWorker::start_workers(int nWorkers)
{
    // Not all unit tests have a MainWorker.
    mxb_assert(!Worker::get_current() || MainWorker::is_current());
    mxb_assert(this_unit.nRunning == 0);

    bool rv = create_workers(nWorkers);

    if (rv)
    {
        this_unit.running = true;
    }

    return rv;
}

// static
bool RoutingWorker::is_running()
{
    return this_unit.running;
}

// static
void RoutingWorker::join_workers()
{
    mxb_assert(MainWorker::is_current());

    int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    for (int i = 0; i < nRunning; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        pWorker->join();
    }

    this_unit.running = false;
}

// static
bool RoutingWorker::shutdown_complete()
{
    mxb_assert(MainWorker::is_current());

    // If a routing worker is being terminated, we must wait for that to finish.
    bool rval = !this_unit.termination_in_process;

    if (rval)
    {
        int nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
        for (int i = 0; i < nRunning; ++i)
        {
            RoutingWorker* pWorker = this_unit.ppWorkers[i];
            mxb_assert(pWorker);

            if (pWorker->event_loop_state() == Worker::EventLoop::RUNNING)
            {
                rval = false;
            }
        }
    }

    return rval;
}

RoutingWorker::SessionsById& RoutingWorker::session_registry()
{
    mxb_assert(Worker::is_current());

    return m_sessions;
}

const RoutingWorker::SessionsById& RoutingWorker::session_registry() const
{
    mxb_assert(Worker::is_current());

    return m_sessions;
}

void RoutingWorker::destroy(DCB* pDcb)
{
    mxb_assert(Worker::is_current());
    mxb_assert(pDcb->owner() == this);

    m_zombies.push_back(pDcb);
}

/**
 * If a second has passed since last keepalive tick, tick all sessions again.
 * Also checks connection pool for expired connections.
 */
void RoutingWorker::process_timeouts()
{
    mxb_assert(Worker::is_current());

    auto now = mxs_clock();
    if (now >= m_next_timeout_check)
    {
        /** Because the resolutions of the timeouts is one second, we only need to
         * check them once per second. One heartbeat is 100 milliseconds. */
        m_next_timeout_check = now + 10;

        for (auto& elem : m_sessions)
        {
            auto* pSes = static_cast<Session*>(elem.second);
            ClientDCB* pClient = pSes->client_dcb;
            if (pClient->state() == DCB::State::POLLING)
            {
                auto idle = now - std::max(pClient->last_read(), pClient->last_write());
                pSes->tick(MXS_CLOCK_TO_SEC(idle));
            }
        }
    }
}

void RoutingWorker::delete_zombies()
{
    mxb_assert(Worker::is_current());

    Zombies slow_zombies;
    // An algorithm cannot be used, as the final closing of a DCB may cause
    // other DCBs to be registered in the zombie queue.

    while (!m_zombies.empty())
    {
        DCB* pDcb = m_zombies.back();
        m_zombies.pop_back();
        MXS_SESSION::Scope scope(pDcb->session());

        bool can_close = true;

        if (pDcb->role() == DCB::Role::CLIENT)
        {
            // Check if any of the backend DCBs isn't ready to be closed. If so, delay the closing of the
            // client DCB until the backend connections have fully established and finished authenticating.
            const auto& dcbs = static_cast<Session*>(pDcb->session())->backend_connections();
            can_close = std::all_of(dcbs.begin(), dcbs.end(), can_close_dcb);
        }

        if (can_close)
        {
            MXB_DEBUG("Ready to close session %lu", pDcb->session() ? pDcb->session()->id() : 0);
            DCB::Manager::call_destroy(pDcb);
        }
        else
        {
            MXB_DEBUG("Delaying destruction of session %lu", pDcb->session() ? pDcb->session()->id() : 0);
            slow_zombies.push_back(pDcb);
        }
    }

    mxb_assert(m_zombies.empty());
    m_zombies.insert(m_zombies.end(), slow_zombies.begin(), slow_zombies.end());
}

void RoutingWorker::add(DCB* pDcb)
{
    mxb_assert(Worker::is_current());

    MXB_AT_DEBUG(auto rv = ) m_dcbs.insert(pDcb);
    mxb_assert(rv.second);
}

void RoutingWorker::remove(DCB* pDcb)
{
    mxb_assert(Worker::is_current());

    auto it = m_dcbs.find(pDcb);
    mxb_assert(it != m_dcbs.end());
    m_dcbs.erase(it);
}

RoutingWorker::ConnectionResult
RoutingWorker::get_backend_connection(SERVER* pSrv, MXS_SESSION* pSes, mxs::Component* pUpstream)
{
    mxb_assert(Worker::is_current());

    auto* pServer = static_cast<Server*>(pSrv);
    auto* pSession = static_cast<Session*>(pSes);

    if (pServer->persistent_conns_enabled() && pServer->is_running())
    {
        auto* pPool_conn = pool_get_connection(pSrv, pSession, pUpstream);
        if (pPool_conn)
        {
            // Connection found from pool, return it.
            return {false, pPool_conn};
        }
    }

    ConnectionResult rval;
    const auto max_allowed_conns = pServer->max_routing_connections();
    auto& stats = pServer->stats();

    if (max_allowed_conns > 0)
    {
        // Server has a connection count limit. Check that we are not already at the limit.
        auto curr_conns = stats.n_current_conns() + stats.n_conn_intents();
        if (curr_conns >= max_allowed_conns)
        {
            // Looks like all connection slots are in use. This may be pessimistic in case an intended
            // connection fails in another thread.
            rval.conn_limit_reached = true;
        }
        else
        {
            // Mark intent, then read current conn value again. This is not entirely accurate, but does
            // avoid overshoot (assuming memory orderings are correct).
            auto intents = stats.add_conn_intent();
            if (intents + stats.n_current_conns() <= max_allowed_conns)
            {
                auto pNew_conn = pSession->create_backend_connection(pServer, this, pUpstream);
                if (pNew_conn)
                {
                    stats.add_connection();
                    rval.conn = pNew_conn;
                }
            }
            else
            {
                rval.conn_limit_reached = true;
            }
            stats.remove_conn_intent();
        }
    }
    else
    {
        // No limit, just create new connection.
        auto* pNew_conn = pSession->create_backend_connection(pServer, this, pUpstream);
        if (pNew_conn)
        {
            stats.add_connection();
            rval.conn = pNew_conn;
        }
    }

    return rval;
}

std::pair<uint64_t, mxs::BackendConnection*>
RoutingWorker::ConnectionPool::get_connection(MXS_SESSION* session)
{
    mxs::BackendConnection* rval = nullptr;
    uint64_t best_reuse = mxs::BackendConnection::REUSE_NOT_POSSIBLE;
    auto best = m_contents.end();

    for (auto it = m_contents.begin(); it != m_contents.end(); ++it)
    {
        auto current_reuse = it->first->can_reuse(session);

        if (current_reuse > best_reuse)
        {
            best = it;
            best_reuse = current_reuse;

            if (current_reuse == mxs::BackendConnection::OPTIMAL_REUSE)
            {
                break;
            }
        }
    }

    if (best != m_contents.end())
    {
        rval = best->second.release_conn();
        m_contents.erase(best);
        m_stats.times_found++;
    }
    else
    {
        m_stats.times_empty++;
    }

    return {best_reuse, rval};
}

void RoutingWorker::ConnectionPool::set_capacity(int global_capacity)
{
    // Capacity has changed, recalculate local capacity.
    m_capacity = global_capacity / this_unit.nRunning.load(std::memory_order_relaxed);
}

mxs::BackendConnection*
RoutingWorker::pool_get_connection(SERVER* pSrv, MXS_SESSION* pSes, mxs::Component* pUpstream)
{
    std::lock_guard<std::mutex> guard(m_pool_lock);

    auto pServer = static_cast<Server*>(pSrv);
    mxb_assert(pServer);
    auto pSession = static_cast<Session*>(pSes);
    bool proxy_protocol = pServer->proxy_protocol();
    mxs::BackendConnection* pFound_conn = nullptr;

    auto it = m_pool_group.find(pServer);
    if (it != m_pool_group.end())
    {
        ConnectionPool& conn_pool = it->second;

        while (!pFound_conn)
        {
            auto [reuse, pCandidate] = conn_pool.get_connection(pSession);

            // If no candidate could be found, stop right away.
            if (!pCandidate)
            {
                break;
            }

            BackendDCB* pDcb = pCandidate->dcb();
            mxb_assert(pCandidate == pDcb->protocol());
            // Put back the original handler.
            pDcb->set_handler(pCandidate);
            pSession->link_backend_connection(pCandidate);

            if (pCandidate->reuse(pSes, pUpstream, reuse))
            {
                pFound_conn = pCandidate;
            }
            else
            {
                // Reusing the current candidate failed. Close connection, then try with another candidate.
                pSession->unlink_backend_connection(pCandidate);
                MXB_WARNING("Failed to reuse a persistent connection.");
                if (pDcb->state() == DCB::State::POLLING)
                {
                    pDcb->disable_events();
                }

                BackendDCB::close(pDcb);
                pServer->stats().remove_connection();
                notify_connection_available(pServer);
            }
        }

        if (pFound_conn)
        {
            // Put the dcb back to the regular book-keeping.
            mxb_assert(m_dcbs.find(pFound_conn->dcb()) == m_dcbs.end());
            m_dcbs.insert(pFound_conn->dcb());
        }
    }
    // else: the server does not have an entry in the pool group.

    return pFound_conn;
}

bool RoutingWorker::move_to_conn_pool(BackendDCB* pDcb)
{
    std::lock_guard<std::mutex> guard(m_pool_lock);

    bool moved_to_pool = false;
    auto* pServer = static_cast<Server*>(pDcb->server());
    long global_pool_cap = pServer->persistpoolmax();
    // For pooling to be possible, several conditions must be met. Check the most obvious ones first.
    if (global_pool_cap > 0)
    {
        auto* pSession = pDcb->session();
        auto* pConn = pDcb->protocol();
        // Pooling enabled for the server. Check connection, session and server status.
        if (pDcb->state() == DCB::State::POLLING && !pDcb->hanged_up() && pConn->established()
            && pSession && pSession->can_pool_backends()
            && pServer->is_running())
        {
            // All ok. Try to add the connection to pool.
            auto pool_iter = m_pool_group.find(pServer);

            if (pool_iter == m_pool_group.end())
            {
                // First pooled connection for the server.
                ConnectionPool new_pool(this, pServer, global_pool_cap);
                pool_iter = m_pool_group.emplace(pServer, std::move(new_pool)).first;
            }

            auto& pool = pool_iter->second;
            if (pool.has_space())
            {
                pool.add_connection(pConn);
                moved_to_pool = true;
            }

            if (moved_to_pool)
            {
                pConn->set_to_pooled();
                pDcb->clear();
                // Change the handler to one that will close the DCB in case there
                // is any activity on it.
                pDcb->set_handler(&m_pool_handler);

                // Remove the dcb from the regular book-keeping.
                auto it = m_dcbs.find(pDcb);
                mxb_assert(it != m_dcbs.end());
                m_dcbs.erase(it);
            }
        }
    }
    return moved_to_pool;
}

size_t RoutingWorker::pool_close_all_conns()
{
    mxb_assert(Worker::is_current());

    size_t nClosed = 0;
    for (auto& kv : m_pool_group)
    {
        ConnectionPool& pool = kv.second;
        nClosed += pool.close_all();
    }
    m_pool_group.clear();

    return nClosed;
}


void RoutingWorker::pool_close_all_conns_by_server(SERVER* pSrv)
{
    std::lock_guard<std::mutex> guard(m_pool_lock);
    auto it = m_pool_group.find(pSrv);
    if (it != m_pool_group.end())
    {
        it->second.close_all();
        m_pool_group.erase(it);
    }
}

void RoutingWorker::ConnectionPool::close_expired()
{
    auto* pServer = static_cast<Server*>(m_pTarget_server);
    auto max_age = pServer->persistmaxtime();

    time_t now = time(nullptr);
    vector<mxs::BackendConnection*> expired_conns;

    // First go through the list and gather the expired connections.
    auto it = m_contents.begin();
    while (it != m_contents.end())
    {
        ConnPoolEntry& entry = it->second;
        if (entry.hanged_up() || (now - entry.created() > max_age))
        {
            expired_conns.push_back(entry.release_conn());
            it = m_contents.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Check that pool is not over capacity. This can only happen if user reduces capacity via a runtime
    // config modification.
    int64_t over_cap_conns = m_contents.size() - m_capacity;
    if (over_cap_conns > 0)
    {
        // Just take the first extra connections found.
        int conns_removed = 0;
        auto remover_it = m_contents.begin();
        while (conns_removed < over_cap_conns)
        {
            expired_conns.push_back(remover_it->second.release_conn());
            remover_it = m_contents.erase(remover_it);
            conns_removed++;
        }
    }

    for (auto* pConn : expired_conns)
    {
        m_pOwner->close_pooled_dcb(pConn->dcb());
    }
}

void RoutingWorker::ConnectionPool::remove_and_close(mxs::BackendConnection* pConn)
{
    auto it = m_contents.find(pConn);
    mxb_assert(it != m_contents.end());
    it->second.release_conn();
    m_contents.erase(it);
    m_pOwner->close_pooled_dcb(pConn->dcb());
}

size_t RoutingWorker::ConnectionPool::close_all()
{
    size_t rv = m_contents.size();

    // Close all entries in the server-specific pool.
    for (auto& pool_entry : m_contents)
    {
        BackendDCB* pDcb = pool_entry.second.release_conn()->dcb();
        m_pOwner->close_pooled_dcb(pDcb);
    }
    m_contents.clear();

    return rv;
}

bool RoutingWorker::ConnectionPool::empty() const
{
    return m_contents.empty();
}

void RoutingWorker::ConnectionPool::add_connection(mxs::BackendConnection* conn)
{
    m_contents.emplace(conn, ConnPoolEntry(conn));
    m_stats.max_size = std::max(m_stats.max_size, m_contents.size());
}

RoutingWorker::ConnectionPool::ConnectionPool(mxs::RoutingWorker* pOwner,
                                              SERVER* pTarget_server,
                                              int global_capacity)
    : m_pOwner(pOwner)
    , m_pTarget_server(pTarget_server)
{
    set_capacity(global_capacity);
}

RoutingWorker::ConnectionPool::ConnectionPool(RoutingWorker::ConnectionPool&& rhs)
    : m_contents(std::move(rhs.m_contents))
    , m_pOwner(rhs.m_pOwner)
    , m_pTarget_server(rhs.m_pTarget_server)
    , m_capacity(rhs.m_capacity)
    , m_stats(rhs.m_stats)
{
}

bool RoutingWorker::ConnectionPool::has_space() const
{
    return (int)m_contents.size() < m_capacity;
}

RoutingWorker::ConnectionPoolStats RoutingWorker::ConnectionPool::stats() const
{
    m_stats.curr_size = m_contents.size();
    return m_stats;
}

void RoutingWorker::evict_dcb(BackendDCB* pDcb)
{
    std::lock_guard<std::mutex> guard(m_pool_lock);

    auto it = m_pool_group.find(pDcb->server());
    mxb_assert(it != m_pool_group.end());
    ConnectionPool& conn_pool = it->second;
    conn_pool.remove_and_close(pDcb->protocol());
}

void RoutingWorker::close_pooled_dcb(BackendDCB* pDcb)
{
    // Put the DCB back into the regular book-keeping.
    mxb_assert(m_dcbs.find(pDcb) == m_dcbs.end());
    m_dcbs.insert(pDcb);

    if (pDcb->state() == DCB::State::POLLING)
    {
        pDcb->disable_events();
    }

    auto* srv = pDcb->server();
    BackendDCB::close(pDcb);
    srv->stats().remove_connection();
    notify_connection_available(srv);
}

void RoutingWorker::make_dcalls()
{
    mxb_assert(Worker::is_current());

    mxb_assert(m_check_pool_dcid == 0);
    mxb_assert(m_activate_eps_dcid == 0);
    mxb_assert(m_timeout_eps_dcid == 0);

    // Every second, check connection pool for expired connections. Ideally, every pooled
    // connection would set their own timer.
    auto check_pool_cb = [this](Callable::Action action){
        if (action == Callable::EXECUTE)
        {
            pool_close_expired();
        }
        return true;
    };
    m_check_pool_dcid = m_callable.dcall(1s, check_pool_cb);

    // The normal connection availability notification is not fool-proof, as it's only sent to the
    // current worker. Every now and then, each worker should check for connections regardless since
    // some may be available.
    auto activate_eps_cb = [this](Callable::Action action) {
        if (action == Callable::Action::EXECUTE)
        {
            activate_waiting_endpoints();
        }
        return true;
    };
    m_activate_eps_dcid = m_callable.dcall(5s, activate_eps_cb);

    auto timeout_eps_cb = [this](Callable::Action action) {
        if (action == Callable::Action::EXECUTE)
        {
            fail_timed_out_endpoints();
        }
        return true;
    };
    m_timeout_eps_dcid = m_callable.dcall(10s, timeout_eps_cb);
}

void RoutingWorker::cancel_dcalls()
{
    mxb_assert(Worker::is_current());

    if (m_check_pool_dcid)
    {
        m_callable.cancel_dcall(m_check_pool_dcid);
        m_check_pool_dcid = 0;
    }

    if (m_activate_eps_dcid)
    {
        m_callable.cancel_dcall(m_activate_eps_dcid);
        m_activate_eps_dcid = 0;
    }

    if (m_timeout_eps_dcid)
    {
        m_callable.cancel_dcall(m_timeout_eps_dcid);
        m_timeout_eps_dcid = 0;
    }

    m_callable.cancel_dcalls();
}

bool RoutingWorker::pre_run()
{
    mxb_assert(Worker::is_current());

    this_thread.pCurrent_worker = this;

    bool rv = modules_thread_init();

    if (rv)
    {
        mxs::CachingParser::thread_init();
        init_datas();

        make_dcalls();
    }
    else
    {
        MXB_ERROR("Could not perform thread initialization for all modules. Thread exits.");
        this_thread.pCurrent_worker = nullptr;
    }

    return rv;
}

void RoutingWorker::post_run()
{
    mxb_assert(Worker::is_current());

    if (is_listening())
    {
        stop_polling_on_shared_fd();
    }

    cancel_dcalls();

    int64_t cleared = mxs::CachingParser::clear_thread_cache();
    size_t nClosed = pool_close_all_conns();

    finish_datas();

    // See MainWorker::post_run for an explanation why this is done here
    m_storage.clear();

    mxs::CachingParser::thread_finish();
    modules_thread_finish();
    // TODO: Add service_thread_finish().

    if (maxscale_is_shutting_down())
    {
        auto i = index();

        if (i != 0)
        {
            // Was not the last, so shutdown the previous one.
            auto* pWorker = this_unit.ppWorkers[i - 1];

            pWorker->execute([pWorker]() {
                    pWorker->start_try_shutdown();
                }, EXECUTE_QUEUED);
        }
    }
    else
    {
        MXB_NOTICE("%s of memory used by the query classifier cache released and "
                   "%lu pooled connections closed "
                   "when routing worker %d was terminated.",
                   mxb::pretty_size(cleared).c_str(),
                   nClosed,
                   index());
    }

    this_thread.pCurrent_worker = nullptr;
}

/**
 * Creates a worker instance.
 *
 * @param index              Routing worker index.
 *
 * @return A worker instance if successful, otherwise NULL.
 */
// static
unique_ptr<RoutingWorker> RoutingWorker::create(int index,
                                                size_t rebalance_window,
                                                const std::vector<std::shared_ptr<Listener>>& listeners)
{
    unique_ptr<RoutingWorker> sWorker(new (std::nothrow) RoutingWorker(index, rebalance_window));

    if (sWorker)
    {
        if (sWorker->start(MAKE_STR("Worker-" << std::setw(2) << std::setfill('0') << index)))
        {
            bool success = false;
            sWorker->call([&sWorker, &listeners, &success]() {
                    success = sWorker->activate(listeners);
                }, Worker::EXECUTE_QUEUED);

            mxb_assert(success);

            if (!success)
            {
                MXB_ERROR("Could not activate worker %d.", index);

                sWorker->shutdown();
                sWorker->join();

                sWorker.reset();
            }
        }
        else
        {
            MXB_ERROR("Could not start worker %d.", index);
        }
    }
    else
    {
        MXB_ERROR("Could not create worker %d.", index);
    }

    return sWorker;
}

void RoutingWorker::epoll_tick()
{
    mxb_assert(Worker::is_current());

    process_timeouts();

    delete_zombies();

    for (auto& func : m_epoll_tick_funcs)
    {
        func();
    }

    if (m_rebalance.perform)
    {
        rebalance();
    }
}

int RoutingWorker::poll_fd() const
{
    return this_unit.epoll_listener_fd;
}

/**
 * Handler for events occurring in the shared epoll instance.
 *
 * @param events  The events.
 *
 * @return What actions were performed.
 */
uint32_t RoutingWorker::handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context context)
{
    mxb_assert(Worker::is_current());
    mxb_assert(pWorker == this);

    struct epoll_event epoll_events[1];

    // We extract just one event
    int nfds = epoll_wait(this_unit.epoll_listener_fd, epoll_events, 1, 0);

    uint32_t actions = mxb::poll_action::NOP;

    if (nfds == -1)
    {
        MXB_ERROR("epoll_wait failed: %s", mxb_strerror(errno));
    }
    else if (nfds == 0)
    {
        MXB_DEBUG("No events for worker %d.", id());
    }
    else
    {
        MXB_DEBUG("1 event for routing worker %d.", id());
        Pollable* pPollable = static_cast<Pollable*>(epoll_events[0].data.ptr);

        actions = pPollable->handle_poll_events(this, epoll_events[0].events, Pollable::NEW_CALL);
    }

    return actions;
}

// static
size_t RoutingWorker::broadcast(Task* pTask, Semaphore* pSem)
{
    // No logging here, function must be signal safe.
    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->execute(pTask, pSem, EXECUTE_AUTO))
        {
            ++n;
        }
    }

    return n;
}

// static
size_t RoutingWorker::broadcast(std::unique_ptr<DisposableTask> sTask)
{
    DisposableTask* pTask = sTask.release();
    Worker::inc_ref(pTask);

    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->post_disposable(pTask, EXECUTE_AUTO))
        {
            ++n;
        }
    }

    Worker::dec_ref(pTask);

    return n;
}

// static
size_t RoutingWorker::broadcast(const std::function<void ()>& func,
                                mxb::Semaphore* pSem,
                                Worker::execute_mode_t mode)
{
    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->execute(func, pSem, mode))
        {
            ++n;
        }
    }

    return n;
}

// static
size_t RoutingWorker::execute_serially(Task& task)
{
    Semaphore sem;
    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->execute(&task, &sem, EXECUTE_AUTO))
        {
            sem.wait();
            ++n;
        }
    }

    return n;
}

// static
size_t RoutingWorker::execute_serially(const std::function<void()>& func)
{
    Semaphore sem;
    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->execute(func, &sem, EXECUTE_AUTO))
        {
            sem.wait();
            ++n;
        }
    }

    return n;
}

// static
size_t RoutingWorker::execute_concurrently(Task& task)
{
    Semaphore sem;
    return sem.wait_n(RoutingWorker::broadcast(&task, &sem));
}

// static
size_t RoutingWorker::execute_concurrently(const std::function<void()>& func)
{
    Semaphore sem;
    return sem.wait_n(RoutingWorker::broadcast(func, &sem, EXECUTE_AUTO));
}

// static
size_t RoutingWorker::broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        if (pWorker->post_message(msg_id, arg1, arg2))
        {
            ++n;
        }
    }

    return n;
}

namespace
{

std::vector<Worker::Statistics> get_stats()
{
    mxb_assert(MainWorker::is_current());

    std::vector<Worker::Statistics> rval;

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        rval.push_back(pWorker->statistics());
    }

    return rval;
}
}

// static
Worker::Statistics RoutingWorker::get_statistics()
{
    mxb_assert(MainWorker::is_current());

    auto s = get_stats();

    Statistics cs;

    cs.n_read = mxs::sum(s, &Statistics::n_read);
    cs.n_write = mxs::sum(s, &Statistics::n_write);
    cs.n_error = mxs::sum(s, &Statistics::n_error);
    cs.n_hup = mxs::sum(s, &Statistics::n_hup);
    cs.n_accept = mxs::sum(s, &Statistics::n_accept);
    cs.n_polls = mxs::sum(s, &Statistics::n_polls);
    cs.n_pollev = mxs::sum(s, &Statistics::n_pollev);
    cs.evq_avg = mxs::avg(s, &Statistics::evq_avg);
    cs.evq_max = mxs::max(s, &Statistics::evq_max);
    cs.maxqtime = mxs::max(s, &Statistics::maxqtime);
    cs.maxexectime = mxs::max(s, &Statistics::maxexectime);
    cs.n_fds = mxs::sum_element(s, &Statistics::n_fds);
    cs.n_fds = mxs::min_element(s, &Statistics::n_fds);
    cs.n_fds = mxs::max_element(s, &Statistics::n_fds);
    cs.qtimes = mxs::avg_element(s, &Statistics::qtimes);
    cs.exectimes = mxs::avg_element(s, &Statistics::exectimes);

    return cs;
}

// static
bool RoutingWorker::get_qc_stats_by_index(int index, CachingParser::Stats* pStats)
{
    mxb_assert(MainWorker::is_current());

    class Task : public Worker::Task
    {
    public:
        Task(CachingParser::Stats* pStats)
            : m_stats(*pStats)
        {
        }

        void execute(Worker&) override final
        {
            mxs::CachingParser::get_thread_cache_stats(&m_stats);
        }

    private:
        CachingParser::Stats& m_stats;
    };

    RoutingWorker* pWorker = RoutingWorker::get_by_index(index);

    if (pWorker)
    {
        Semaphore sem;
        Task task(pStats);
        pWorker->execute(&task, &sem, EXECUTE_AUTO);
        sem.wait();
    }

    return pWorker != nullptr;
}

// static
void RoutingWorker::get_qc_stats(std::vector<CachingParser::Stats>& all_stats)
{
    mxb_assert(MainWorker::is_current());

    class Task : public Worker::Task
    {
    public:
        Task(std::vector<CachingParser::Stats>* pAll_stats)
            : m_all_stats(*pAll_stats)
        {
        }

        void execute(Worker& worker) override final
        {
            int index = static_cast<RoutingWorker&>(worker).index();
            mxb_assert(index >= 0 && index < (int)m_all_stats.size());

            CachingParser::Stats& stats = m_all_stats[index];

            mxs::CachingParser::get_thread_cache_stats(&stats);
        }

    private:
        std::vector<CachingParser::Stats>& m_all_stats;
    };

    auto nWorkers = this_unit.nRunning.load(std::memory_order_relaxed);

    all_stats.resize(nWorkers);

    Task task(&all_stats);
    mxs::RoutingWorker::execute_concurrently(task);
}

namespace
{

json_t* qc_stats_to_json(const char* zHost, int id, const CachingParser::Stats& stats)
{
    mxb_assert(MainWorker::is_current());

    json_t* pStats = json_object();
    json_object_set_new(pStats, "size", json_integer(stats.size));
    json_object_set_new(pStats, "inserts", json_integer(stats.inserts));
    json_object_set_new(pStats, "hits", json_integer(stats.hits));
    json_object_set_new(pStats, "misses", json_integer(stats.misses));
    json_object_set_new(pStats, "evictions", json_integer(stats.evictions));

    json_t* pAttributes = json_object();
    json_object_set_new(pAttributes, "stats", pStats);

    json_t* pSelf = mxs_json_self_link(zHost, "qc_stats", std::to_string(id).c_str());

    json_t* pJson = json_object();
    json_object_set_new(pJson, CN_ID, json_string(std::to_string(id).c_str()));
    json_object_set_new(pJson, CN_TYPE, json_string("qc_stats"));
    json_object_set_new(pJson, CN_ATTRIBUTES, pAttributes);
    json_object_set_new(pJson, CN_LINKS, pSelf);

    return pJson;
}
}

// static
std::unique_ptr<json_t> RoutingWorker::get_qc_stats_as_json_by_index(const char* zHost, int index)
{
    mxb_assert(MainWorker::is_current());

    std::unique_ptr<json_t> sStats;

    CachingParser::Stats stats;

    if (get_qc_stats_by_index(index, &stats))
    {
        json_t* pJson = qc_stats_to_json(zHost, index, stats);

        stringstream self;
        self << MXS_JSON_API_QC_STATS << index;

        sStats.reset(mxs_json_resource(zHost, self.str().c_str(), pJson));
    }

    return sStats;
}

// static
std::unique_ptr<json_t> RoutingWorker::get_qc_stats_as_json(const char* zHost)
{
    mxb_assert(MainWorker::is_current());

    vector<CachingParser::Stats> all_stats;

    get_qc_stats(all_stats);

    std::unique_ptr<json_t> sAll_stats(json_array());

    int id = 0;
    for (const auto& stats : all_stats)
    {
        json_t* pJson = qc_stats_to_json(zHost, id, stats);

        json_array_append_new(sAll_stats.get(), pJson);
        ++id;
    }

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_QC_STATS, sAll_stats.release()));
}

// static
RoutingWorker* RoutingWorker::pick_worker()
{
    static uint32_t index_generator = 0;

    // nConfigured, so that we will not use a worker that is draining.

    uint32_t nConfigured = this_unit.nConfigured.load(std::memory_order_relaxed);
    uint32_t index = mxb::atomic::add(&index_generator, 1, mxb::atomic::RELAXED) % nConfigured;

    return this_unit.ppWorkers[index];
}

void RoutingWorker::register_epoll_tick_func(std::function<void ()> func)
{
    mxb_assert(Worker::is_current());

    m_epoll_tick_funcs.push_back(func);
}

void RoutingWorker::update_average_load(size_t count)
{
    mxb_assert(MainWorker::is_current());

    if (m_average_load.size() != count)
    {
        m_average_load.resize(count);
    }

    m_average_load.add_value(this->load(WorkerLoad::ONE_SECOND));
}

void RoutingWorker::terminate()
{
    mxb_assert(Worker::is_current());

    // No more messages for this worker.
    set_messages_enabled(false);

    auto start = std::chrono::steady_clock::now();

    m_callable.dcall(500ms, [this, start]() {
            bool ready_to_proceed = false;
            auto now = std::chrono::steady_clock::now();

            std::chrono::duration time_passed = now - start;

            if (maxscale_is_shutting_down())
            {
                MXB_NOTICE("Terminating worker %d going down immediately, "
                           "as MaxScale shutdown has been initiated.",
                           index());
                ready_to_proceed = true;
            }
            else if (time_passed > TERMINATION_DELAY)
            {
                MXB_NOTICE("Terminating worker %d going down as %d milliseconds has "
                           "passed since termination was started.",
                           index(),
                           (int)std::chrono::duration_cast<std::chrono::milliseconds>(time_passed).count());
                ready_to_proceed = true;
            }
            else
            {
                std::chrono::duration remaining = TERMINATION_DELAY - time_passed;
                MXB_NOTICE("Terminating worker %d has been idle for %d milliseconds, "
                           "still waiting %d milliseconds before going down.",
                           index(),
                           (int)std::chrono::duration_cast<std::chrono::milliseconds>(time_passed).count(),
                           (int)std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
            }

            if (ready_to_proceed)
            {
                shutdown();

                MainWorker* pMain = MainWorker::get();

                pMain->execute([this]() {
                        mxb_assert(MainWorker::is_current());
                        mxb_assert(this_unit.termination_in_process);

                        this->join();
                        delete this;

                        // Others may be in line.
                        terminate_last_if_dormant(false);
                    }, Worker::EXECUTE_QUEUED);
            }

            return !ready_to_proceed;
        });
}

// static
void RoutingWorker::collect_worker_load(size_t count)
{
    mxb_assert(MainWorker::is_current());

    auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    for (int i = 0; i < nRunning; ++i)
    {
        auto* pWorker = this_unit.ppWorkers[i];

        pWorker->update_average_load(count);
    }
}

// static
bool RoutingWorker::balance_workers()
{
    mxb_assert(MainWorker::is_current());

    bool balancing = false;

    int threshold = mxs::Config::get().rebalance_threshold.get();

    if (threshold != 0)
    {
        balancing = balance_workers(threshold);
    }

    return balancing;
}

// static
bool RoutingWorker::balance_workers(int threshold)
{
    mxb_assert(MainWorker::is_current());

    bool balancing = false;

    int min_load = 100;
    int max_load = 0;
    RoutingWorker* pTo = nullptr;
    RoutingWorker* pFrom = nullptr;

    auto rebalance_period = mxs::Config::get().rebalance_period.get();
    // If rebalance_period is != 0, then the average load has been updated
    // and we can use it.
    bool use_average = rebalance_period != std::chrono::milliseconds(0);

    auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    for (int i = 0; i < this_unit.nRunning; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];

        int load;

        if (use_average)
        {
            load = pWorker->average_load();
        }
        else
        {
            // If we can't use the average, we use one second load.
            load = pWorker->load(WorkerLoad::ONE_SECOND);
        }

        if (load < min_load)
        {
            min_load = load;
            pTo = pWorker;
        }

        if (load > max_load)
        {
            max_load = load;
            pFrom = pWorker;
        }
    }

    int diff_load = max_load - min_load;

    if (diff_load > threshold)
    {
        MXB_NOTICE("Difference in load (%d) between the thread with the maximum load (%d) the thread "
                   "with the minimum load (%d) exceeds the 'rebalance_threshold' value of %d, "
                   "moving work from the latter to the former.",
                   diff_load, max_load, min_load, threshold);
        balancing = true;
    }

    if (balancing)
    {
        mxb_assert(pFrom);
        mxb_assert(pTo);

        if (!pFrom->execute([pFrom, pTo]() {
                                pFrom->rebalance(pTo);
                            }, Worker::EXECUTE_QUEUED))
        {
            MXB_ERROR("Could not post task to worker, worker load balancing will not take place.");
        }
    }

    return balancing;
}

void RoutingWorker::rebalance(RoutingWorker* pTo, int nSessions)
{
    mxb_assert(Worker::is_current());

    // We can't balance here, because if a single epoll_wait() call returns
    // both the rebalance-message (sent from balance_workers() above) and
    // an event for a DCB that we move to another worker, we would crash.
    // So we only make a note and rebalance in epoll_tick().
    m_rebalance.set(pTo, nSessions);
}

void RoutingWorker::rebalance()
{
    mxb_assert(Worker::is_current());
    mxb_assert(m_rebalance.pTo);
    mxb_assert(m_rebalance.perform);

    const int n_requested_moves = m_rebalance.nSessions;
    if (n_requested_moves == 1)
    {
        // Just one, move the most active one.
        int max_io_activity = 0;
        Session* pMax_session = nullptr;

        for (auto& kv : m_sessions)
        {
            auto pSession = static_cast<Session*>(kv.second);
            if (pSession->is_movable())
            {
                int io_activity = pSession->io_activity();

                if (io_activity > max_io_activity)
                {
                    max_io_activity = io_activity;
                    pMax_session = pSession;
                }
            }
        }

        if (pMax_session)
        {
            pMax_session->move_to(m_rebalance.pTo);
        }
        else if (!m_sessions.empty())
        {
            MXB_INFO("Could not move any sessions from worker %i because all its sessions are in an "
                     "unmovable state.", id());
        }
    }
    else if (n_requested_moves > 1)
    {
        // TODO: Move all sessions in one message to recipient worker.

        std::vector<Session*> sessions;

        // If more than one, just move enough sessions is arbitrary order.
        for (auto& kv : m_sessions)
        {
            auto pSession = static_cast<Session*>(kv.second);
            if (pSession->is_movable())
            {
                sessions.push_back(pSession);
                if (sessions.size() == (size_t)n_requested_moves)
                {
                    break;
                }
            }
        }

        int n_available_sessions = m_sessions.size();
        int n_movable_sessions = sessions.size();
        if (n_movable_sessions < n_requested_moves && n_available_sessions >= n_requested_moves)
        {
            // Had enough sessions but some were not movable.
            int non_movable = n_available_sessions - n_movable_sessions;
            MXB_INFO("%i session(s) out of %i on worker %i are in an unmovable state.",
                     non_movable, n_available_sessions, id());
        }

        for (auto* pSession : sessions)
        {
            pSession->move_to(m_rebalance.pTo);
        }
    }

    m_rebalance.reset();
}

namespace
{

class MemoryTask : public Worker::Task
{
public:
    MemoryTask(uint32_t nThreads)
        : m_tmus(nThreads)
    {
    }

    void execute(Worker& worker) override final
    {
        auto& rworker = static_cast<RoutingWorker&>(worker);

        m_tmus[rworker.index()] = rworker.calculate_memory_usage();
    }

    void fill(json_t* pStats)
    {
        RoutingWorker::MemoryUsage pmu;

        json_t* pThreads = json_array();

        for (size_t i = 0; i < m_tmus.size(); ++i)
        {
            const auto& tmu = m_tmus[i];

            json_array_append_new(pThreads, tmu.to_json());
            pmu += tmu;
        }

        json_object_set_new(pStats, "process", pmu.to_json());
        json_object_set_new(pStats, "threads", pThreads);
    }

private:
    std::vector<RoutingWorker::MemoryUsage> m_tmus;
};

}

//static
std::unique_ptr<json_t> RoutingWorker::memory_to_json(const char* zHost)
{
    mxb_assert(MainWorker::is_current());

    MemoryTask task(this_unit.nRunning.load(std::memory_order_relaxed));
    RoutingWorker::execute_concurrently(task);

    json_t* pAttr = json_object();
    task.fill(pAttr);

    json_t* pMemory = json_object();
    json_object_set_new(pMemory, CN_ID, json_string(CN_MEMORY));
    json_object_set_new(pMemory, CN_TYPE, json_string(CN_MEMORY));
    json_object_set_new(pMemory, CN_ATTRIBUTES, pAttr);

    return std::unique_ptr<json_t>(mxs_json_resource(zHost, MXS_JSON_API_MEMORY, pMemory));
}

RoutingWorker::MemoryUsage RoutingWorker::calculate_memory_usage() const
{
    mxb_assert(Worker::is_current());

    MemoryUsage rv;

    CachingParser::Stats qc;
    if (mxs::CachingParser::get_thread_cache_stats(&qc))
    {
        rv.query_classifier = qc.size;
    }

    for (const DCB* pZombie : m_zombies)
    {
        rv.zombies += pZombie->runtime_size();
    }

    const Registry<MXS_SESSION>& sessions = session_registry();
    for (const auto& kv : sessions)
    {
        rv.sessions += kv.second->runtime_size();
    }

    rv.total = rv.query_classifier + rv.zombies + rv.sessions;

    return rv;
}

// static
void RoutingWorker::start_shutdown()
{
    // The routing workers are shutdown serially from the end, to ensure that
    // the finish_for(...) calls are done in the inverse order of how the
    // init_for(...) calls were made.

    auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    mxb_assert(nRunning > 0);

    auto* pWorker = this_unit.ppWorkers[nRunning - 1];

    pWorker->execute([pWorker]() {
            pWorker->start_try_shutdown();
        }, EXECUTE_QUEUED);
}

//static
bool RoutingWorker::set_listen_mode(int index, bool enabled)
{
    mxb_assert(MainWorker::is_current());

    bool rv = false;

    int n = this_unit.nConfigured.load(std::memory_order_relaxed);

    if (index >= 0 && index < n)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[index];
        mxb_assert(pWorker);

        auto listeners = Listener::get_started_listeners();

        bool success = false;

        if (!pWorker->call([pWorker, enabled, &listeners, &rv]() {
                    mxb_assert(pWorker->is_active());

                    bool is_listening = pWorker->is_listening();

                    if (is_listening && !enabled)
                    {
                        if (pWorker->stop_listening(listeners))
                        {
                            rv = true;
                        }
                        else
                        {
                            MXB_ERROR("Could not stop listening.");
                        }
                    }
                    else if (!is_listening && enabled)
                    {
                        if (pWorker->start_listening(listeners))
                        {
                            rv = true;
                        }
                        else
                        {
                            MXB_ERROR("Could not start listening.");
                        }
                    }
                    else
                    {
                        rv = true;
                    }
                }, EXECUTE_QUEUED))
        {
            MXB_ERROR("Could not call routing worker %d.", index);
        }
    }
    else
    {
        MXB_ERROR("%d does not refer to an active worker.", index);
    }

    return rv;
}


//static
bool RoutingWorker::termination_in_process()
{
    return this_unit.termination_in_process;
}

void RoutingWorker::start_try_shutdown()
{
    mxb_assert(Worker::is_current());

    if (try_shutdown_dcall())
    {
        m_callable.dcall(100ms, &RoutingWorker::try_shutdown_dcall, this);
    }
}

bool RoutingWorker::try_shutdown_dcall()
{
    mxb_assert(Worker::is_current());

    bool retry = false;

    pool_close_all_conns();

    if (termination_in_process())
    {
        retry = true;
    }
    else if (m_sessions.empty())
    {
        shutdown();
    }
    else
    {
        for (const auto& s : m_sessions)
        {
            s.second->kill();
        }
        retry = true;
    }

    return retry;
}

void RoutingWorker::register_session(MXS_SESSION* ses)
{
    mxb_assert(Worker::is_current());

    MXB_AT_DEBUG(bool rv = ) m_sessions.add(ses);
    mxb_assert(rv);
}

void RoutingWorker::deregister_session(uint64_t session_id)
{
    mxb_assert(Worker::is_current());

    bool rv = m_sessions.remove(session_id);

    if (rv && can_deactivate())
    {
        deactivate();
    }
}

//static
void RoutingWorker::pool_set_size(const std::string& srvname, int64_t size)
{
    auto* pWorker = RoutingWorker::get_current();
    std::lock_guard<std::mutex> guard(pWorker->m_pool_lock);
    // Check if the worker has a pool with the given server name and update if found.
    // The pool may not exist if pooling was previously disabled or empty.
    for (auto& kv : pWorker->m_pool_group)
    {
        if (kv.first->name() == srvname)
        {
            kv.second.set_capacity(size);
            break;
        }
    }
}

RoutingWorker::ConnectionPoolStats RoutingWorker::pool_get_stats(const SERVER* pSrv)
{
    mxb_assert(MainWorker::is_current());

    RoutingWorker::ConnectionPoolStats rval;

    auto nRunning = this_unit.nRunning.load(std::memory_order_relaxed);
    for (int i = 0; i < this_unit.nRunning; ++i)
    {
        rval.add(this_unit.ppWorkers[i]->pool_stats(pSrv));
    }

    return rval;
}

RoutingWorker::ConnectionPoolStats RoutingWorker::pool_stats(const SERVER* pSrv)
{
    mxb_assert(MainWorker::is_current());

    ConnectionPoolStats rval;
    std::lock_guard<std::mutex> guard(m_pool_lock);

    auto it = m_pool_group.find(pSrv);

    if (it != m_pool_group.end())
    {
        rval = it->second.stats();
    }

    return rval;
}

void RoutingWorker::add_conn_wait_entry(ServerEndpoint* ep)
{
    m_eps_waiting_for_conn[ep->server()].push_back(ep);
}

void RoutingWorker::erase_conn_wait_entry(ServerEndpoint* ep)
{
    mxb_assert(Worker::is_current());

    auto map_iter = m_eps_waiting_for_conn.find(ep->server());
    mxb_assert(map_iter != m_eps_waiting_for_conn.end());
    // The element is surely found in both the map and the set.
    auto& ep_deque = map_iter->second;
    // Erasing from the middle of a deque is inefficient, as possibly a large number of elements
    // needs to be moved. TODO: set the element to null and erase later.
    ep_deque.erase(std::find(ep_deque.begin(), ep_deque.end(), ep));

    if (ep_deque.empty())
    {
        m_eps_waiting_for_conn.erase(map_iter);
    }
}

void RoutingWorker::notify_connection_available(SERVER* server)
{
    mxb_assert(Worker::is_current());

    // A connection to a server should be available, either in the pool or a new one can be created.
    // Cannot be certain due to other threads. Do not activate any connections here, only schedule a check.

    // In the vast majority of cases (whenever idle pooling is disabled) the map is empty.
    if (!m_eps_waiting_for_conn.empty() && !m_ep_activation_scheduled)
    {
        if (m_eps_waiting_for_conn.count(server) > 0)
        {
            // An endpoint is waiting for connection to this server.
            auto func = [this]() {
                    activate_waiting_endpoints();
                    m_ep_activation_scheduled = false;
                    return false;
                };

            // The check will run once execution returns to the event loop.
            execute(func, execute_mode_t::EXECUTE_QUEUED);
            m_ep_activation_scheduled = true;
        }
    }
}

/**
 * A connection slot to at least one server should be available. Add as many connections as possible.
 */
void RoutingWorker::activate_waiting_endpoints()
{
    mxb_assert(Worker::is_current());

    auto map_iter = m_eps_waiting_for_conn.begin();
    while (map_iter != m_eps_waiting_for_conn.end())
    {
        auto& ep_set = map_iter->second;
        bool keep_activating = true;

        while (keep_activating && !ep_set.empty())
        {
            bool erase_from_set = false;
            auto it_first = ep_set.begin();
            auto* ep = *it_first;
            auto res = ep->continue_connecting();

            switch (res)
            {
            case ServerEndpoint::ContinueRes::SUCCESS:
                // Success, remove from wait list.
                erase_from_set = true;
                break;

            case ServerEndpoint::ContinueRes::WAIT:
                // No connection was available, perhaps connection limit was reached. Continue waiting.
                // Do not try to connect to this server again right now.
                keep_activating = false;
                break;

            case ServerEndpoint::ContinueRes::FAIL:
                // Resuming the connection failed. Either connection was resumed but writing packets failed
                // or something went wrong in creating a new connection. Close the endpoint. The endpoint map
                // must not be modified by the handle_failed_continue call.
                erase_from_set = true;
                ep->handle_failed_continue();
                break;
            }

            if (erase_from_set)
            {
                ep_set.erase(it_first);
            }
        }

        if (ep_set.empty())
        {
            map_iter = m_eps_waiting_for_conn.erase(map_iter);
        }
        else
        {
            map_iter++;
        }
    }
}

void RoutingWorker::fail_timed_out_endpoints()
{
    mxb_assert(Worker::is_current());

    // Check the oldest endpoints. Fail the ones which have been waiting for too long.
    auto now = epoll_tick_now();
    auto it_map = m_eps_waiting_for_conn.begin();
    while (it_map != m_eps_waiting_for_conn.end())
    {
        auto& ep_deq = it_map->second;
        // The oldest ep:s are at the front of the deque. Close timed out ones until an element is no
        // longer timed out.
        auto it = ep_deq.begin();

        while (it != ep_deq.end())
        {
            auto* ep = *it;
            if (now - ep->conn_wait_start() > ep->session()->multiplex_timeout())
            {
                ep->handle_timed_out_continue();
                it = ep_deq.erase(it);
            }
            else
            {
                break;
            }
        }

        if (ep_deq.empty())
        {
            it_map = m_eps_waiting_for_conn.erase(it_map);
        }
        else
        {
            ++it_map;
        }
    }
}

void RoutingWorker::pool_close_expired()
{
    mxb_assert(Worker::is_current());

    std::lock_guard<std::mutex> guard(m_pool_lock);

    // Close expired connections in the thread local pool. If the server is down, purge all connections.
    for (auto& kv : m_pool_group)
    {
        auto* pServer = kv.first;
        auto& server_pool = kv.second;

        if (pServer->is_down())
        {
            server_pool.close_all();
        }
        else
        {
            server_pool.close_expired();
        }
    }
}

bool RoutingWorker::conn_to_server_needed(const SERVER* srv) const
{
    mxb_assert(Worker::is_current());

    return m_eps_waiting_for_conn.find(srv) != m_eps_waiting_for_conn.end();
}

void RoutingWorker::ConnectionPoolStats::add(const ConnectionPoolStats& rhs)
{
    curr_size += rhs.curr_size;
    max_size += rhs.max_size;
    times_found += rhs.times_found;
    times_empty += rhs.times_empty;
}

class RoutingWorker::InfoTask : public Worker::Task
{
public:
    InfoTask(const char* zHost, uint32_t nThreads)
        : m_zHost(zHost)
    {
        m_data.resize(nThreads);
    }

    void execute(Worker& worker) override final
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        mxb_assert(rworker.is_current());

        json_t* pStats = json_object();

        add_stats(rworker, pStats);

        json_t* pAttr = json_object();
        json_object_set_new(pAttr, "stats", pStats);

        int index = rworker.index();
        stringstream ss;
        ss << index;

        json_t* pJson = json_object();
        json_object_set_new(pJson, CN_ID, json_string(ss.str().c_str())); // Index is the id for the outside.
        json_object_set_new(pJson, CN_TYPE, json_string(CN_THREADS));
        json_object_set_new(pJson, CN_ATTRIBUTES, pAttr);
        json_object_set_new(pJson, CN_LINKS, mxs_json_self_link(m_zHost, CN_THREADS, ss.str().c_str()));

        mxb_assert((size_t)index < m_data.size());
        m_data[index] = pJson;
    }

    json_t* resource()
    {
        json_t* pArr = json_array();

        for (auto it = m_data.begin(); it != m_data.end(); it++)
        {
            json_array_append_new(pArr, *it);
        }

        return mxs_json_resource(m_zHost, MXS_JSON_API_THREADS, pArr);
    }

    json_t* resource(int index)
    {
        stringstream self;
        self << MXS_JSON_API_THREADS << index;
        return mxs_json_resource(m_zHost, self.str().c_str(), m_data[index]);
    }

private:
    static void add_stats(const RoutingWorker& rworker, json_t* pStats)
    {
        json_object_set_new(pStats, "state", json_string(to_string(rworker.state())));
        json_object_set_new(pStats, "listening", json_boolean(rworker.is_listening()));

        const Worker::Statistics& s = rworker.statistics();
        json_object_set_new(pStats, "reads", json_integer(s.n_read));
        json_object_set_new(pStats, "writes", json_integer(s.n_write));
        json_object_set_new(pStats, "errors", json_integer(s.n_error));
        json_object_set_new(pStats, "hangups", json_integer(s.n_hup));
        json_object_set_new(pStats, "accepts", json_integer(s.n_accept));
        json_object_set_new(pStats, "avg_event_queue_length", json_integer(s.evq_avg));
        json_object_set_new(pStats, "max_event_queue_length", json_integer(s.evq_max));
        json_object_set_new(pStats, "max_exec_time", json_integer(s.maxexectime));
        json_object_set_new(pStats, "max_queue_time", json_integer(s.maxqtime));

        int64_t nCurrent = rworker.current_fd_count();
        int64_t nTotal = rworker.total_fd_count();
        json_object_set_new(pStats, "current_descriptors", json_integer(nCurrent));
        json_object_set_new(pStats, "total_descriptors", json_integer(nTotal));

        json_t* pLoad = json_object();
        json_object_set_new(pLoad, "last_second", json_integer(rworker.load(Worker::Load::ONE_SECOND)));
        json_object_set_new(pLoad, "last_minute", json_integer(rworker.load(Worker::Load::ONE_MINUTE)));
        json_object_set_new(pLoad, "last_hour", json_integer(rworker.load(Worker::Load::ONE_HOUR)));

        json_object_set_new(pStats, "load", pLoad);

        auto sStats = CachingParser::get_thread_cache_stats_as_json();
        json_object_set_new(pStats, "query_classifier_cache", sStats.release());

        json_object_set_new(pStats, "sessions", json_integer(rworker.session_registry().size()));
        json_object_set_new(pStats, "zombies", json_integer(rworker.m_zombies.size()));

        RoutingWorker::MemoryUsage mu = rworker.calculate_memory_usage();
        json_object_set_new(pStats, "memory", mu.to_json());
    }

private:
    vector<json_t*> m_data;
    const char*     m_zHost;
};

const char* to_string(RoutingWorker::State state)
{
    switch (state)
    {
    case RoutingWorker::State::ACTIVE:
        return "Active";

    case RoutingWorker::State::DRAINING:
        return "Draining";

    case RoutingWorker::State::DORMANT:
        return "Dormant";
    }

    mxb_assert(!true);
    return "Unknown";
}

}

namespace
{

class FunctionTask : public Worker::DisposableTask
{
public:
    FunctionTask(std::function<void ()> cb)
        : m_cb(cb)
    {
    }

    void execute(Worker& worker) override final
    {
        m_cb();
    }

protected:
    std::function<void ()> m_cb;
};
}

json_t* mxs_rworker_to_json(const char* zHost, int index)
{
    mxb_assert(mxs::MainWorker::is_current());

    Worker* target = RoutingWorker::get_by_index(index);
    mxb_assert(target); // REST-API should have checked the validity.
    RoutingWorker::InfoTask task(zHost, index + 1);
    Semaphore sem;

    target->execute(&task, &sem, Worker::EXECUTE_AUTO);
    sem.wait();

    return task.resource(index);
}

json_t* mxs_rworker_list_to_json(const char* host)
{
    mxb_assert(mxs::MainWorker::is_current());
    auto n = this_unit.nRunning.load(std::memory_order_relaxed);

    RoutingWorker::InfoTask task(host, n);
    RoutingWorker::execute_concurrently(task);
    return task.resource();
}

namespace
{

class WatchdogTask : public Worker::Task
{
public:
    WatchdogTask()
    {
    }

    void execute(Worker& worker) override final
    {
        // Success if this is called.
    }
};
}

void mxs_rworker_watchdog()
{
    mxb_assert(mxs::MainWorker::is_current());

    MXB_INFO("MaxScale watchdog called.");
    WatchdogTask task;
    RoutingWorker::execute_concurrently(task);
}
