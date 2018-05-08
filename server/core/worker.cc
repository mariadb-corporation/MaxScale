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
#include <sys/timerfd.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/config.h>
#include <maxscale/clock.h>
#include <maxscale/limits.h>
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

using std::vector;
using std::stringstream;

namespace
{

using maxscale::Worker;

const int MXS_WORKER_MSG_TASK = -1;
const int MXS_WORKER_MSG_DISPOSABLE_TASK = -2;

/**
 * Unit variables.
 */
struct this_unit
{
    bool     initialized;    // Whether the initialization has been performed.
    Worker** ppWorkers;      // Array of worker instances.
    int      next_worker_id; // Next worker id.
} this_unit =
{
    false, // initialized
    NULL,  // ppWorkers
    0,     // next_worker_id
};

int next_worker_id()
{
    return atomic_add(&this_unit.next_worker_id, 1);
}

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

}

static bool modules_thread_init();
static void modules_thread_finish();

namespace maxscale
{

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

namespace
{

int create_timerfd()
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    if (fd == -1)
    {
        if (errno == EINVAL)
        {
            // Ok, we may be running on an old kernel, let's try again but without flags.
            fd = timerfd_create(CLOCK_MONOTONIC, 0);

            if (fd != -1)
            {
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags != -1)
                {
                    flags |= O_NONBLOCK;
                    if (fcntl(fd, F_SETFL, flags) == -1)
                    {
                        MXS_ALERT("Could not make timer fd non-blocking, MaxScale will not work: %s",
                                  mxs_strerror(errno));
                        close(fd);
                        fd = -1;
                        ss_dassert(!true);
                    }
                }
                else
                {
                    MXS_ALERT("Could not get timer fd flags, MaxScale will not work: %s",
                              mxs_strerror(errno));
                    close(fd);
                    fd = -1;
                    ss_dassert(!true);
                }
            }
            else
            {
                MXS_ALERT("Could not create timer file descriptor even with no flags, MaxScale "
                          "will not work: %s", mxs_strerror(errno));
                ss_dassert(!true);
            }
        }
        else
        {
            MXS_ALERT("Could not create timer file descriptor, MaxScale will not work: %s",
                      mxs_strerror(errno));
            ss_dassert(!true);
        }
    }

    return fd;
}

}

WorkerTimer::WorkerTimer(Worker* pWorker)
    : m_fd(create_timerfd())
    , m_pWorker(pWorker)
{
    MXS_POLL_DATA::handler = handler;
    MXS_POLL_DATA::thread.id = m_pWorker->id();

    if (m_fd != -1)
    {
        if (!m_pWorker->add_fd(m_fd, EPOLLIN, this))
        {
            MXS_ALERT("Could not add timer descriptor to worker, MaxScale will not work.");
            ::close(m_fd);
            m_fd = -1;
            ss_dassert(!true);
        }
    }
}

WorkerTimer::~WorkerTimer()
{
    if (m_fd != -1)
    {
        if (!m_pWorker->remove_fd(m_fd))
        {
            MXS_ERROR("Could not remove timer fd from worker.");
        }

        ::close(m_fd);
    }
}

void WorkerTimer::start(int32_t interval)
{
    ss_dassert(interval > 0);

    // TODO: Add possibility to set initial delay and interval.
    time_t initial_sec = interval / 1000;
    long initial_nsec = (interval - initial_sec * 1000) * 1000000;

    time_t interval_sec = (interval / 1000);
    long interval_nsec = (interval - interval_sec * 1000) * 1000000;

    struct itimerspec time;

    time.it_value.tv_sec = initial_sec;
    time.it_value.tv_nsec = initial_nsec;
    time.it_interval.tv_sec = interval_sec;
    time.it_interval.tv_nsec = interval_nsec;

    if (timerfd_settime(m_fd, 0, &time, NULL) != 0)
    {
        MXS_ERROR("Could not set timer settings.");
    }
}

void WorkerTimer::cancel()
{
    start(0);
}

uint32_t WorkerTimer::handle(int wid, uint32_t events)
{
    ss_dassert(wid == m_pWorker->id());
    ss_dassert(events & EPOLLIN);
    ss_dassert((events & ~EPOLLIN) == 0);

    // Read all events
    uint64_t expirations;
    while (read(m_fd, &expirations, sizeof(expirations)) == 0)
    {
    }

    tick();

    return MXS_POLL_READ;
}

//static
uint32_t WorkerTimer::handler(MXS_POLL_DATA* pThis, int wid, uint32_t events)
{
    return static_cast<WorkerTimer*>(pThis)->handle(wid, events);
}

namespace
{

int create_epoll_instance()
{
    int fd = ::epoll_create(MAX_EVENTS);

    if (fd == -1)
    {
        MXS_ALERT("Could not create epoll-instance for worker, MaxScale will not work: %s",
                  mxs_strerror(errno));
        ss_dassert(!true);
    }

    return fd;
}

}

//static
uint32_t Worker::s_next_delayed_call_id = 1;

Worker::Worker()
    : m_id(next_worker_id())
    , m_epoll_fd(create_epoll_instance())
    , m_state(STOPPED)
    , m_pQueue(NULL)
    , m_thread(0)
    , m_started(false)
    , m_should_shutdown(false)
    , m_shutdown_initiated(false)
    , m_nCurrent_descriptors(0)
    , m_nTotal_descriptors(0)
    , m_pTimer(new PrivateTimer(this, this, &Worker::tick))
{
    if (m_epoll_fd != -1)
    {
        m_pQueue = MessageQueue::create(this);

        if (m_pQueue)
        {
            if (!m_pQueue->add_to_worker(this))
            {
                MXS_ALERT("Could not add message queue to worker, MaxScale will not work.");
                ss_dassert(!true);
            }
        }
        else
        {
            MXS_ALERT("Could not create message queue for worker, MaxScale will not work.");
            ss_dassert(!true);
        }
    }

    this_unit.ppWorkers[m_id] = this;
}

Worker::~Worker()
{
    this_unit.ppWorkers[m_id] = NULL;

    ss_dassert(!m_started);

    delete m_pTimer;
    delete m_pQueue;
    close(m_epoll_fd);

    // When going down, we need to cancel all pending calls.
    for (auto i = m_calls.begin(); i != m_calls.end(); ++i)
    {
        i->second->call(Call::CANCEL);
        delete i->second;
    }
}

// static
bool Worker::init()
{
    ss_dassert(!this_unit.initialized);

    Worker** ppWorkers = new (std::nothrow) Worker* [MXS_MAX_THREADS] (); // Zero initialized array

    if (ppWorkers)
    {
        this_unit.ppWorkers = ppWorkers;

        this_unit.initialized = true;
    }
    else
    {
        MXS_OOM();
        ss_dassert(!true);
    }

    return this_unit.initialized;
}

void Worker::finish()
{
    ss_dassert(this_unit.initialized);

    delete [] this_unit.ppWorkers;
    this_unit.ppWorkers = NULL;

    this_unit.initialized = false;
}

namespace
{

int64_t one_stats_get(int64_t Worker::STATISTICS::*what, enum ts_stats_type type)
{
    int64_t best = type == TS_STATS_MAX ? LONG_MIN : (type == TS_STATS_MIX ? LONG_MAX : 0);

    int nWorkers = this_unit.next_worker_id;

    for (int i = 0; i < nWorkers; ++i)
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

    return type == TS_STATS_AVG ? best / (nWorkers != 0 ? nWorkers : 1) : best;
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
        for (int j = 0; j < this_unit.next_worker_id; ++j)
        {
            Worker* pWorker = Worker::get(j);
            ss_dassert(pWorker);

            cs.n_fds[i] += pWorker->statistics().n_fds[i];
        }
    }

    for (int i = 0; i <= Worker::STATISTICS::N_QUEUE_TIMES; ++i)
    {
        int nWorkers = this_unit.next_worker_id;

        for (int j = 0; j < nWorkers; ++j)
        {
            Worker* pWorker = Worker::get(j);
            ss_dassert(pWorker);

            cs.qtimes[i] += pWorker->statistics().qtimes[i];
            cs.exectimes[i] += pWorker->statistics().exectimes[i];
        }

        cs.qtimes[i] /= (nWorkers != 0 ? nWorkers : 1);
        cs.exectimes[i] /= (nWorkers != 0 ? nWorkers : 1);
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
        resolve_poll_error(fd, errno, EPOLL_CTL_ADD);
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
        resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

Worker* Worker::get(int worker_id)
{
    ss_dassert((worker_id >= 0) && (worker_id < this_unit.next_worker_id));

    return this_unit.ppWorkers[worker_id];
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

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

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

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

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

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

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

size_t Worker::broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.

    size_t n = 0;

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

        if (pWorker->post_message(msg_id, arg1, arg2))
        {
            ++n;
        }
    }

    return n;
}

void Worker::run()
{
    this_thread.current_worker_id = m_id;

    if (pre_run())
    {
        poll_waitevents();

        post_run();
        MXS_INFO("Worker %d has shut down.", m_id);
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
    ss_dassert((this_unit.next_worker_id == 0) || (this_unit.ppWorkers != NULL));

    int nWorkers = this_unit.next_worker_id;
    for (int i = 0; i < nWorkers; ++i)
    {
        Worker* pWorker = this_unit.ppWorkers[i];
        ss_dassert(pWorker);

        pWorker->shutdown();
    }
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

// static
void Worker::resolve_poll_error(int fd, int errornum, int op)
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

        if (nfds == -1 && errno != EINTR)
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

        uint64_t cycle_start = mxs_clock();

        for (int i = 0; i < nfds; i++)
        {
            /** Calculate event queue statistics */
            int64_t started = mxs_clock();
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
            qtime = mxs_clock() - started;

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

        epoll_tick();

        m_state = IDLE;
    } /*< while(1) */

    m_state = STOPPED;
}

namespace
{

int64_t get_current_time_ms()
{
    struct timespec ts;
    ss_debug(int rv =) clock_gettime(CLOCK_MONOTONIC, &ts);
    ss_dassert(rv == 0);

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

}

void Worker::tick()
{
    int64_t now = get_current_time_ms();

    vector<DelayedCall*> repeating_calls;

    auto i = m_sorted_calls.begin();

    // i->first is the time when the first call should be invoked.
    while (!m_sorted_calls.empty() && (i->first <= now))
    {
        DelayedCall* pCall = i->second;

        auto j = m_calls.find(pCall->id());
        ss_dassert(j != m_calls.end());

        m_sorted_calls.erase(i);
        m_calls.erase(j);

        if (pCall->call(Worker::Call::EXECUTE))
        {
            repeating_calls.push_back(pCall);
        }
        else
        {
            delete pCall;
        }

        // NOTE: Must be reassigned, ++i will not work in case a delayed
        // NOTE: call cancels another delayed call.
        i = m_sorted_calls.begin();
    }

    for (auto i = repeating_calls.begin(); i != repeating_calls.end(); ++i)
    {
        DelayedCall* pCall = *i;

        m_sorted_calls.insert(std::make_pair(pCall->at(), pCall));
        m_calls.insert(std::make_pair(pCall->id(), pCall));
    }

    adjust_timer();
}

uint32_t Worker::add_delayed_call(DelayedCall* pCall)
{
    bool adjust = true;

    if (!m_sorted_calls.empty())
    {
        DelayedCall* pFirst = m_sorted_calls.begin()->second;

        if (pCall->at() > pFirst->at())
        {
            // If the added delayed call needs to be called later
            // than the first delayed call, then we do not need to
            // adjust the timer.
            adjust = false;
        }
    }

    // Insert the delayed call into the map ordered by invocation time.
    m_sorted_calls.insert(std::make_pair(pCall->at(), pCall));

    // Insert the delayed call into the map indexed by id.
    ss_dassert(m_calls.find(pCall->id()) == m_calls.end());
    m_calls.insert(std::make_pair(pCall->id(), pCall));

    if (adjust)
    {
        adjust_timer();
    }

    return pCall->id();
}

void Worker::adjust_timer()
{
    if (!m_sorted_calls.empty())
    {
        DelayedCall* pCall = m_sorted_calls.begin()->second;

        uint64_t now = get_current_time_ms();
        int64_t delay = pCall->at() - now;

        if (delay <= 0)
        {
            delay = 1;
        }

        m_pTimer->start(delay);
    }
    else
    {
        m_pTimer->cancel();
    }
}

bool Worker::cancel_delayed_call(uint32_t id)
{
    bool found = false;

    auto i = m_calls.find(id);

    if (i != m_calls.end())
    {
        DelayedCall* pCall = i->second;
        m_calls.erase(i);

        // All delayed calls with exactly the same trigger time.
        // Not particularly likely there will be many of those.
        auto range = m_sorted_calls.equal_range(pCall->at());

        auto k = range.first;
        ss_dassert(k != range.second);

        while (k != range.second)
        {
            if (k->second == pCall)
            {
                m_sorted_calls.erase(k);
                delete pCall;

                k = range.second;

                found = true;
            }
            else
            {
                ++k;
            }
        }

        ss_dassert(found);
    }
    else
    {
        ss_dassert(!true);
        MXS_WARNING("Attempt to remove a delayed call, associated with non-existing id.");
    }

    return found;
}

}


namespace
{

class WorkerInfoTask: public maxscale::WorkerTask
{
public:
    WorkerInfoTask(const char* zHost, uint32_t nThreads)
        : m_zHost(zHost)
    {
        m_data.resize(nThreads);
    }

    void execute(Worker& worker)
    {
        json_t* pStats = json_object();
        const Worker::STATISTICS& s = worker.get_local_statistics();
        json_object_set_new(pStats, "reads", json_integer(s.n_read));
        json_object_set_new(pStats, "writes", json_integer(s.n_write));
        json_object_set_new(pStats, "errors", json_integer(s.n_error));
        json_object_set_new(pStats, "hangups", json_integer(s.n_hup));
        json_object_set_new(pStats, "accepts", json_integer(s.n_accept));
        json_object_set_new(pStats, "blocking_polls", json_integer(s.blockingpolls));
        json_object_set_new(pStats, "event_queue_length", json_integer(s.evq_length));
        json_object_set_new(pStats, "max_event_queue_length", json_integer(s.evq_max));
        json_object_set_new(pStats, "max_exec_time", json_integer(s.maxexectime));
        json_object_set_new(pStats, "max_queue_time", json_integer(s.maxqtime));

        uint32_t nCurrent;
        uint64_t nTotal;
        worker.get_descriptor_counts(&nCurrent, &nTotal);
        json_object_set_new(pStats, "current_descriptors", json_integer(nCurrent));
        json_object_set_new(pStats, "total_descriptors", json_integer(nTotal));

        json_t* load = json_object();
        json_object_set_new(load, "last_second", json_integer(worker.load(Worker::Load::ONE_SECOND)));
        json_object_set_new(load, "last_minute", json_integer(worker.load(Worker::Load::ONE_MINUTE)));
        json_object_set_new(load, "last_hour", json_integer(worker.load(Worker::Load::ONE_HOUR)));
        json_object_set_new(pStats, "load", load);

        json_t* pAttr = json_object();
        json_object_set_new(pAttr, "stats", pStats);

        int idx = worker.get_current_id();
        stringstream ss;
        ss << idx;

        json_t* pJson = json_object();
        json_object_set_new(pJson, CN_ID, json_string(ss.str().c_str()));
        json_object_set_new(pJson, CN_TYPE, json_string(CN_THREADS));
        json_object_set_new(pJson, CN_ATTRIBUTES, pAttr);
        json_object_set_new(pJson, CN_LINKS, mxs_json_self_link(m_zHost, CN_THREADS, ss.str().c_str()));

        ss_dassert((size_t)idx < m_data.size());
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

}

json_t* mxs_worker_to_json(const char* zHost, int id)
{
    Worker* target = Worker::get(id);
    WorkerInfoTask task(zHost, id + 1);
    mxs::Semaphore sem;

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

size_t mxs_worker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return Worker::broadcast_message(msg_id, arg1, arg2);
}

MXS_WORKER* mxs_worker_get(int worker_id)
{
    return Worker::get(worker_id);
}

MXS_WORKER* mxs_worker_get_current()
{
    return Worker::get_current();
}

int mxs_worker_get_current_id()
{
    return Worker::get_current_id();
}

int mxs_worker_id(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->id();
}

bool mxs_worker_should_shutdown(MXS_WORKER* pWorker)
{
    return static_cast<Worker*>(pWorker)->should_shutdown();
}

bool mxs_worker_post_message(MXS_WORKER* pWorker, uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    return static_cast<Worker*>(pWorker)->post_message(msg_id, arg1, arg2);
}
