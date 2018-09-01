/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/routingworker.hh>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <sstream>

#include <maxbase/atomic.h>
#include <maxbase/semaphore.hh>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/clock.h>
#include <maxscale/limits.h>
#include <maxscale/json_api.h>
#include <maxscale/utils.hh>
#include <maxscale/statistics.hh>

#include "internal/dcb.h"
#include "internal/modules.h"
#include "internal/poll.hh"
#include "internal/service.hh"

#define WORKER_ABSENT_ID -1

using maxbase::Semaphore;
using maxbase::Worker;
using maxbase::WorkerLoad;
using maxscale::RoutingWorker;
using maxscale::Closer;
using std::vector;
using std::stringstream;

namespace
{

/**
 * Unit variables.
 */
struct this_unit
{
    bool            initialized;        // Whether the initialization has been performed.
    int             nWorkers;           // How many routing workers there are.
    RoutingWorker** ppWorkers;          // Array of routing worker instances.
    int             next_worker_id;     // Next worker id
    // DEPRECATED in 2.3, remove in 2.4.
    int number_poll_spins;      // Maximum non-block polls
    // DEPRECATED in 2.3, remove in 2.4.
    int max_poll_sleep;     // Maximum block time
    int epoll_listener_fd;  // Shared epoll descriptor for listening descriptors.
    int id_main_worker;     // The id of the worker running in the main thread.
    int id_min_worker;      // The smallest routing worker id.
    int id_max_worker;      // The largest routing worker id.
} this_unit =
{
    false,              // initialized
    0,                  // nWorkers
    NULL,               // ppWorkers
    0,                  // next_worker_id
    0,                  // number_poll_spins
    0,                  // max_poll_sleep
    -1,                 // epoll_listener_fd
    WORKER_ABSENT_ID,   // id_main_worker
    WORKER_ABSENT_ID,   // id_min_worker
    WORKER_ABSENT_ID,   // id_max_worker
};

int next_worker_id()
{
    return atomic_add(&this_unit.next_worker_id, 1);
}

thread_local struct this_thread
{
    int current_worker_id;      // The worker id of the current thread
} this_thread =
{
    WORKER_ABSENT_ID
};

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
bool modules_thread_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_init)
        {
            int rc = (module->thread_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->thread_finish)
            {
                (module->thread_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

/**
 * Calls thread_finish on all loaded modules.
 */
void modules_thread_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_finish)
        {
            (module->thread_finish)();
        }
    }
}
}

namespace maxscale
{

RoutingWorker::RoutingWorker()
    : m_id(next_worker_id())
{
    MXB_POLL_DATA::handler = &RoutingWorker::epoll_instance_handler;
    MXB_POLL_DATA::owner = this;
}

RoutingWorker::~RoutingWorker()
{
}

// static
bool RoutingWorker::init()
{
    mxb_assert(!this_unit.initialized);

    this_unit.number_poll_spins = config_nbpolls();
    this_unit.max_poll_sleep = config_pollsleep();

    this_unit.epoll_listener_fd = epoll_create(MAX_EVENTS);

    if (this_unit.epoll_listener_fd != -1)
    {
        int nWorkers = config_threadcount();
        RoutingWorker** ppWorkers = new( std::nothrow) RoutingWorker* [MXS_MAX_THREADS]();      // 0-inited
                                                                                                // array

        if (ppWorkers)
        {
            int id_main_worker = WORKER_ABSENT_ID;
            int id_min_worker = INT_MAX;
            int id_max_worker = INT_MIN;

            int i;
            for (i = 0; i < nWorkers; ++i)
            {
                RoutingWorker* pWorker = RoutingWorker::create(this_unit.epoll_listener_fd);

                if (pWorker)
                {
                    int id = pWorker->id();

                    // The first created worker will be the main worker.
                    if (id_main_worker == WORKER_ABSENT_ID)
                    {
                        id_main_worker = id;
                    }

                    if (id < id_min_worker)
                    {
                        id_min_worker = id;
                    }

                    if (id > id_max_worker)
                    {
                        id_max_worker = id;
                    }

                    ppWorkers[i] = pWorker;
                }
                else
                {
                    for (int j = i - 1; j >= 0; --j)
                    {
                        delete ppWorkers[j];
                    }

                    delete[] ppWorkers;
                    ppWorkers = NULL;
                    break;
                }
            }

            if (ppWorkers)
            {
                this_unit.ppWorkers = ppWorkers;
                this_unit.nWorkers = nWorkers;
                this_unit.id_main_worker = id_main_worker;
                this_unit.id_min_worker = id_min_worker;
                this_unit.id_max_worker = id_max_worker;

                this_unit.initialized = true;
            }
        }
        else
        {
            MXS_OOM();
            close(this_unit.epoll_listener_fd);
        }
    }
    else
    {
        MXS_ALERT("Could not allocate an epoll instance.");
    }

    if (this_unit.initialized)
    {
        // When the initialization has successfully been performed, we set the
        // current_worker_id of this thread to 0. That way any connections that
        // are made during service startup (after this function returns, but
        // bofore the workes have been started) will be handled by the worker
        // that will be running in the main thread.
        this_thread.current_worker_id = 0;
    }

    return this_unit.initialized;
}

void RoutingWorker::finish()
{
    mxb_assert(this_unit.initialized);

    for (int i = this_unit.id_max_worker; i >= this_unit.id_min_worker; --i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        delete pWorker;
        this_unit.ppWorkers[i] = NULL;
    }

    delete[] this_unit.ppWorkers;
    this_unit.ppWorkers = NULL;

    close(this_unit.epoll_listener_fd);
    this_unit.epoll_listener_fd = 0;

    this_unit.initialized = false;
}

// static
bool RoutingWorker::add_shared_fd(int fd, uint32_t events, MXB_POLL_DATA* pData)
{
    bool rv = true;

    // This must be level-triggered. Since this is intended for listening
    // sockets and each worker will call accept() just once before going
    // back the epoll_wait(), using EPOLLET would mean that if there are
    // more clients to be accepted than there are threads returning from
    // epoll_wait() for an event, then some clients would be accepted only
    // when a new client has connected, thus causing a new EPOLLIN event.
    events &= ~EPOLLET;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->owner = RoutingWorker::get(RoutingWorker::MAIN);

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

// static
bool RoutingWorker::remove_shared_fd(int fd)
{
    bool rv = true;

    struct epoll_event ev = {};

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_DEL, fd, &ev) != 0)
    {
        Worker::resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

bool mxs_worker_should_shutdown(MXB_WORKER* pWorker)
{
    return static_cast<RoutingWorker*>(pWorker)->should_shutdown();
}

RoutingWorker* RoutingWorker::get(int worker_id)
{
    if (worker_id == MAIN)
    {
        worker_id = this_unit.id_main_worker;
    }

    mxb_assert((worker_id >= this_unit.id_min_worker) && (worker_id <= this_unit.id_max_worker));

    return this_unit.ppWorkers[worker_id];
}

RoutingWorker* RoutingWorker::get_current()
{
    RoutingWorker* pWorker = NULL;

    int worker_id = get_current_id();

    if (worker_id != WORKER_ABSENT_ID)
    {
        pWorker = RoutingWorker::get(worker_id);
    }

    return pWorker;
}

int RoutingWorker::get_current_id()
{
    return this_thread.current_worker_id;
}

// static
bool RoutingWorker::start_threaded_workers()
{
    bool rv = true;

    for (int i = this_unit.id_min_worker; i <= this_unit.id_max_worker; ++i)
    {
        // The main RoutingWorker will run in the main thread, so
        // we exclude that.
        if (i != this_unit.id_main_worker)
        {
            RoutingWorker* pWorker = this_unit.ppWorkers[i];
            mxb_assert(pWorker);

            if (!pWorker->start())
            {
                MXS_ALERT("Could not start routing worker %d of %d.", i, config_threadcount());
                rv = false;
                // At startup, so we don't even try to clean up.
                break;
            }
        }
    }

    return rv;
}

// static
void RoutingWorker::join_threaded_workers()
{
    for (int i = this_unit.id_min_worker; i <= this_unit.id_max_worker; i++)
    {
        if (i != this_unit.id_main_worker)
        {
            RoutingWorker* pWorker = this_unit.ppWorkers[i];
            mxb_assert(pWorker);

            pWorker->join();
        }
    }
}

// static
void RoutingWorker::set_nonblocking_polls(unsigned int nbpolls)
{
    this_unit.number_poll_spins = nbpolls;
}

// static
void RoutingWorker::set_maxwait(unsigned int maxwait)
{
    this_unit.max_poll_sleep = maxwait;
}

RoutingWorker::SessionsById& RoutingWorker::session_registry()
{
    return m_sessions;
}

void RoutingWorker::register_zombie(DCB* pDcb)
{
    mxb_assert(pDcb->poll.owner == this);

    m_zombies.push_back(pDcb);
}

void RoutingWorker::delete_zombies()
{
    // An algorithm cannot be used, as the final closing of a DCB may cause
    // other DCBs to be registered in the zombie queue.

    while (!m_zombies.empty())
    {
        DCB* pDcb = m_zombies.back();
        m_zombies.pop_back();
        dcb_final_close(pDcb);
    }
}

bool RoutingWorker::pre_run()
{
    this_thread.current_worker_id = m_id;

    bool rv = modules_thread_init() && service_thread_init() && qc_thread_init(QC_INIT_SELF);

    if (!rv)
    {
        MXS_ERROR("Could not perform thread initialization for all modules. Thread exits.");
        this_thread.current_worker_id = WORKER_ABSENT_ID;
    }

    return rv;
}

void RoutingWorker::post_run()
{
    modules_thread_finish();
    // TODO: Add sercice_thread_finish().
    this_thread.current_worker_id = WORKER_ABSENT_ID;
}

/**
 * Creates a worker instance.
 * - Allocates the structure.
 * - Creates a pipe.
 * - Adds the read descriptor to the polling mechanism.
 *
 * @param epoll_listener_fd  The file descriptor of the epoll set to which listening
 *                           sockets will be placed.
 *
 * @return A worker instance if successful, otherwise NULL.
 */
// static
RoutingWorker* RoutingWorker::create(int epoll_listener_fd)
{
    RoutingWorker* pThis = new( std::nothrow) RoutingWorker();

    if (pThis)
    {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        MXB_POLL_DATA* pData = pThis;
        ev.data.ptr = pData;    // Necessary for pointer adjustment, otherwise downcast will not work.

        // The shared epoll instance descriptor is *not* added using EPOLLET (edge-triggered)
        // because we want it to be level-triggered. That way, as long as there is a single
        // active (accept() can be called) listening socket, epoll_wait() will return an event
        // for it. It must be like that because each worker will call accept() just once before
        // calling epoll_wait() again. The end result is that as long as the load of different
        // workers is roughly the same, the client connections will be distributed evenly across
        // the workers. If the load is not the same, then a worker with less load will get more
        // clients that a worker with more load.
        if (epoll_ctl(pThis->m_epoll_fd, EPOLL_CTL_ADD, epoll_listener_fd, &ev) == 0)
        {
            MXS_INFO("Epoll instance for listening sockets added to worker epoll instance.");
        }
        else
        {
            MXS_ERROR("Could not add epoll instance for listening sockets to "
                      "epoll instance of worker: %s",
                      mxs_strerror(errno));
            delete pThis;
            pThis = NULL;
        }
    }
    else
    {
        MXS_OOM();
    }

    return pThis;
}

void RoutingWorker::epoll_tick()
{
    dcb_process_idle_sessions(m_id);

    m_state = ZPROCESSING;

    delete_zombies();
}

/**
 * Callback for events occurring on the shared epoll instance.
 *
 * @param pData   Will point to a Worker instance.
 * @param wid     The worker id.
 * @param events  The events.
 *
 * @return What actions were performed.
 */
// static
uint32_t RoutingWorker::epoll_instance_handler(MXB_POLL_DATA* pData, MXB_WORKER* pWorker, uint32_t events)
{
    RoutingWorker* pThis = static_cast<RoutingWorker*>(pData);
    mxb_assert(pThis == pWorker);

    return pThis->handle_epoll_events(events);
}

/**
 * Handler for events occurring in the shared epoll instance.
 *
 * @param events  The events.
 *
 * @return What actions were performed.
 */
uint32_t RoutingWorker::handle_epoll_events(uint32_t events)
{
    struct epoll_event epoll_events[1];

    // We extract just one event
    int nfds = epoll_wait(this_unit.epoll_listener_fd, epoll_events, 1, 0);

    uint32_t actions = MXB_POLL_NOP;

    if (nfds == -1)
    {
        MXS_ERROR("epoll_wait failed: %s", mxs_strerror(errno));
    }
    else if (nfds == 0)
    {
        MXS_DEBUG("No events for worker %d.", m_id);
    }
    else
    {
        MXS_DEBUG("1 event for worker %d.", m_id);
        MXB_POLL_DATA* pData = static_cast<MXB_POLL_DATA*>(epoll_events[0].data.ptr);

        actions = pData->handler(pData, this, epoll_events[0].events);
    }

    return actions;
}

// static
size_t RoutingWorker::broadcast(Task* pTask, Semaphore* pSem)
{
    // No logging here, function must be signal safe.
    size_t n = 0;

    int nWorkers = this_unit.next_worker_id;
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

    int nWorkers = this_unit.next_worker_id;
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
size_t RoutingWorker::broadcast(std::function<void ()> func,
                                mxb::Semaphore* pSem,
                                mxb::Worker::execute_mode_t mode)
{
    size_t n = 0;
    int nWorkers = this_unit.next_worker_id;

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

    int nWorkers = this_unit.next_worker_id;
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
size_t RoutingWorker::execute_concurrently(Task& task)
{
    Semaphore sem;
    return sem.wait_n(RoutingWorker::broadcast(&task, &sem));
}

// static
size_t RoutingWorker::broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    int nWorkers = this_unit.next_worker_id;
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

// static
void RoutingWorker::shutdown_all()
{
    // NOTE: No logging here, this function must be signal safe.
    mxb_assert((this_unit.next_worker_id == 0) || (this_unit.ppWorkers != NULL));

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = this_unit.ppWorkers[i];
        mxb_assert(pWorker);

        pWorker->shutdown();
    }
}

namespace
{

int64_t one_stats_get(int64_t Worker::STATISTICS::* what, enum ts_stats_type type)
{
    int64_t best = type == TS_STATS_MAX ? LONG_MIN : (type == TS_STATS_MIX ? LONG_MAX : 0);

    int nWorkers = this_unit.next_worker_id;

    for (int i = 0; i < nWorkers; ++i)
    {
        RoutingWorker* pWorker = RoutingWorker::get(i);
        mxb_assert(pWorker);

        const Worker::STATISTICS& s = pWorker->statistics();

        int64_t value = s.*what;

        switch (type)
        {
        case TS_STATS_MAX:
            if (value > best)
            {
                best = value;
            }
            break;

        case TS_STATS_MIX:
            if (value < best)
            {
                best = value;
            }
            break;

        case TS_STATS_AVG:
        case TS_STATS_SUM:
            best += value;
            break;
        }
    }

    return type == TS_STATS_AVG ? best / (nWorkers != 0 ? nWorkers : 1) : best;
}
}

// static
Worker::STATISTICS RoutingWorker::get_statistics()
{
    STATISTICS cs;

    cs.n_read = one_stats_get(&STATISTICS::n_read, TS_STATS_SUM);
    cs.n_write = one_stats_get(&STATISTICS::n_write, TS_STATS_SUM);
    cs.n_error = one_stats_get(&STATISTICS::n_error, TS_STATS_SUM);
    cs.n_hup = one_stats_get(&STATISTICS::n_hup, TS_STATS_SUM);
    cs.n_accept = one_stats_get(&STATISTICS::n_accept, TS_STATS_SUM);
    cs.n_polls = one_stats_get(&STATISTICS::n_polls, TS_STATS_SUM);
    cs.n_pollev = one_stats_get(&STATISTICS::n_pollev, TS_STATS_SUM);
    cs.n_nbpollev = one_stats_get(&STATISTICS::n_nbpollev, TS_STATS_SUM);
    cs.evq_avg = one_stats_get(&STATISTICS::evq_avg, TS_STATS_AVG);
    cs.evq_max = one_stats_get(&STATISTICS::evq_max, TS_STATS_MAX);
    cs.blockingpolls = one_stats_get(&STATISTICS::blockingpolls, TS_STATS_SUM);
    cs.maxqtime = one_stats_get(&STATISTICS::maxqtime, TS_STATS_MAX);
    cs.maxexectime = one_stats_get(&STATISTICS::maxexectime, TS_STATS_MAX);

    for (int i = 0; i < Worker::STATISTICS::MAXNFDS - 1; i++)
    {
        for (int j = 0; j < this_unit.next_worker_id; ++j)
        {
            Worker* pWorker = RoutingWorker::get(j);
            mxb_assert(pWorker);

            cs.n_fds[i] += pWorker->statistics().n_fds[i];
        }
    }

    for (int i = 0; i <= Worker::STATISTICS::N_QUEUE_TIMES; ++i)
    {
        int nWorkers = this_unit.next_worker_id;

        for (int j = 0; j < nWorkers; ++j)
        {
            Worker* pWorker = RoutingWorker::get(j);
            mxb_assert(pWorker);

            cs.qtimes[i] += pWorker->statistics().qtimes[i];
            cs.exectimes[i] += pWorker->statistics().exectimes[i];
        }

        cs.qtimes[i] /= (nWorkers != 0 ? nWorkers : 1);
        cs.exectimes[i] /= (nWorkers != 0 ? nWorkers : 1);
    }

    return cs;
}

// static
int64_t RoutingWorker::get_one_statistic(POLL_STAT what)
{
    int64_t rv = 0;

    int64_t Worker::STATISTICS::* member = NULL;
    enum ts_stats_type approach;

    switch (what)
    {
    case POLL_STAT_READ:
        member = &Worker::STATISTICS::n_read;
        approach = TS_STATS_SUM;
        break;

    case POLL_STAT_WRITE:
        member = &Worker::STATISTICS::n_write;
        approach = TS_STATS_SUM;
        break;

    case POLL_STAT_ERROR:
        member = &Worker::STATISTICS::n_error;
        approach = TS_STATS_SUM;
        break;

    case POLL_STAT_HANGUP:
        member = &Worker::STATISTICS::n_hup;
        approach = TS_STATS_SUM;
        break;

    case POLL_STAT_ACCEPT:
        member = &Worker::STATISTICS::n_accept;
        approach = TS_STATS_SUM;
        break;

    case POLL_STAT_EVQ_AVG:
        member = &Worker::STATISTICS::evq_avg;
        approach = TS_STATS_AVG;
        break;

    case POLL_STAT_EVQ_MAX:
        member = &Worker::STATISTICS::evq_max;
        approach = TS_STATS_MAX;
        break;

    case POLL_STAT_MAX_QTIME:
        member = &Worker::STATISTICS::maxqtime;
        approach = TS_STATS_MAX;
        break;

    case POLL_STAT_MAX_EXECTIME:
        member = &Worker::STATISTICS::maxexectime;
        approach = TS_STATS_MAX;
        break;

    default:
        mxb_assert(!true);
    }

    if (member)
    {
        rv = one_stats_get(member, approach);
    }

    return rv;
}

// static
bool RoutingWorker::get_qc_stats(int id, QC_CACHE_STATS* pStats)
{
    class Task : public Worker::Task
    {
    public:
        Task(QC_CACHE_STATS* pStats)
            : m_stats(*pStats)
        {
        }

        void execute(Worker&)
        {
            qc_get_cache_stats(&m_stats);
        }

    private:
        QC_CACHE_STATS& m_stats;
    };

    RoutingWorker* pWorker = RoutingWorker::get(id);

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
void RoutingWorker::get_qc_stats(std::vector<QC_CACHE_STATS>& all_stats)
{
    class Task : public Worker::Task
    {
    public:
        Task(std::vector<QC_CACHE_STATS>* pAll_stats)
            : m_all_stats(*pAll_stats)
        {
            m_all_stats.resize(config_threadcount());
        }

        void execute(Worker& worker)
        {
            int id = mxs::RoutingWorker::get_current_id();
            mxb_assert(id >= 0);

            QC_CACHE_STATS& stats = m_all_stats[id];

            qc_get_cache_stats(&stats);
        }

    private:
        std::vector<QC_CACHE_STATS>& m_all_stats;
    };

    Task task(&all_stats);
    mxs::RoutingWorker::execute_concurrently(task);
}

namespace
{

json_t* qc_stats_to_json(const char* zHost, int id, const QC_CACHE_STATS& stats)
{
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
std::unique_ptr<json_t> RoutingWorker::get_qc_stats_as_json(const char* zHost, int id)
{
    std::unique_ptr<json_t> sStats;

    QC_CACHE_STATS stats;

    if (get_qc_stats(id, &stats))
    {
        json_t* pJson = qc_stats_to_json(zHost, id, stats);

        stringstream self;
        self << MXS_JSON_API_QC_STATS << id;

        sStats.reset(mxs_json_resource(zHost, self.str().c_str(), pJson));
    }

    return sStats;
}

// static
std::unique_ptr<json_t> RoutingWorker::get_qc_stats_as_json(const char* zHost)
{
    vector<QC_CACHE_STATS> all_stats;

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
    static int id_generator = 0;
    int id = this_unit.id_min_worker + (atomic_add(&id_generator, 1) % this_unit.nWorkers);
    return get(id);
}
}

size_t mxs_rworker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return RoutingWorker::broadcast_message(msg_id, arg1, arg2);
}

bool mxs_rworker_register_session(MXS_SESSION* session)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    mxb_assert(pWorker);
    return pWorker->session_registry().add(session);
}

bool mxs_rworker_deregister_session(uint64_t id)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    mxb_assert(pWorker);
    return pWorker->session_registry().remove(id);
}

MXS_SESSION* mxs_rworker_find_session(uint64_t id)
{
    RoutingWorker* pWorker = RoutingWorker::get_current();
    mxb_assert(pWorker);
    return pWorker->session_registry().lookup(id);
}

MXB_WORKER* mxs_rworker_get(int worker_id)
{
    return RoutingWorker::get(worker_id);
}

MXB_WORKER* mxs_rworker_get_current()
{
    return RoutingWorker::get_current();
}

int mxs_rworker_get_current_id()
{
    return RoutingWorker::get_current_id();
}

namespace
{

using namespace maxscale;

class WorkerInfoTask : public Worker::Task
{
public:
    WorkerInfoTask(const char* zHost, uint32_t nThreads)
        : m_zHost(zHost)
    {
        m_data.resize(nThreads);
    }

    void execute(Worker& worker)
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);

        json_t* pStats = json_object();
        const Worker::STATISTICS& s = rworker.statistics();
        json_object_set_new(pStats, "reads", json_integer(s.n_read));
        json_object_set_new(pStats, "writes", json_integer(s.n_write));
        json_object_set_new(pStats, "errors", json_integer(s.n_error));
        json_object_set_new(pStats, "hangups", json_integer(s.n_hup));
        json_object_set_new(pStats, "accepts", json_integer(s.n_accept));
        json_object_set_new(pStats, "blocking_polls", json_integer(s.blockingpolls));
        // TODO: When REST-API v2 is published, remove 'event_queue_length'.
        json_object_set_new(pStats, "event_queue_length", json_integer(s.evq_avg));
        json_object_set_new(pStats, "avg_event_queue_length", json_integer(s.evq_avg));
        json_object_set_new(pStats, "max_event_queue_length", json_integer(s.evq_max));
        json_object_set_new(pStats, "max_exec_time", json_integer(s.maxexectime));
        json_object_set_new(pStats, "max_queue_time", json_integer(s.maxqtime));

        uint32_t nCurrent;
        uint64_t nTotal;
        rworker.get_descriptor_counts(&nCurrent, &nTotal);
        json_object_set_new(pStats, "current_descriptors", json_integer(nCurrent));
        json_object_set_new(pStats, "total_descriptors", json_integer(nTotal));

        json_t* load = json_object();
        json_object_set_new(load, "last_second", json_integer(rworker.load(Worker::Load::ONE_SECOND)));
        json_object_set_new(load, "last_minute", json_integer(rworker.load(Worker::Load::ONE_MINUTE)));
        json_object_set_new(load, "last_hour", json_integer(rworker.load(Worker::Load::ONE_HOUR)));
        json_object_set_new(pStats, "load", load);

        json_t* qc = qc_get_cache_stats_as_json();

        if (qc)
        {
            json_object_set_new(pStats, "query_classifier_cache", qc);
        }

        json_t* pAttr = json_object();
        json_object_set_new(pAttr, "stats", pStats);

        int idx = rworker.id();
        stringstream ss;
        ss << idx;

        json_t* pJson = json_object();
        json_object_set_new(pJson, CN_ID, json_string(ss.str().c_str()));
        json_object_set_new(pJson, CN_TYPE, json_string(CN_THREADS));
        json_object_set_new(pJson, CN_ATTRIBUTES, pAttr);
        json_object_set_new(pJson, CN_LINKS, mxs_json_self_link(m_zHost, CN_THREADS, ss.str().c_str()));

        mxb_assert((size_t)idx < m_data.size());
        m_data[idx] = pJson;
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

    json_t* resource(int id)
    {
        stringstream self;
        self << MXS_JSON_API_THREADS << id;
        return mxs_json_resource(m_zHost, self.str().c_str(), m_data[id]);
    }

private:
    vector<json_t*> m_data;
    const char*     m_zHost;
};

class FunctionTask : public Worker::DisposableTask
{
public:
    FunctionTask(std::function<void ()> cb)
        : m_cb(cb)
    {
    }

    void execute(Worker& worker)
    {
        m_cb();
    }

protected:
    std::function<void ()> m_cb;
};
}

size_t mxs_rworker_broadcast(void (* cb)(void* data), void* data)
{
    std::unique_ptr<FunctionTask> task(new FunctionTask([cb, data]() {
                                                            cb(data);
                                                        }));

    return RoutingWorker::broadcast(std::move(task));
}

uint64_t mxs_rworker_create_key()
{
    return RoutingWorker::create_key();
}

void mxs_rworker_set_data(uint64_t key, void* data, void (* callback)(void*))
{
    RoutingWorker::get_current()->set_data(key, data, callback);
}

void* mxs_rworker_get_data(uint64_t key)
{
    return RoutingWorker::get_current()->get_data(key);
}

void mxs_rworker_delete_data(uint64_t key)
{
    auto func = [key]() {
            RoutingWorker::get_current()->delete_data(key);
        };

    std::unique_ptr<FunctionTask> task(new FunctionTask(func));
    RoutingWorker::broadcast(std::move(task));
}

json_t* mxs_rworker_to_json(const char* zHost, int id)
{
    Worker* target = RoutingWorker::get(id);
    WorkerInfoTask task(zHost, id + 1);
    Semaphore sem;

    target->execute(&task, &sem, Worker::EXECUTE_AUTO);
    sem.wait();

    return task.resource(id);
}

json_t* mxs_rworker_list_to_json(const char* host)
{
    WorkerInfoTask task(host, config_threadcount());
    RoutingWorker::execute_concurrently(task);
    return task.resource();
}
