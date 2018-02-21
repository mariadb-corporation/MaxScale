/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/worker.hh"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/config.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/log_manager.h>
#include <maxscale/platform.h>
#include <maxscale/semaphore.hh>
#include <maxscale/json_api.h>
#include <maxscale/utils.hh>

#include "internal/dcb.h"
#include "internal/modules.h"
#include "internal/poll.h"
#include "internal/service.h"
#include "internal/statistics.h"
#include "internal/workertask.hh"

#define WORKER_ABSENT_ID -1

using maxscale::Worker;
using maxscale::WorkerLoad;
using maxscale::Closer;
using maxscale::Semaphore;
using std::vector;
using std::stringstream;

namespace
{

const int MXS_WORKER_MSG_TASK = -1;
const int MXS_WORKER_MSG_DISPOSABLE_TASK = -2;

/**
 * Unit variables.
 */
struct this_unit
{
    bool     initialized;       // Whether the initialization has been performed.
    int      n_workers;         // How many workers there are.
    Worker** ppWorkers;         // Array of worker instances.
    // DEPRECATED in 2.3, remove in 2.4.
    int      number_poll_spins; // Maximum non-block polls
    // DEPRECATED in 2.3, remove in 2.4.
    int      max_poll_sleep;    // Maximum block time
    int      epoll_listener_fd; // Shared epoll descriptor for listening descriptors.
} this_unit =
{
    false,
    0,
    NULL,
    0,
    0
};

thread_local struct this_thread
{
    int current_worker_id; // The worker id of the current thread
} this_thread =
{
    WORKER_ABSENT_ID
};

/**
 * Structure used for sending cross-thread messages.
 */
typedef struct worker_message
{
    uint32_t id;   /*< Message id. */
    intptr_t arg1; /*< Message specific first argument. */
    intptr_t arg2; /*< Message specific second argument. */
} WORKER_MESSAGE;

/**
 * Check error returns from epoll_ctl; impossible ones lead to crash.
 *
 * @param errornum   The errno set by epoll_ctl
 * @param op         Either EPOLL_CTL_ADD or EPOLL_CTL_DEL.
 */
void poll_resolve_error(int fd, int errornum, int op)
{
    if (op == EPOLL_CTL_ADD)
    {
        if (EEXIST == errornum)
        {
            MXS_ERROR("File descriptor %d already present in an epoll instance.", fd);
            return;
        }

        if (ENOSPC == errornum)
        {
            MXS_ERROR("The limit imposed by /proc/sys/fs/epoll/max_user_watches was "
                      "reached when trying to add file descriptor %d to an epoll instance.", fd);
            return;
        }
    }
    else
    {
        ss_dassert(op == EPOLL_CTL_DEL);

        /* Must be removing */
        if (ENOENT == errornum)
        {
            MXS_ERROR("File descriptor %d was not found in epoll instance.", fd);
            return;
        }
    }

    /* Common checks for add or remove - crash MaxScale */
    if (EBADF == errornum)
    {
        raise(SIGABRT);
    }
    if (EINVAL == errornum)
    {
        raise(SIGABRT);
    }
    if (ENOMEM == errornum)
    {
        raise(SIGABRT);
    }
    if (EPERM == errornum)
    {
        raise(SIGABRT);
    }

    /* Undocumented error number */
    raise(SIGABRT);
}

}

static bool modules_thread_init();
static void modules_thread_finish();

WorkerLoad::WorkerLoad()
    : m_start_time(0)
    , m_wait_start(0)
    , m_wait_time(0)
    , m_load_1_minute(&m_load_1_hour)
    , m_load_1_second(&m_load_1_minute)
{
}

void WorkerLoad::about_to_work(uint64_t now)
{
    uint64_t duration = now - m_start_time;

    m_wait_time += (now - m_wait_start);

    if (duration > ONE_SECOND)
    {
        int load_percentage = 100 * ((duration - m_wait_time) / (double)duration);

        m_start_time = now;
        m_wait_time = 0;

        m_load_1_second.add_value(load_percentage);
    }
}

WorkerLoad::Average::~Average()
{
}

//static
uint64_t WorkerLoad::get_time()
{
    uint64_t now;

    timespec t;

    ss_debug(int rv=)clock_gettime(CLOCK_MONOTONIC, &t);
    ss_dassert(rv == 0);

    return t.tv_sec * 1000 + (t.tv_nsec / 1000000);
}

Worker::Worker(int id,
               int epoll_fd)
    : m_id(id)
    , m_state(STOPPED)
    , m_epoll_fd(epoll_fd)
    , m_pQueue(NULL)
    , m_thread(0)
    , m_started(false)
    , m_should_shutdown(false)
    , m_shutdown_initiated(false)
    , m_nCurrent_descriptors(0)
    , m_nTotal_descriptors(0)
{
    MXS_POLL_DATA::handler = &Worker::epoll_instance_handler;
    MXS_POLL_DATA::thread.id = id;
}

Worker::~Worker()
{
    ss_dassert(!m_started);

    delete m_pQueue;
    close(m_epoll_fd);
}

// static
bool Worker::init()
{
    ss_dassert(!this_unit.initialized);

    this_unit.n_workers = config_threadcount();
    this_unit.number_poll_spins = config_nbpolls();
    this_unit.max_poll_sleep = config_pollsleep();

    this_unit.epoll_listener_fd = epoll_create(MAX_EVENTS);

    if (this_unit.epoll_listener_fd != -1)
    {
        this_unit.ppWorkers = new (std::nothrow) Worker* [this_unit.n_workers] (); // Zero initialized array

        if (this_unit.ppWorkers)
        {
            for (int i = 0; i < this_unit.n_workers; ++i)
            {
                Worker* pWorker = Worker::create(i, this_unit.epoll_listener_fd);

                if (pWorker)
                {
                    this_unit.ppWorkers[i] = pWorker;
                }
                else
                {
                    for (int j = i - 1; j >= 0; --j)
                    {
                        delete this_unit.ppWorkers[j];
                    }

                    delete this_unit.ppWorkers;
                    this_unit.ppWorkers = NULL;
                    break;
                }
            }

            if (this_unit.ppWorkers)
            {
                this_unit.initialized = true;
            }
        }
        else
        {
            close(this_unit.epoll_listener_fd);
        }
    }
    else
    {
        MXS_ERROR("Could not allocate an epoll instance.");
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

void Worker::finish()
{
    ss_dassert(this_unit.initialized);

    for (int i = this_unit.n_workers - 1; i >= 0; --i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        delete pWorker;
        this_unit.ppWorkers[i] = NULL;
    }

    delete [] this_unit.ppWorkers;
    this_unit.ppWorkers = NULL;

    close(this_unit.epoll_listener_fd);
    this_unit.epoll_listener_fd = 0;

    this_unit.initialized = false;
}

namespace
{

int64_t one_stats_get(int64_t Worker::STATISTICS::*what, enum ts_stats_type type)
{
    int64_t best = type == TS_STATS_MAX ? LONG_MIN : (type == TS_STATS_MIX ? LONG_MAX : 0);

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = Worker::get(i);
        ss_dassert(pWorker);

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

    return type == TS_STATS_AVG ? best / this_unit.n_workers : best;
}

}

//static
Worker::STATISTICS Worker::get_statistics()
{
    STATISTICS cs;

    cs.n_read        = one_stats_get(&STATISTICS::n_read, TS_STATS_SUM);
    cs.n_write       = one_stats_get(&STATISTICS::n_write, TS_STATS_SUM);
    cs.n_error       = one_stats_get(&STATISTICS::n_error, TS_STATS_SUM);
    cs.n_hup         = one_stats_get(&STATISTICS::n_hup, TS_STATS_SUM);
    cs.n_accept      = one_stats_get(&STATISTICS::n_accept, TS_STATS_SUM);
    cs.n_polls       = one_stats_get(&STATISTICS::n_polls, TS_STATS_SUM);
    cs.n_pollev      = one_stats_get(&STATISTICS::n_pollev, TS_STATS_SUM);
    cs.n_nbpollev    = one_stats_get(&STATISTICS::n_nbpollev, TS_STATS_SUM);
    cs.evq_length    = one_stats_get(&STATISTICS::evq_length, TS_STATS_AVG);
    cs.evq_max       = one_stats_get(&STATISTICS::evq_max, TS_STATS_MAX);
    cs.blockingpolls = one_stats_get(&STATISTICS::blockingpolls, TS_STATS_SUM);
    cs.maxqtime      = one_stats_get(&STATISTICS::maxqtime, TS_STATS_MAX);
    cs.maxexectime   = one_stats_get(&STATISTICS::maxexectime, TS_STATS_MAX);

    for (int i = 0; i < Worker::STATISTICS::MAXNFDS - 1; i++)
    {
        for (int j = 0; j < this_unit.n_workers; ++j)
        {
            Worker* pWorker = Worker::get(j);
            ss_dassert(pWorker);

            cs.n_fds[i] += pWorker->statistics().n_fds[i];
        }
    }

    for (int i = 0; i <= Worker::STATISTICS::N_QUEUE_TIMES; ++i)
    {
        for (int j = 0; j < this_unit.n_workers; ++j)
        {
            Worker* pWorker = Worker::get(j);
            ss_dassert(pWorker);

            cs.qtimes[i] += pWorker->statistics().qtimes[i];
            cs.exectimes[i] += pWorker->statistics().exectimes[i];
        }

        cs.qtimes[i] /= this_unit.n_workers;
        cs.exectimes[i] /= this_unit.n_workers;
    }

    return cs;
}

//static
int64_t Worker::get_one_statistic(POLL_STAT what)
{
    int64_t rv = 0;

    int64_t Worker::STATISTICS::*member = NULL;
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

    case POLL_STAT_EVQ_LEN:
        member = &Worker::STATISTICS::evq_length;
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
        ss_dassert(!true);
    }

    if (member)
    {
        rv = one_stats_get(member, approach);
    }

    return rv;
}

void Worker::get_descriptor_counts(uint32_t* pnCurrent, uint64_t* pnTotal)
{
    *pnCurrent = atomic_load_uint32(&m_nCurrent_descriptors);
    *pnTotal = atomic_load_uint64(&m_nTotal_descriptors);
}

bool Worker::add_fd(int fd, uint32_t events, MXS_POLL_DATA* pData)
{
    bool rv = true;

    // Must be edge-triggered.
    events |= EPOLLET;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->thread.id = m_id;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0)
    {
        atomic_add_uint32(&m_nCurrent_descriptors, 1);
        atomic_add_uint64(&m_nTotal_descriptors, 1);
    }
    else
    {
        poll_resolve_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

//static
bool Worker::add_shared_fd(int fd, uint32_t events, MXS_POLL_DATA* pData)
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

    pData->thread.id = 0; // TODO: Remove the thread id altogether.

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        poll_resolve_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

bool Worker::remove_fd(int fd)
{
    bool rv = true;

    struct epoll_event ev = {};

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, &ev) == 0)
    {
        atomic_add_uint32(&m_nCurrent_descriptors, -1);
    }
    else
    {
        poll_resolve_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

//static
bool Worker::remove_shared_fd(int fd)
{
    bool rv = true;

    struct epoll_event ev = {};

    if (epoll_ctl(this_unit.epoll_listener_fd, EPOLL_CTL_DEL, fd, &ev) != 0)
    {
        poll_resolve_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

int mxs_worker_id(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->id();
}

bool mxs_worker_should_shutdown(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->should_shutdown();
}

Worker* Worker::get(int worker_id)
{
    ss_dassert(worker_id < this_unit.n_workers);

    return this_unit.ppWorkers[worker_id];
}

MXS_WORKER* mxs_worker_get(int worker_id)
{
    return Worker::get(worker_id);
}

int mxs_worker_get_current_id()
{
    return Worker::get_current_id();
}

Worker* Worker::get_current()
{
    Worker* pWorker = NULL;

    int worker_id = get_current_id();

    if (worker_id != WORKER_ABSENT_ID)
    {
        pWorker = Worker::get(worker_id);
    }

    return pWorker;
}

int Worker::get_current_id()
{
    return this_thread.current_worker_id;
}

//static
void Worker::set_nonblocking_polls(unsigned int nbpolls)
{
    this_unit.number_poll_spins = nbpolls;
}

//static
void Worker::set_maxwait(unsigned int maxwait)
{
    this_unit.max_poll_sleep = maxwait;
}

bool Worker::post(Task* pTask, Semaphore* pSem, enum execute_mode_t mode)
{
    // No logging here, function must be signal safe.
    bool rval = true;

    if (mode == Worker::EXECUTE_AUTO && Worker::get_current() == this)
    {
        pTask->execute(*this);

        if (pSem)
        {
            pSem->post();
        }
    }
    else
    {
        intptr_t arg1 = reinterpret_cast<intptr_t>(pTask);
        intptr_t arg2 = reinterpret_cast<intptr_t>(pSem);

        rval = post_message(MXS_WORKER_MSG_TASK, arg1, arg2);
    }

    return rval;
}

bool Worker::post(std::auto_ptr<DisposableTask> sTask, enum execute_mode_t mode)
{
    // No logging here, function must be signal safe.
    return post_disposable(sTask.release(), mode);
}

// private
bool Worker::post_disposable(DisposableTask* pTask, enum execute_mode_t mode)
{
    bool posted = true;

    pTask->inc_ref();

    if (mode == Worker::EXECUTE_AUTO && Worker::get_current() == this)
    {
        pTask->execute(*this);
        pTask->dec_ref();
    }
    else
    {
        intptr_t arg1 = reinterpret_cast<intptr_t>(pTask);

        posted = post_message(MXS_WORKER_MSG_DISPOSABLE_TASK, arg1, 0);

        if (!posted)
        {
            pTask->dec_ref();
        }
    }

    return posted;
}

//static
size_t Worker::broadcast(Task* pTask, Semaphore* pSem)
{
    // No logging here, function must be signal safe.
    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        if (pWorker->post(pTask, pSem))
        {
            ++n;
        }
    }

    return n;
}

//static
size_t Worker::broadcast(std::auto_ptr<DisposableTask> sTask)
{
    DisposableTask* pTask = sTask.release();
    pTask->inc_ref();

    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        if (pWorker->post_disposable(pTask))
        {
            ++n;
        }
    }

    pTask->dec_ref();

    return n;
}

//static
size_t Worker::execute_serially(Task& task)
{
    Semaphore sem;
    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        if (pWorker->post(&task, &sem))
        {
            sem.wait();
            ++n;
        }
    }

    return n;
}

//static
size_t Worker::execute_concurrently(Task& task)
{
    Semaphore sem;
    return sem.wait_n(Worker::broadcast(&task, &sem));
}

bool Worker::post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.
    MessageQueue::Message message(msg_id, arg1, arg2);

    return m_pQueue->post(message);
}

bool mxs_worker_post_message(MXS_WORKER* pWorker, uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return static_cast<Worker*>(pWorker)->post_message(msg_id, arg1, arg2);
}

size_t Worker::broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        if (pWorker->post_message(msg_id, arg1, arg2))
        {
            ++n;
        }
    }

    return n;
}

size_t mxs_worker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return Worker::broadcast_message(msg_id, arg1, arg2);
}

bool mxs_worker_register_session(MXS_SESSION* session)
{
    Worker* worker = Worker::get_current();
    ss_dassert(worker);
    return worker->session_registry().add(session);
}

bool mxs_worker_deregister_session(uint64_t id)
{
    Worker* worker = Worker::get_current();
    ss_dassert(worker);
    return worker->session_registry().remove(id);
}

MXS_SESSION* mxs_worker_find_session(uint64_t id)
{
    Worker* worker = Worker::get_current();
    ss_dassert(worker);
    return worker->session_registry().lookup(id);
}

Worker::SessionsById& Worker::session_registry()
{
    return m_sessions;
}

class WorkerInfoTask: public maxscale::WorkerTask
{
public:
    WorkerInfoTask(const char* host, uint32_t nthreads):
        m_host(host)
    {
        m_data.resize(nthreads);
    }

    void execute(Worker& worker)
    {
        json_t* stats = json_object();
        const Worker::STATISTICS& s = worker.get_local_statistics();
        json_object_set_new(stats, "reads", json_integer(s.n_read));
        json_object_set_new(stats, "writes", json_integer(s.n_write));
        json_object_set_new(stats, "errors", json_integer(s.n_error));
        json_object_set_new(stats, "hangups", json_integer(s.n_hup));
        json_object_set_new(stats, "accepts", json_integer(s.n_accept));
        json_object_set_new(stats, "blocking_polls", json_integer(s.blockingpolls));
        json_object_set_new(stats, "event_queue_length", json_integer(s.evq_length));
        json_object_set_new(stats, "max_event_queue_length", json_integer(s.evq_max));
        json_object_set_new(stats, "max_exec_time", json_integer(s.maxexectime));
        json_object_set_new(stats, "max_queue_time", json_integer(s.maxqtime));

        json_t* attr = json_object();
        json_object_set_new(attr, "stats", stats);

        int idx = worker.get_current_id();
        stringstream ss;
        ss << idx;

        json_t* json = json_object();
        json_object_set_new(json, CN_ID, json_string(ss.str().c_str()));
        json_object_set_new(json, CN_TYPE, json_string(CN_THREADS));
        json_object_set_new(json, CN_ATTRIBUTES, attr);
        json_object_set_new(json, CN_LINKS, mxs_json_self_link(m_host, CN_THREADS, ss.str().c_str()));

        ss_dassert((size_t)idx < m_data.size());
        m_data[idx] = json;
    }

    json_t* resource()
    {
        json_t* arr = json_array();

        for (vector<json_t*>::iterator it = m_data.begin(); it != m_data.end(); it++)
        {
            json_array_append_new(arr, *it);
        }

        return mxs_json_resource(m_host, MXS_JSON_API_THREADS, arr);
    }

    json_t* resource(int id)
    {
        stringstream self;
        self << MXS_JSON_API_THREADS << id;
        return mxs_json_resource(m_host, self.str().c_str(), m_data[id]);
    }

private:
    vector<json_t*> m_data;
    const char*     m_host;
};

json_t* mxs_worker_to_json(const char* host, int id)
{
    Worker* target = Worker::get(id);
    WorkerInfoTask task(host, id + 1);
    Semaphore sem;

    target->post(&task, &sem);
    sem.wait();

    return task.resource(id);
}

json_t* mxs_worker_list_to_json(const char* host)
{
    WorkerInfoTask task(host, config_threadcount());
    Worker::execute_concurrently(task);
    return task.resource();
}

void Worker::register_zombie(DCB* pDcb)
{
    ss_dassert(pDcb->poll.thread.id == m_id);

    m_zombies.push_back(pDcb);
}

void Worker::delete_zombies()
{
    // An algorithm cannot be used, as the final closing of a DCB may cause
    // other DCBs to be registered in the zombie queue.

    while (!m_zombies.empty())
    {
        DCB* pDcb = m_zombies.back();
        m_zombies.resize(m_zombies.size() - 1);
        dcb_final_close(pDcb);
    }
}

void Worker::run()
{
    this_thread.current_worker_id = m_id;

    if (modules_thread_init() && service_thread_init())
    {
        poll_waitevents();

        MXS_INFO("Worker %d has shut down.", m_id);
        modules_thread_finish();
    }
    else
    {
        MXS_ERROR("Could not perform thread initialization for all modules. Thread exits.");
    }

    this_thread.current_worker_id = WORKER_ABSENT_ID;
}

bool Worker::start(size_t stack_size)
{
    m_started = true;

    if (!thread_start(&m_thread, &Worker::thread_main, this, stack_size))
    {
        m_started = false;
    }

    return m_started;
}

void Worker::join()
{
    if (m_started)
    {
        MXS_INFO("Waiting for worker %d.", m_id);
        thread_wait(m_thread);
        MXS_INFO("Waited for worker %d.", m_id);
        m_started = false;
    }
}

void Worker::shutdown()
{
    // NOTE: No logging here, this function must be signal safe.

    if (!m_shutdown_initiated)
    {
        if (post_message(MXS_WORKER_MSG_SHUTDOWN, 0, 0))
        {
            m_shutdown_initiated = true;
        }
    }
}

void Worker::shutdown_all()
{
    // NOTE: No logging here, this function must be signal safe.

    for (int i = 0; i < this_unit.n_workers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];

        pWorker->shutdown();
    }
}

/**
 * Creates a worker instance.
 * - Allocates the structure.
 * - Creates a pipe.
 * - Adds the read descriptor to the polling mechanism.
 *
 * @param worker_id          The id of the worker.
 * @param epoll_listener_fd  The file descriptor of the epoll set to which listening
 *                           sockets will be placed.
 *
 * @return A worker instance if successful, otherwise NULL.
 */
//static
Worker* Worker::create(int worker_id, int epoll_listener_fd)
{
    Worker* pThis = NULL;

    int epoll_fd = epoll_create(MAX_EVENTS);

    if (epoll_fd != -1)
    {
        pThis = new (std::nothrow) Worker(worker_id, epoll_fd);

        if (pThis)
        {
            struct epoll_event ev;
            ev.events = EPOLLIN;
            MXS_POLL_DATA* pData = pThis;
            ev.data.ptr = pData; // Necessary for pointer adjustment, otherwise downcast will not work.

            // The shared epoll instance descriptor is *not* added using EPOLLET (edge-triggered)
            // because we want it to be level-triggered. That way, as long as there is a single
            // active (accept() can be called) listening socket, epoll_wait() will return an event
            // for it. It must be like that because each worker will call accept() just once before
            // calling epoll_wait() again. The end result is that as long as the load of different
            // workers is roughly the same, the client connections will be distributed evenly across
            // the workers. If the load is not the same, then a worker with less load will get more
            // clients that a worker with more load.
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, epoll_listener_fd, &ev) == 0)
            {
                MXS_INFO("Epoll instance for listening sockets added to worker epoll instance.");

                MessageQueue* pQueue = MessageQueue::create(pThis);

                if (pQueue)
                {
                    if (pQueue->add_to_worker(pThis))
                    {
                        pThis->m_pQueue = pQueue;
                    }
                    else
                    {
                        MXS_ERROR("Could not add message queue to worker.");
                        delete pThis;
                        pThis = NULL;
                    }
                }
                else
                {
                    MXS_ERROR("Could not create message queue for worker.");
                    delete pThis;
                    pThis = NULL;
                }
            }
            else
            {
                MXS_ERROR("Could not add epoll instance for listening sockets to "
                          "epoll instance of worker: %s", mxs_strerror(errno));
                delete pThis;
                pThis = NULL;
            }
        }
        else
        {
            MXS_OOM();
            close(epoll_fd);
        }
    }
    else
    {
        MXS_ERROR("Could not create epoll-instance for worker: %s", mxs_strerror(errno));
    }

    return pThis;
}

/**
 * The worker message handler.
 *
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 */
void Worker::handle_message(MessageQueue& queue, const MessageQueue::Message& msg)
{
    switch  (msg.id())
    {
    case MXS_WORKER_MSG_PING:
        {
            ss_dassert(msg.arg1() == 0);
            char* zArg2 = reinterpret_cast<char*>(msg.arg2());
            const char* zMessage = zArg2 ? zArg2 : "Alive and kicking";
            MXS_NOTICE("Worker[%d]: %s.", m_id, zMessage);
            MXS_FREE(zArg2);
        }
        break;

    case MXS_WORKER_MSG_SHUTDOWN:
        {
            MXS_INFO("Worker %d received shutdown message.", m_id);
            m_should_shutdown = true;
        }
        break;

    case MXS_WORKER_MSG_CALL:
        {
            void (*f)(int, void*) = (void (*)(int, void*))msg.arg1();

            f(m_id, (void*)msg.arg2());
        }
        break;

    case MXS_WORKER_MSG_TASK:
        {
            Task *pTask = reinterpret_cast<Task*>(msg.arg1());
            Semaphore* pSem = reinterpret_cast<Semaphore*>(msg.arg2());

            pTask->execute(*this);

            if (pSem)
            {
                pSem->post();
            }
        }
        break;

    case MXS_WORKER_MSG_DISPOSABLE_TASK:
        {
            DisposableTask *pTask = reinterpret_cast<DisposableTask*>(msg.arg1());
            pTask->execute(*this);
            pTask->dec_ref();
        }
        break;

    default:
        MXS_ERROR("Worker received unknown message %d.", msg.id());
    }
}

/**
 * The entry point of each worker thread.
 *
 * @param arg A worker.
 */
//static
void Worker::thread_main(void* pArg)
{
    Worker* pWorker = static_cast<Worker*>(pArg);
    pWorker->run();
}

/**
 * The main polling loop
 */
void Worker::poll_waitevents()
{
    struct epoll_event events[MAX_EVENTS];

    m_state = IDLE;

    m_load.reset();

    while (!should_shutdown())
    {
        int nfds;

        m_state = POLLING;

        atomic_add_int64(&m_statistics.n_polls, 1);

        uint64_t now = Load::get_time();
        int timeout = Load::GRANULARITY - (now - m_load.start_time());

        if (timeout < 0)
        {
            // If the processing of the last batch of events took us past the next
            // time boundary, we ensure we return immediately.
            timeout = 0;
        }

        m_load.about_to_wait(now);
        nfds = epoll_wait(m_epoll_fd, events, MAX_EVENTS, timeout);
        m_load.about_to_work();

        if (nfds == -1)
        {
            int eno = errno;
            errno = 0;
            MXS_ERROR("%lu [poll_waitevents] epoll_wait returned "
                      "%d, errno %d",
                      pthread_self(),
                      nfds,
                      eno);
        }

        if (nfds > 0)
        {
            m_statistics.evq_length = nfds;
            if (nfds > m_statistics.evq_max)
            {
                m_statistics.evq_max = nfds;
            }

            MXS_DEBUG("%lu [poll_waitevents] epoll_wait found %d fds",
                      pthread_self(),
                      nfds);
            atomic_add_int64(&m_statistics.n_pollev, 1);

            m_state = PROCESSING;

            m_statistics.n_fds[(nfds < STATISTICS::MAXNFDS ? (nfds - 1) : STATISTICS::MAXNFDS - 1)]++;
        }

        uint64_t cycle_start = hkheartbeat;

        for (int i = 0; i < nfds; i++)
        {
            /** Calculate event queue statistics */
            int64_t started = hkheartbeat;
            int64_t qtime = started - cycle_start;

            if (qtime > STATISTICS::N_QUEUE_TIMES)
            {
                m_statistics.qtimes[STATISTICS::N_QUEUE_TIMES]++;
            }
            else
            {
                m_statistics.qtimes[qtime]++;
            }

            m_statistics.maxqtime = MXS_MAX(m_statistics.maxqtime, qtime);

            MXS_POLL_DATA *data = (MXS_POLL_DATA*)events[i].data.ptr;

            uint32_t actions = data->handler(data, m_id, events[i].events);

            if (actions & MXS_POLL_ACCEPT)
            {
                atomic_add_int64(&m_statistics.n_accept, 1);
            }

            if (actions & MXS_POLL_READ)
            {
                atomic_add_int64(&m_statistics.n_read, 1);
            }

            if (actions & MXS_POLL_WRITE)
            {
                atomic_add_int64(&m_statistics.n_write, 1);
            }

            if (actions & MXS_POLL_HUP)
            {
                atomic_add_int64(&m_statistics.n_hup, 1);
            }

            if (actions & MXS_POLL_ERROR)
            {
                atomic_add_int64(&m_statistics.n_error, 1);
            }

            /** Calculate event execution statistics */
            qtime = hkheartbeat - started;

            if (qtime > STATISTICS::N_QUEUE_TIMES)
            {
                m_statistics.exectimes[STATISTICS::N_QUEUE_TIMES]++;
            }
            else
            {
                m_statistics.exectimes[qtime % STATISTICS::N_QUEUE_TIMES]++;
            }

            m_statistics.maxexectime = MXS_MAX(m_statistics.maxexectime, qtime);
        }

        dcb_process_idle_sessions(m_id);

        m_state = ZPROCESSING;

        delete_zombies();

        m_state = IDLE;
    } /*< while(1) */

    m_state = STOPPED;
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
//static
uint32_t Worker::epoll_instance_handler(struct mxs_poll_data* pData, int wid, uint32_t events)
{
    Worker* pWorker = static_cast<Worker*>(pData);
    ss_dassert(pWorker->m_id == wid);

    return pWorker->handle_epoll_events(events);
}

/**
 * Handler for events occurring in the shared epoll instance.
 *
 * @param events  The events.
 *
 * @return What actions were performed.
 */
uint32_t Worker::handle_epoll_events(uint32_t events)
{
    struct epoll_event epoll_events[1];

    // We extract just one event
    int nfds = epoll_wait(this_unit.epoll_listener_fd, epoll_events, 1, 0);

    uint32_t actions = MXS_POLL_NOP;

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
        MXS_POLL_DATA* pData = static_cast<MXS_POLL_DATA*>(epoll_events[0].data.ptr);

        actions = pData->handler(pData, m_id, epoll_events[0].events);
    }

    return actions;
}

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
static bool modules_thread_init()
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
static void modules_thread_finish()
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
