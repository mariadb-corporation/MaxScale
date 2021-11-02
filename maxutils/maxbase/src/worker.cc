/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/worker.hh>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <maxbase/assert.h>
#include <maxbase/atomic.hh>
#include <maxbase/log.h>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>

#define WORKER_ABSENT_ID -1

using std::function;
using std::vector;
using std::stringstream;
using namespace std::chrono;

namespace
{

using maxbase::Worker;

/**
 * Unit variables.
 */
struct this_unit
{
    bool initialized;   // Whether the initialization has been performed.
} this_unit =
{
    false,      // initialized
};

thread_local struct this_thread
{
    Worker* pCurrent_worker;    // The current worker
} this_thread =
{
    nullptr
};

/**
 * Structure used for sending cross-thread messages.
 */
typedef struct worker_message
{
    uint32_t id;    /*< Message id. */
    intptr_t arg1;  /*< Message specific first argument. */
    intptr_t arg2;  /*< Message specific second argument. */
} WORKER_MESSAGE;
}

namespace maxbase
{

const int WORKER_STATISTICS::MAXNFDS;
const int64_t WORKER_STATISTICS::N_QUEUE_TIMES;

WorkerLoad::WorkerLoad()
    : m_load_1_hour(60)                     // 60 minutes in an hour
    , m_load_1_minute(60, &m_load_1_hour)   // 60 seconds in a minute
    , m_load_1_second(&m_load_1_minute)
{
}

void WorkerLoad::about_to_work(TimePoint now)
{
    auto dur = now - m_start_time;

    m_wait_time += (now - m_wait_start);

    if (dur >= GRANULARITY)
    {
        int load_percentage = 0.5 + 100 * ((dur - m_wait_time).count() / (double)dur.count());

        m_start_time = now;
        m_wait_time = 0s;

        m_load_1_second.add_value(load_percentage);
    }
}

// static
uint64_t WorkerLoad::get_time_ms(mxb::TimePoint tp)
{
    return duration_cast<milliseconds>(tp.time_since_epoch()).count();
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
                        MXB_ALERT("Could not make timer fd non-blocking, system will not work: %s",
                                  mxb_strerror(errno));
                        close(fd);
                        fd = -1;
                        mxb_assert(!true);
                    }
                }
                else
                {
                    MXB_ALERT("Could not get timer fd flags, system will not work: %s",
                              mxb_strerror(errno));
                    close(fd);
                    fd = -1;
                    mxb_assert(!true);
                }
            }
            else
            {
                MXB_ALERT("Could not create timer file descriptor even with no flags, system "
                          "will not work: %s",
                          mxb_strerror(errno));
                mxb_assert(!true);
            }
        }
        else
        {
            MXB_ALERT("Could not create timer file descriptor, system will not work: %s",
                      mxb_strerror(errno));
            mxb_assert(!true);
        }
    }

    return fd;
}
}

WorkerTimer::WorkerTimer(Worker* pWorker)
    : m_fd(create_timerfd())
    , m_pWorker(pWorker)
{
    POLL_DATA::handler = handler;
    POLL_DATA::owner = m_pWorker;

    if (m_fd != -1)
    {
        if (!m_pWorker->add_fd(m_fd, EPOLLIN | EPOLLET, this))
        {
            MXB_ALERT("Could not add timer descriptor to worker, system will not work.");
            ::close(m_fd);
            m_fd = -1;
            mxb_assert(!true);
        }
    }
}

WorkerTimer::~WorkerTimer()
{
    if (m_fd != -1)
    {
        if (!m_pWorker->remove_fd(m_fd))
        {
            MXB_ERROR("Could not remove timer fd from worker.");
        }

        ::close(m_fd);
    }
}

void WorkerTimer::start(int32_t interval)
{
    mxb_assert(interval >= 0);

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
        MXB_ERROR("Could not set timer settings.");
    }
}

void WorkerTimer::cancel()
{
    start(0);
}

uint32_t WorkerTimer::handle(Worker* pWorker, uint32_t events)
{
    mxb_assert(pWorker == m_pWorker);
    mxb_assert(events & EPOLLIN);
    mxb_assert((events & ~EPOLLIN) == 0);

    // Read all events
    uint64_t expirations;
    while (read(m_fd, &expirations, sizeof(expirations)) == 0)
    {
    }

    tick();

    return poll_action::READ;
}

// static
uint32_t WorkerTimer::handler(POLL_DATA* pThis, WORKER* pWorker, uint32_t events)
{
    return static_cast<WorkerTimer*>(pThis)->handle(static_cast<Worker*>(pWorker), events);
}

namespace
{

int create_epoll_instance()
{
    // Since Linux kernel 2.6.8, the size argument is ignored, but must be positive.

    int fd = ::epoll_create(1);

    if (fd == -1)
    {
        MXB_ALERT("Could not create epoll-instance for worker, system will not work: %s",
                  mxb_strerror(errno));
        mxb_assert(!true);
    }

    return fd;
}
}

Worker::Worker(int max_events)
    : m_epoll_fd(create_epoll_instance())
    , m_state(STOPPED)
    , m_max_events(max_events)
    , m_pQueue(NULL)
    , m_started(false)
    , m_should_shutdown(false)
    , m_shutdown_initiated(false)
    , m_nCurrent_descriptors(0)
    , m_nTotal_descriptors(0)
    , m_pTimer(new PrivateTimer(this, this, &Worker::tick))
    , m_next_delayed_call_id{1}
{
    mxb_assert(max_events > 0);

    if (m_epoll_fd != -1)
    {
        m_pQueue = MessageQueue::create(this);

        if (m_pQueue)
        {
            if (!m_pQueue->add_to_worker(this))
            {
                MXB_ALERT("Could not add message queue to worker, system will not work.");
                mxb_assert(!true);
            }
        }
        else
        {
            MXB_ALERT("Could not create message queue for worker, system will not work.");
            mxb_assert(!true);
        }
    }
}

Worker::~Worker()
{
    mxb_assert(!m_started);

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
    mxb_assert(!this_unit.initialized);

    this_unit.initialized = true;

    return this_unit.initialized;
}

void Worker::finish()
{
    mxb_assert(this_unit.initialized);

    this_unit.initialized = false;
}

void Worker::get_descriptor_counts(uint32_t* pnCurrent, uint64_t* pnTotal)
{
    *pnCurrent = atomic_load_uint32(&m_nCurrent_descriptors);
    *pnTotal = atomic_load_uint64(&m_nTotal_descriptors);
}

Worker::RandomEngine& Worker::random_engine()
{
    return m_random_engine;
}

void Worker::gen_random_bytes(uint8_t* pOutput, size_t nBytes)
{
    auto pWorker = mxb::Worker::get_current();      // Must be in a worker thread.
    auto& rand_eng = pWorker->m_random_engine;
    size_t bytes_written = 0;
    while (bytes_written < nBytes)
    {
        auto random_num = rand_eng.rand();
        auto random_num_size = sizeof(random_num);
        auto bytes_left = nBytes - bytes_written;
        auto writable = std::min(bytes_left, random_num_size);
        memcpy(pOutput + bytes_written, &random_num, writable);
        bytes_written += writable;
    }
}

bool Worker::add_fd(int fd, uint32_t events, POLL_DATA* pData)
{
    bool rv = true;

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->owner = this;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0)
    {
        mxb::atomic::add(&m_nCurrent_descriptors, 1, mxb::atomic::RELAXED);
        mxb::atomic::add(&m_nTotal_descriptors, 1, mxb::atomic::RELAXED);
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
        mxb::atomic::add(&m_nCurrent_descriptors, -1, mxb::atomic::RELAXED);
    }
    else
    {
        resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

Worker* Worker::get_current()
{
    return this_thread.pCurrent_worker;
}

bool Worker::execute(Task* pTask, mxb::Semaphore* pSem, enum execute_mode_t mode)
{
    // No logging here, function must be signal safe.
    bool rval = true;

    if ((mode == Worker::EXECUTE_DIRECT)
        || (mode == Worker::EXECUTE_AUTO && Worker::get_current() == this))
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

        rval = post_message(MXB_WORKER_MSG_TASK, arg1, arg2);
    }

    return rval;
}

bool Worker::execute(std::unique_ptr<DisposableTask> sTask, enum execute_mode_t mode)
{
    // No logging here, function must be signal safe.
    return post_disposable(sTask.release(), mode);
}

// private
bool Worker::post_disposable(DisposableTask* pTask, enum execute_mode_t mode)
{
    bool posted = true;

    pTask->inc_ref();

    if ((mode == Worker::EXECUTE_DIRECT)
        || (mode == Worker::EXECUTE_AUTO && Worker::get_current() == this))
    {
        pTask->execute(*this);
        pTask->dec_ref();
    }
    else
    {
        intptr_t arg1 = reinterpret_cast<intptr_t>(pTask);

        posted = post_message(MXB_WORKER_MSG_DISPOSABLE_TASK, arg1, 0);

        if (!posted)
        {
            pTask->dec_ref();
        }
    }

    return posted;
}

bool Worker::execute(const function<void ()>& func, mxb::Semaphore* pSem, execute_mode_t mode)
{

    class CustomTask : public Task
    {
    public:

        CustomTask(const function<void ()>& func)
            : m_func(func)
        {
        }

    private:
        function<void ()> m_func;

        void execute(maxbase::Worker& worker) override final
        {
            m_func();

            // The task needs to delete itself only after the task has been executed
            delete this;
        }
    };

    bool rval = false;
    CustomTask* task = new(std::nothrow) CustomTask(func);

    if (task)
    {
        if (!(rval = execute(task, pSem, mode)))
        {
            // Posting the task failed, it needs to be deleted now
            delete task;
        }
    }

    return rval;
}

bool Worker::call(Task& task, execute_mode_t mode)
{
    mxb::Semaphore sem;
    return execute(&task, &sem, mode) && sem.wait();
}

bool Worker::call(const function<void ()>& func, execute_mode_t mode)
{
    mxb::Semaphore sem;
    return execute(func, &sem, mode) && sem.wait();
}

bool Worker::post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2)
{
    // NOTE: No logging here, this function must be signal safe.
    bool rval = false;

    // TODO: Enable and fix this in develop and/or 2.4: The deletion of rworker_local is done after the
    // workers have stopped and it triggers this assertion.
    // mxb_assert(state() != Worker::FINISHED);

    if (state() != Worker::FINISHED)
    {
        MessageQueue::Message message(msg_id, arg1, arg2);
        rval = m_pQueue->post(message);
    }

    return rval;
}

void Worker::run(mxb::Semaphore* pSem)
{
    mxb_assert(m_state == STOPPED || m_state == FINISHED);
    this_thread.pCurrent_worker = this;

    if (pre_run())
    {
        m_state = PROCESSING;

        if (pSem)
        {
            pSem->post();
        }

        poll_waitevents();

        m_state = FINISHED;

        post_run();
        MXB_INFO("Worker %p has shut down.", this);
    }
    else if (pSem)
    {
        pSem->post();
    }

    this_thread.pCurrent_worker = nullptr;
}

bool Worker::start()
{
    mxb_assert(!m_started);
    mxb_assert(m_thread.get_id() == std::thread::id());
    mxb::Semaphore sem;

    m_started = true;
    m_should_shutdown = false;
    m_shutdown_initiated = false;

    try
    {
        m_thread = std::thread(&Worker::thread_main, this, &sem);
        sem.wait();
    }
    catch (const std::exception& x)
    {
        MXB_ERROR("Could not start worker thread: %s", x.what());
        m_started = false;
    }

    return m_started;
}

void Worker::join()
{
    mxb_assert(m_thread.get_id() != std::thread::id());

    if (m_started)
    {
        MXB_INFO("Waiting for worker %p.", this);
        m_thread.join();
        MXB_INFO("Waited for worker %p.", this);
        m_started = false;
    }
}

void Worker::shutdown()
{
    // NOTE: No logging here, this function must be signal safe.
    // This function could set m_should_shutdown directly, but posting it in a message also ensures
    // the thread wakes up from epoll_wait.
    if (!m_shutdown_initiated)
    {
        auto init_shutdown = [this]() {
            MXB_INFO("Worker %p received shutdown message.", this);
            m_should_shutdown = true;
        };
        execute(init_shutdown, EXECUTE_QUEUED);
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
    switch ((int)msg.id())
    {
    case MXB_WORKER_MSG_TASK:
        {
            Task* pTask = reinterpret_cast<Task*>(msg.arg1());
            mxb::Semaphore* pSem = reinterpret_cast<mxb::Semaphore*>(msg.arg2());

            pTask->execute(*this);

            if (pSem)
            {
                pSem->post();
            }
        }
        break;

    case MXB_WORKER_MSG_DISPOSABLE_TASK:
        {
            DisposableTask* pTask = reinterpret_cast<DisposableTask*>(msg.arg1());
            pTask->execute(*this);
            pTask->dec_ref();
        }
        break;

    default:
        MXB_ERROR("Worker received unknown message %d.", msg.id());
    }
}

/**
 * The entry point of each worker thread.
 *
 * @param arg A worker.
 */
// static
void Worker::thread_main(Worker* pThis, mxb::Semaphore* pSem)
{
    pThis->run(pSem);
}

bool Worker::pre_run()
{
    return true;
}

void Worker::post_run()
{
}

void Worker::call_epoll_tick()
{
    epoll_tick();
}

void Worker::epoll_tick()
{
}

// static
void Worker::resolve_poll_error(int fd, int errornum, int op)
{
    if (op == EPOLL_CTL_ADD)
    {
        if (EEXIST == errornum)
        {
            MXB_ERROR("File descriptor %d already present in an epoll instance.", fd);
            return;
        }

        if (ENOSPC == errornum)
        {
            MXB_ERROR("The limit imposed by /proc/sys/fs/epoll/max_user_watches was "
                      "reached when trying to add file descriptor %d to an epoll instance.",
                      fd);
            return;
        }
    }
    else
    {
        mxb_assert(op == EPOLL_CTL_DEL);

        /* Must be removing */
        if (ENOENT == errornum)
        {
            MXB_ERROR("File descriptor %d was not found in epoll instance.", fd);
            return;
        }
    }

    /* Common checks for add or remove - crash system */
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

namespace
{

long time_in_100ms_ticks(maxbase::TimePoint tp)
{
    using TenthSecondDuration = duration<long, std::ratio<1, 10>>;

    auto dur = tp.time_since_epoch();
    auto tenth = duration_cast<TenthSecondDuration>(dur);

    return tenth.count();
}
}

/**
 * The main polling loop
 */
void Worker::poll_waitevents()
{
    struct epoll_event events[m_max_events];

    m_load.reset(mxb::Clock::now());

    int64_t nFds_total = 0;
    int64_t nPolls_effective = 0;

    while (!m_should_shutdown)
    {
        m_state = POLLING;

        ++m_statistics.n_polls;

        auto now = mxb::Clock::now();

        int timeout = duration_cast<milliseconds>(m_load.about_to_wait(now)).count();
        // Don't allow a 0 timeout as that would cause fast looping for 1ms
        timeout = std::max(timeout, 1);
        int nfds = epoll_wait(m_epoll_fd, events, m_max_events, timeout);

        m_epoll_tick_now = mxb::Clock::now();

        m_load.about_to_work(m_epoll_tick_now);
        uint64_t cycle_start = time_in_100ms_ticks(m_epoll_tick_now);

        if (nfds == -1 && errno != EINTR)
        {
            int eno = errno;
            errno = 0;
            MXB_ERROR("%lu [poll_waitevents] epoll_wait returned "
                      "%d, errno %d",
                      pthread_self(),
                      nfds,
                      eno);
        }

        // Set some stats (time taken is negligible).
        if (nfds > 0)
        {
            nPolls_effective += 1;
            nFds_total += nfds;

            if (nFds_total <= 0)
            {
                // Wrapped, so we reset the situation.
                nFds_total = nfds;
                nPolls_effective = 1;
            }

            m_statistics.evq_avg = nFds_total / nPolls_effective;

            m_statistics.evq_max = std::max(m_statistics.evq_max, int64_t(nfds));   // evq_max could be int

            ++m_statistics.n_pollev;

            m_state = PROCESSING;

            ++m_statistics.n_fds[std::min(nfds - 1, STATISTICS::MAXNFDS - 1)];
        }

        // Set loop_now before the loop, and inside the loop
        // just before looping back to the top.
        auto loop_now = m_epoll_tick_now;

        for (int i = 0; i < nfds; i++)
        {
            /** Calculate event queue statistics */
            int64_t started = time_in_100ms_ticks(loop_now);
            int64_t qtime = started - cycle_start;

            ++m_statistics.qtimes[std::min(qtime, STATISTICS::N_QUEUE_TIMES)];
            m_statistics.maxqtime = std::max(m_statistics.maxqtime, qtime);

            POLL_DATA* data = (POLL_DATA*)events[i].data.ptr;
            uint32_t actions = data->handler(data, this, events[i].events);

            m_statistics.n_accept += bool(actions & poll_action::ACCEPT);
            m_statistics.n_read += bool(actions & poll_action::READ);
            m_statistics.n_write += bool(actions & poll_action::WRITE);
            m_statistics.n_hup += bool(actions & poll_action::HUP);
            m_statistics.n_error += bool(actions & poll_action::ERROR);

            /** Calculate event execution statistics */
            loop_now = maxbase::Clock::now();
            qtime = time_in_100ms_ticks(loop_now) - started;

            ++m_statistics.exectimes[std::min(qtime, STATISTICS::N_QUEUE_TIMES)];
            m_statistics.maxexectime = std::max(m_statistics.maxexectime, qtime);
        }

        call_epoll_tick();
    }   /*< while(1) */
}

void Worker::tick()
{
    int64_t now = WorkerLoad::get_time_ms(mxb::Clock::now());

    vector<DelayedCall*> repeating_calls;

    auto i = m_sorted_calls.begin();

    // i->first is the time when the first call should be invoked.
    while (!m_sorted_calls.empty() && (i->first <= now))
    {
        DelayedCall* pCall = i->second;

        auto j = m_calls.find(pCall->id());
        mxb_assert(j != m_calls.end());

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
    mxb_assert(Worker::get_current() == this);
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
    mxb_assert(m_calls.find(pCall->id()) == m_calls.end());
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

        uint64_t now = WorkerLoad::get_time_ms(mxb::Clock::now());
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
    mxb_assert(Worker::get_current() == this || m_state == FINISHED);
    bool found = false;

    auto i = m_calls.find(id);

    if (i != m_calls.end())
    {
        DelayedCall* pCall = i->second;
        m_calls.erase(i);

        // All delayed calls with exactly the same trigger time.
        // Not particularly likely there will be many of those.
        auto range = m_sorted_calls.equal_range(pCall->at());

        mxb_assert(range.first != range.second);

        for (auto k = range.first; k != range.second; ++k)
        {
            if (k->second == pCall)
            {
                m_sorted_calls.erase(k);

                pCall->call(Worker::Call::CANCEL);
                delete pCall;

                found = true;
                break;
            }
        }

        mxb_assert(found);
    }
    else
    {
        mxb_assert_message(!true,
                           "Attempt to remove delayed call using non-existent id %u. "
                           "Calling hktask_remove() from the task function? Simply "
                           "return false instead.", id);
        MXB_WARNING("Attempt to remove a delayed call, associated with non-existing id.");
    }

    return found;
}
}
