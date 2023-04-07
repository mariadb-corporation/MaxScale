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

#include <maxbase/assert.hh>
#include <maxbase/atomic.hh>
#include <maxbase/log.hh>
#include <maxbase/string.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/threadpool.hh>

#define WORKER_ABSENT_ID -1

using std::function;
using std::vector;
using std::string;
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
    bool initialized;    // Whether the initialization has been performed.
    int  next_worker_id; // Next worker id

} this_unit =
{
    false, // initialized
    1,     // next_worker_id
};

int32_t next_worker_id()
{
    return mxb::atomic::add(&this_unit.next_worker_id, 1, mxb::atomic::RELAXED);
}

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

string epoll_events_to_string(EPOLL_EVENTS events)
{

#define CHECK_EPOLL_EVENT(EVENT)\
        if (events & EVENT)\
        {\
           rv.push_back(#EVENT);\
        }

    vector<string> rv;

#if defined(EPOLLIN)
    CHECK_EPOLL_EVENT(EPOLLIN);
#endif

#if defined(EPOLLPRI)
    CHECK_EPOLL_EVENT(EPOLLPRI);
#endif

#if defined(EPOLLOUT)
    CHECK_EPOLL_EVENT(EPOLLOUT);
#endif

#if defined(EPOLLRDNORM)
    CHECK_EPOLL_EVENT(EPOLLRDNORM);
#endif

#if defined(EPOLLRDBAND)
    CHECK_EPOLL_EVENT(EPOLLRDBAND);
#endif

#if defined(EPOLLWRNORM)
    CHECK_EPOLL_EVENT(EPOLLWRNORM);
#endif

#if defined(EPOLLWRBAND)
    CHECK_EPOLL_EVENT(EPOLLWRBAND);
#endif

#if defined(EPOLLMSG)
    CHECK_EPOLL_EVENT(EPOLLMSG);
#endif

#if defined(EPOLLERR)
    CHECK_EPOLL_EVENT(EPOLLERR);
#endif

#if defined(EPOLLHUP)
    CHECK_EPOLL_EVENT(EPOLLHUP);
#endif

#if defined(EPOLLRDHUP)
    CHECK_EPOLL_EVENT(EPOLLRDHUP);
#endif

#if defined(EPOLLEXCLUSIVE)
    CHECK_EPOLL_EVENT(EPOLLEXCLUSIVE);
#endif

#if defined(EPOLLWAKEUP)
    CHECK_EPOLL_EVENT(EPOLLWAKEUP);
#endif

#if defined(EPOLLONESHOT)
    CHECK_EPOLL_EVENT(EPOLLONESHOT);
#endif

#if defined(EPOLLET)
    CHECK_EPOLL_EVENT(EPOLLET);
#endif

#undef CHECK_EPOLL_EVENT

    return mxb::join(rv, "|");
}


const int WorkerStatistics::MAXNFDS;
const int64_t WorkerStatistics::N_QUEUE_TIMES;

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
    if (m_fd != -1)
    {
        if (!m_pWorker->add_pollable(EPOLLIN | EPOLLET, this))
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
        if (!m_pWorker->remove_pollable(this))
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

uint32_t WorkerTimer::handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context)
{
    mxb_assert(pWorker == m_pWorker);
    mxb_assert(((events & EPOLLIN) != 0) && ((events & ~EPOLLIN) == 0));

    // Read all events
    uint64_t expirations;
    while (read(m_fd, &expirations, sizeof(expirations)) == 0)
    {
    }

    tick();

    return poll_action::READ;
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

Worker::Callable::~Callable()
{
    if (!m_dcalls.empty())
    {
        // Before MaxScale 22.8, if the target of a delayed call was deleted before the
        // delayed call was due, it would have resulted in a random crash, so it should
        // be a very rare occurrence. If we ever see this, then we know that some
        // unexplained crash may have been caused by this.
        MXB_ERROR("Recipient of delayed call was deleted before delayed call was due.");
    }

    // False, because the dcalls can't be called with Worker::Callable::CANCEL as most
    // parts of the object to be called already has been destructed at this point.
    cancel_dcalls(false);
}

bool Worker::Callable::cancel_dcall(Worker::DCId id, bool call)
{
    bool rv = false;

    auto it = m_dcalls.find(id);

    if (it != m_dcalls.end())
    {
        auto* pCall = it->second;

        if (m_dcalls_suspended)
        {
            // If the dcalls have been suspended, then we have to delete the call here, as
            // the worker has no knowledge about it.

            if (call)
            {
                pCall->call(Callable::CANCEL);
            }
            m_dcalls.erase(it);
            delete pCall;
        }
        else
        {
            mxb_assert(m_pWorker);
            m_pWorker->cancel_dcall(pCall, call);
        }

        rv = true;
    }

    return rv;
}

void Worker::Callable::cancel_dcalls(bool call)
{
    if (m_dcalls_suspended)
    {
        // If the dcalls have been suspended, then we have to delete them here, as
        // the worker has no knowledge about them.

        for (auto kv : m_dcalls)
        {
            auto* pCall = kv.second;

            if (call)
            {
                pCall->call(Callable::CANCEL);
            }
            delete pCall;
        }

        m_dcalls.clear();
    }
    else
    {
        // Can't iterate; the cancel_delayed_call() will cause unregister_dcall() to be called.
        while (!m_dcalls.empty())
        {
            auto* pCall = m_dcalls.begin()->second;
            m_pWorker->cancel_dcall(pCall, call);
        }
    }
}

void Worker::Callable::suspend_dcalls()
{
    mxb_assert(!m_dcalls_suspended);

    for (auto kv : m_dcalls)
    {
        auto* pCall = kv.second;

        m_pWorker->remove_dcall(pCall);
    }

    m_dcalls_suspended = true;
}

void Worker::Callable::resume_dcalls()
{
    mxb_assert(m_dcalls_suspended);

    for (auto kv : m_dcalls)
    {
        auto pCall = kv.second;
        m_pWorker->restore_dcall(pCall);
    }

    m_dcalls_suspended = false;
}

void Worker::Callable::register_dcall(Worker::DCall* pCall)
{
    mxb_assert(m_dcalls.find(pCall->id()) == m_dcalls.end());
    m_dcalls.emplace(pCall->id(), pCall);
}

void Worker::Callable::unregister_dcall(Worker::DCall* pCall)
{
    // When this unregister_dcall() is called, the dcall need not be present as the
    // unregistration is done in a context where the dcall may have been cancelled
    // already.
    auto it = m_dcalls.find(pCall->id());

    if (it != m_dcalls.end())
    {
        m_dcalls.erase(it);
    }
}

void Worker::Callable::unregister_dcall(DCId id)
{
    // When this unregister_dcall() is called, the dcall will be present unless
    // there is a bug in the implementation.
    auto it = m_dcalls.find(id);
    mxb_assert(it != m_dcalls.end());

    m_dcalls.erase(it);
}

Worker::Worker(int max_events)
    : m_epoll_fd(create_epoll_instance())
    , m_id(next_worker_id())
    , m_max_events(max_events)
    , m_pTimer(new PrivateTimer(this, this, &Worker::tick))
    , m_prev_dcid(m_id)
{
    // The 16 most significant bits of the 64 bit delayed call id, are the 16
    // least significant bits of the worker id.
    m_prev_dcid <<= 48;

    mxb_assert(max_events > 0);

    if (m_epoll_fd != -1)
    {
        m_pQueue = MessageQueue::create(MessageQueue::EVENT, this);

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
        DCall* pCall = i->second;

        pCall->call(Callable::CANCEL);
        pCall->owner().unregister_dcall(pCall->id());
        delete pCall;
    }
}

std::string Worker::thread_name() const
{
    string s;

    if (get_current() == this)
    {
        s = mxb::get_thread_name();
    }
    else
    {
        s = mxb::get_thread_name(m_thread);
    }

    return s;
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

int64_t Worker::current_fd_count() const
{
    return m_nCurrent_descriptors;
}

int64_t Worker::total_fd_count() const
{
    return m_nTotal_descriptors;
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

bool Worker::add_pollable(uint32_t events, Pollable* pPollable)
{
    mxb_assert(!m_started || is_current());
    mxb_assert(pPollable->is_shared() || pPollable->polling_worker() == nullptr);

    bool rv = true;

    int fd = pPollable->poll_fd();

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pPollable;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0)
    {
        m_nCurrent_descriptors++;
        m_nTotal_descriptors++;

        if (pPollable->is_unique())
        {
            pPollable->set_polling_worker(this);
        }
    }
    else
    {
        resolve_poll_error(fd, errno, EPOLL_CTL_ADD);
        rv = false;
    }

    return rv;
}

bool Worker::modify_pollable(uint32_t events, Pollable* pPollable)
{
    bool rv = true;

    int fd = pPollable->poll_fd();

    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pPollable;

    mxb_assert(pPollable->is_shared() || pPollable->polling_worker() == this);

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0)
    {
        resolve_poll_error(fd, errno, EPOLL_CTL_MOD);
        rv = false;
    }

    return rv;
}

bool Worker::remove_pollable(Pollable* pPollable)
{
    mxb_assert(!m_started || is_current());
    bool rv = true;

    int fd = pPollable->poll_fd();

    struct epoll_event ev = {};

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, &ev) == 0)
    {
        m_nCurrent_descriptors--;

        if (!m_scheduled_polls.empty())
        {
            auto it = m_scheduled_polls.find(fd);
            if (it != m_scheduled_polls.end())
            {
                m_scheduled_polls.erase(it);
            }
        }

        if (!m_incomplete_polls.empty())
        {
            auto it = m_incomplete_polls.find(fd);
            if (it != m_incomplete_polls.end())
            {
                m_incomplete_polls.erase(it);
            }
        }

        if (pPollable->is_unique())
        {
            pPollable->set_polling_worker(nullptr);
        }
    }
    else
    {
        resolve_poll_error(fd, errno, EPOLL_CTL_DEL);
        rv = false;
    }

    return rv;
}

//static
Worker* Worker::get_current()
{
    return this_thread.pCurrent_worker;
}

bool Worker::is_current() const
{
    return this_thread.pCurrent_worker == this;
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

    if (messages_enabled())
    {
        // TODO: Enable and fix this in develop and/or 2.4: The deletion of rworker_local is done after the
        // workers have stopped and it triggers this assertion.
        // mxb_assert(event_loop_state() != EventLoop::FINISHED);

        if (event_loop_state() != EventLoop::FINISHED)
        {
            MessageQueue::Message message(msg_id, arg1, arg2);
            rval = m_pQueue->post(message);
        }
    }

    return rval;
}

void Worker::run(mxb::Semaphore* pSem)
{
    mxb_assert(m_event_loop_state == EventLoop::NOT_STARTED || m_event_loop_state == EventLoop::FINISHED);
    this_thread.pCurrent_worker = this;

    if (pre_run())
    {
        if (pSem)
        {
            pSem->post();
        }

        m_event_loop_state = EventLoop::RUNNING;

        poll_waitevents();

        m_event_loop_state = EventLoop::FINISHED;

        post_run();
        MXB_INFO("Worker (%s, %p) has shut down.", thread_name().c_str(), this);
    }
    else if (pSem)
    {
        pSem->post();
    }

    this_thread.pCurrent_worker = nullptr;
}

bool Worker::start(const std::string& name)
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
        set_thread_name(m_thread, name);
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
        string s = thread_name();
        MXB_INFO("Waiting for worker (%s, %p).", s.c_str(), this);
        m_thread.join();
        MXB_INFO("Waited for worker (%s, %p).", s.c_str(), this);
        m_started = false;
    }
}

void Worker::shutdown()
{
    // NOTE: No logging here, this function must be signal safe.
    if (!m_shutdown_initiated)
    {
        m_shutdown_initiated = true; // A potential race here, but it does not matter.

        // If called from the thread itself, we just turn on the flag because in that case
        // we must be running in an event-handler and will shortly return to the loop where
        // the flag is checked. Otherwise we post the change, so that the thread returns from
        // epoll_wait().

        auto init_shutdown = [this]() {
            MXB_INFO("Worker (%s, %p) received shutdown message.", thread_name().c_str(), this);
            m_should_shutdown = true;
        };

        if (get_current() == this)
        {
            init_shutdown();
        }
        else
        {
            execute(init_shutdown, EXECUTE_QUEUED);
        }
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

TimePoint Worker::deliver_events(uint64_t cycle_start,
                                 TimePoint loop_now,
                                 Pollable* pPollable,
                                 uint32_t events,
                                 Pollable::Context context)
{
    // The polling worker can be nullptr, if epoll_wait() returned more events than
    // one and an event other than the last one caused the high watermark to be hit,
    // which will cause events for the DCB to be disabled => polling worker set to nullptr.
    mxb_assert(pPollable->is_shared()
               || pPollable->polling_worker() == this
               || pPollable->polling_worker() == nullptr);

    /** Calculate event queue statistics */
    int64_t started = time_in_100ms_ticks(loop_now);
    int64_t qtime = started - cycle_start;

    ++m_statistics.qtimes[std::min(qtime, Statistics::N_QUEUE_TIMES)];
    m_statistics.maxqtime = std::max(m_statistics.maxqtime, qtime);

    int fd = pPollable->poll_fd();
    uint32_t actions = pPollable->handle_poll_events(this, events, context);

    m_statistics.n_accept += bool(actions & poll_action::ACCEPT);
    m_statistics.n_read += bool(actions & poll_action::READ);
    m_statistics.n_write += bool(actions & poll_action::WRITE);
    m_statistics.n_hup += bool(actions & poll_action::HUP);
    m_statistics.n_error += bool(actions & poll_action::ERROR);

    if (actions & poll_action::INCOMPLETE_READ)
    {
        m_statistics.n_incomplete_read += 1;

        PendingPoll pending_poll = { EPOLLIN, pPollable };
        m_incomplete_polls.emplace(fd, pending_poll);
    }

    /** Calculate event execution statistics */
    loop_now = maxbase::Clock::now();
    qtime = time_in_100ms_ticks(loop_now) - started;

    ++m_statistics.exectimes[std::min(qtime, Statistics::N_QUEUE_TIMES)];
    m_statistics.maxexectime = std::max(m_statistics.maxexectime, qtime);

    return loop_now;
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
        ++m_statistics.n_polls;

        auto now = mxb::Clock::now();

        int timeout = duration_cast<milliseconds>(m_load.about_to_wait(now)).count();
        // Don't allow a 0 timeout as that would cause fast looping for 1ms
        timeout = std::max(timeout, m_min_timeout);

        if (!m_incomplete_polls.empty())
        {
            // But we want it to return immediately if we know there are pending
            // polls to handle.
            timeout = 0;
        }
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

            ++m_statistics.n_fds[std::min(nfds - 1, Statistics::MAXNFDS - 1)];
        }

        mxb_assert(m_scheduled_polls.empty());

        m_scheduled_polls.swap(m_incomplete_polls);

        // Set loop_now before the loop, and inside the loop
        // just before looping back to the top.
        auto loop_now = m_epoll_tick_now;

        for (int i = 0; i < nfds; i++)
        {
            uint32_t pollable_events = events[i].events;
            Pollable* pPollable = static_cast<Pollable*>(events[i].data.ptr);
            int fd = pPollable->poll_fd();

            // Lookup from empty unordered map is an order of magnitude more expensive
            // than checking whether it is empty.
            if (!m_scheduled_polls.empty())
            {
                auto it = m_scheduled_polls.find(fd);

                if (it != m_scheduled_polls.end())
                {
                    // Ok, so there were events for this Pollable already. We'll merge
                    // them and remove it from the scheduled calls.
                    pollable_events |= it->second.events;
                    m_scheduled_polls.erase(it);
                }
            }

            loop_now = deliver_events(cycle_start, loop_now, pPollable, pollable_events, Pollable::NEW_CALL);
        }

        // Can't just iterate over it, in case the callback removes
        // pollables from the worker.
        while (!m_scheduled_polls.empty())
        {
            auto it = m_scheduled_polls.begin();
            PendingPoll pending_poll = it->second;
            m_scheduled_polls.erase(it);

            auto* pPollable = pending_poll.pPollable;
            auto events = pending_poll.events;

            loop_now = deliver_events(cycle_start, loop_now, pPollable, events, Pollable::REPEATED_CALL);
        }

        if (!m_lcalls.empty())
        {
            // We can't just iterate, because a loop-call may add another loop-call,
            // which may cause the vector to be reallocated.
            int i = 0;
            do
            {
                std::function<void ()>& f = m_lcalls[i++];
                f();
            }
            while (m_lcalls.begin() + i != m_lcalls.end());

            m_lcalls.clear();
        }

        call_epoll_tick();
    }   /*< while(1) */
}

void Worker::tick()
{
    int64_t now = WorkerLoad::get_time_ms(mxb::Clock::now());

    vector<DCall*> repeating_calls;

    auto i = m_sorted_calls.begin();

    // i->first is the time when the first call should be invoked.
    while (!m_sorted_calls.empty() && (i->first <= now))
    {
        DCall* pCall = i->second;
        mxb_assert(pCall->owner().worker() == this);

        auto j = m_calls.find(pCall->id());
        mxb_assert(j != m_calls.end());

        m_sorted_calls.erase(i);
        m_calls.erase(j);

        m_pCurrent_call = pCall;
        bool repeat = pCall->call(Callable::EXECUTE);
        m_pCurrent_call = nullptr;

        if (repeat)
        {
            if (!pCall->owner().dcalls_suspended())
            {
                repeating_calls.push_back(pCall);
            }
        }
        else
        {
            pCall->owner().unregister_dcall(pCall);
            delete pCall;
        }

        // NOTE: Must be reassigned, ++i will not work in case a delayed
        // NOTE: call cancels another delayed call.
        i = m_sorted_calls.begin();
    }

    for (auto i = repeating_calls.begin(); i != repeating_calls.end(); ++i)
    {
        DCall* pCall = *i;

        m_sorted_calls.insert(std::make_pair(pCall->at(), pCall));
        m_calls.insert(std::make_pair(pCall->id(), pCall));
    }

    adjust_timer();
}

Worker::DCId Worker::add_dcall(DCall* pCall)
{
    mxb_assert(Worker::get_current() == this);

    if (!pCall->owner().dcalls_suspended())
    {
        // Only if the dcalls are currently not suspended, we actually add it.
        // Otherwise it will be added when the dcalls of the object eventually
        // are resumed.
        restore_dcall(pCall);
    }

    pCall->owner().register_dcall(pCall);

    return pCall->id();
}

void Worker::adjust_timer()
{
    if (!m_sorted_calls.empty())
    {
        DCall* pCall = m_sorted_calls.begin()->second;

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

Worker::DCall* Worker::remove_dcall(DCId id)
{
    DCall* pCall = nullptr;

    auto i = m_calls.find(id);

    if (i != m_calls.end())
    {
        pCall = i->second;
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
                break;
            }
        }

        mxb_assert(pCall);
    }
    else
    {
        mxb_assert_message(!true,
                           "Attempt to remove delayed call using nonexistent id %ld. "
                           "Calling hktask_remove() from the task function? Simply "
                           "return false instead.", id);
        MXB_WARNING("Attempt to remove a delayed call, associated with non-existing id.");
    }

    return pCall;
}

void Worker::lcall(std::function<void ()>&& f)
{
    m_lcalls.emplace_back(std::move(f));
}

void Worker::remove_dcall(DCall* pCall)
{
    // Prevent re-entrancy problems if delayed calls are suspended from
    // a delayed call.
    if (pCall != m_pCurrent_call)
    {
        MXB_AT_DEBUG(auto* p = )remove_dcall(pCall->id());
        mxb_assert(p == pCall);
    }
}

void Worker::restore_dcall(DCall* pCall)
{
    bool adjust = true;

    if (!m_sorted_calls.empty())
    {
        DCall* pFirst = m_sorted_calls.begin()->second;

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
    auto id = pCall->id();
    mxb_assert(m_calls.find(id) == m_calls.end());
    m_calls.insert(std::make_pair(id, pCall));

    if (adjust)
    {
        adjust_timer();
    }
}

void Worker::cancel_dcall(DCall* pCall, bool call)
{
    mxb_assert(Worker::get_current() == this || m_event_loop_state == EventLoop::FINISHED);

    remove_dcall(pCall);

    if (pCall != m_pCurrent_call)
    {
        if (call)
        {
            pCall->call(Callable::CANCEL);
        }
    }

    pCall->owner().unregister_dcall(pCall->id());
    delete pCall;
}
}
