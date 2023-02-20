/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>

#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include <maxbase/assert.hh>
#include <maxbase/average.hh>
#include <maxbase/messagequeue.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/workertask.hh>
#include <maxbase/random.hh>
#include <maxbase/stopwatch.hh>

using namespace std::chrono_literals;

enum mxb_worker_msg_id_t
{
    MXB_WORKER_MSG_TASK,
    MXB_WORKER_MSG_DISPOSABLE_TASK
};

namespace maxbase
{

class WorkerStatistics final
{
public:
    static const int     MAXNFDS = 10;
    static const int64_t N_QUEUE_TIMES = 30;

    void reset()
    {
        WorkerStatistics empty;
        *this = empty;
    }

    int64_t n_read {0};            /*< Number of read events   */
    int64_t n_write {0};           /*< Number of write events  */
    int64_t n_error {0};           /*< Number of error events  */
    int64_t n_hup {0};             /*< Number of hangup events */
    int64_t n_accept {0};          /*< Number of accept events */
    int64_t n_polls {0};           /*< Number of poll cycles   */
    int64_t n_pollev {0};          /*< Number of polls returning events */
    int64_t n_incomplete_read {0}; /*< Number of times reading was not completed in one callback */
    int64_t evq_avg {0};           /*< Average event queue length */
    int64_t evq_max {0};           /*< Maximum event queue length */
    int64_t maxqtime {0};          /*< Maximum duration from epoll_wait() -> handling. */
    int64_t maxexectime {0};       /*< Maximum duration of event handling (callback). */

    std::array<int64_t, MAXNFDS>            n_fds {};   /*< Number of wakeups with particular n_fds value */
    std::array<uint32_t, N_QUEUE_TIMES + 1> qtimes {};
    std::array<uint32_t, N_QUEUE_TIMES + 1> exectimes {};
};

/**
 * WorkerLoad is a class that calculates the load percentage of a worker
 * thread, based upon the relative amount of time the worker spends in
 * epoll_wait().
 *
 * If during a time period of length T milliseconds, the worker thread
 * spends t milliseconds in epoll_wait(), then the load of the worker is
 * calculated as 100 * ((T - t) / T). That is, if the worker spends all
 * the time in epoll_wait(), then the load is 0 and if the worker spends
 * no time waiting in epoll_wait(), then the load is 100.
 */
class WorkerLoad
{
    WorkerLoad(const WorkerLoad&) = delete;
    WorkerLoad& operator=(const WorkerLoad&) = delete;

public:
    enum counter_t
    {
        ONE_SECOND = 1000,
        ONE_MINUTE = 60 * ONE_SECOND,
        ONE_HOUR   = 60 * ONE_MINUTE,
    };

    const mxb::Duration GRANULARITY = 1s;

    /**
     * Constructor
     */
    WorkerLoad();

    /**
     * Reset the load calculation. Should be called immediately before the
     * worker enters its eternal epoll_wait()-loop.
     */
    void reset(mxb::TimePoint now)
    {
        m_start_time = now;
        m_wait_start = now;
        m_wait_time = 0s;
    }

    /**
     * To be used for signaling that the worker is about to call epoll_wait().
     *
     * @param now  The current time.
     *
     * @return The timeout the client should pass to epoll_wait().
     */
    mxb::Duration about_to_wait(mxb::TimePoint now)
    {
        m_wait_start = now;

        auto duration = now - m_start_time;

        if (duration >= GRANULARITY)
        {
            about_to_work(now);
            duration = GRANULARITY;
        }
        else
        {
            duration = GRANULARITY - duration;
        }

        return duration;
    }

    /**
     * To be used for signaling that the worker has returned from epoll_wait().
     *
     * @param now  The current time.
     */
    void about_to_work(TimePoint now);

    /**
     * Returns the last calculated load,
     *
     * @return A value between 0 and 100.
     */
    uint8_t percentage(counter_t counter) const
    {
        switch (counter)
        {
        case ONE_SECOND:
            return m_load_1_second.value();

        case ONE_MINUTE:
            return m_load_1_minute.value();

        case ONE_HOUR:
            return m_load_1_hour.value();

        default:
            mxb_assert(!true);
            return 0;
        }
    }

    /**
     * When was the last 1 second period started.
     *
     * @return The start time.
     */
    mxb::TimePoint start_time() const
    {
        return m_start_time;
    }

    /**
     * @return Convert a timepoint to milliseconds (for C-style interfaces).
     */
    static uint64_t get_time_ms(TimePoint tp);

private:

    mxb::TimePoint m_start_time {}; /*< When was the current 1-second period started. */
    mxb::TimePoint m_wait_start {}; /*< The time when the worker entered epoll_wait(). */
    mxb::Duration  m_wait_time {};  /*< How much time the worker has spent in epoll_wait(). */
    AverageN       m_load_1_hour;   /*< The average load during the last hour. */
    AverageN       m_load_1_minute; /*< The average load during the last minute. */
    Average1       m_load_1_second; /*< The load during the last 1-second period. */
};

/**
 * WorkerTimer is a timer class built on top of timerfd_create(2),
 * which means that each WorkerTimer instance will consume one file
 * descriptor. The implication of that is that there should not be
 * too many WorkerTimer instances. In order to be used, a WorkerTimer
 * needs a Worker instance in whose context the timer is triggered.
 */
class WorkerTimer : private Pollable
{
    WorkerTimer(const WorkerTimer&) = delete;
    WorkerTimer& operator=(const WorkerTimer&) = delete;

public:
    virtual ~WorkerTimer();

    /**
     * @brief Start the timer.
     *
     * @param interval The initial delay in milliseconds before the
     *                 timer is triggered, and the subsequent interval
     *                 between triggers.
     *
     * @attention A value of 0 means that the timer is cancelled.
     */
    void start(int32_t interval);

    /**
     * @brief Cancel the timer.
     */
    void cancel();

protected:
    /**
     * @brief Constructor
     *
     * @param pWorker  The worker in whose context the timer is to run.
     */
    WorkerTimer(Worker* pWorker);

    /**
     * @brief Called when the timer is triggered.
     */
    virtual void tick() = 0;

private:
    int poll_fd() const override
    {
        return m_fd;
    }
    uint32_t handle_poll_events(Worker* pWorker, uint32_t events, Pollable::Context context) override;

private:
    int     m_fd;       /**< The timerfd descriptor. */
    Worker* m_pWorker;  /**< The worker in whose context the timer runs. */
};

/**
 * @class Worker
 *
 * A Worker is a class capable of asynchronously processing events
 * associated with file descriptors. Internally Worker has a thread
 * and an epoll-instance of its own.
 */
class Worker : private MessageQueue::Handler
{
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

public:
    using Statistics = WorkerStatistics;
    using Task = WorkerTask;
    using DisposableTask = WorkerDisposableTask;
    using Load = WorkerLoad;
    using Timer = WorkerTimer;
    using RandomEngine = XorShiftRandom;
    using DCId = int64_t;

    // Must be zero since the id is used in if-statements. Existing code assumes that a normal id is never 0.
    static constexpr DCId NO_CALL = 0;

private:
    class DCall;

public:
    /**
     * Delayed calls, i.e. calls that will be called after some delay, can be made on
     * any object whose class is derived from Worker::Callable. The delayed call will be
     * made by the worker that was provided to the constructor of Callable. However, the
     * worker can be changed, but when the change is made, the delayed calls must be
     * suspended. Note that Callable is *not* thread-safe; it must only be manipulated
     * from its worker.
     */
    class Callable
    {
    public:
        enum Action
        {
            EXECUTE,    /**< Execute the call */
            CANCEL      /**< Cancel the call */
        };

        Callable(Worker* pWorker)
            : m_pWorker(pWorker)
        {
        }

        Callable(const Callable&) = delete;
        Callable& operator = (const Callable&) = delete;

        virtual ~Callable();

        /**
         * @return The worker this object is associated with.
         */
        Worker* worker() const
        {
            return m_pWorker;
        }

        /**
         * Delayed call
         *
         * @param delay      The delay in milliseconds.
         * @param pFunction  A static/free function.
         * @param data       Data to provide to @c pFunction.
         *
         * @return The dcall id.
         */
        template<class D>
        DCId dcall(const std::chrono::milliseconds& delay,
                   bool (* pFunction)(Action action, D data),
                   D data)
        {
            mxb_assert(m_pWorker);
            return m_pWorker->dcall(this, delay, pFunction, data);
        }

        /**
         * Delayed call
         *
         * @param delay    The delay in milliseconds.
         * @param pMethod  A member function.
         * @param pT       The object on which to call @c pMethod.
         *
         * @return The dcall id.
         */
        template<class T>
        DCId dcall(const std::chrono::milliseconds& delay,
                   bool (T::* pMethod)(Action action),
                   T* pT)
        {
            mxb_assert(m_pWorker);
            return m_pWorker->dcall(this, delay, pMethod, pT);
        }

        template<class T>
        DCId dcall(const std::chrono::milliseconds& delay,
                   bool (T::* pMethod)(),
                   T* pT)
        {
            mxb_assert(m_pWorker);
            return m_pWorker->dcall(this, delay, pMethod, pT);
        }

        /**
         * Delayed call
         *
         * @param delay  The delay in milliseconds.
         * @param f      A functor.
         *
         * @return The dcall id.
         */
        DCId dcall(const std::chrono::milliseconds& delay,
                   std::function<bool(Action action)>&& f)
        {
            mxb_assert(m_pWorker);
            return m_pWorker->dcall(this, delay, std::move(f));
        }

        DCId dcall(const std::chrono::milliseconds& delay,
                   std::function<bool()>&& f)
        {
            mxb_assert(m_pWorker);
            return m_pWorker->dcall(this, delay, std::move(f));
        }

        /**
         * Cancel a dcall
         *
         * @param id    The id of the dcall to cancel.
         * @param call  If true, the delayed function will be called with Call::CANCEL.
         *              Otherwise, the function will not be called at all.
         *
         * @return True, if the id referred to a delayed call, false otherwise.
         */
        bool cancel_dcall(DCId id, bool call = true);

        /**
         * Cancel all dcalls.
         *
         * @param call  If true, then the delayed function will be called with Call::CANCEL.
         *              Otherwise, the function will not be called at all.
         */
        void cancel_dcalls(bool call = true);

        /**
         * Suspend all dcalls of this object. Will cause them to be removed from the worker.
         */
        void suspend_dcalls();

        /**
         * Resume all previously suspended dcalls of this object. Will cause them to be added
         * back to the worker.
         */
        void resume_dcalls();

        /**
         * @return True, if the dcalls currently are suspended.
         */
        bool dcalls_suspended() const
        {
            return m_dcalls_suspended;
        }

    protected:
        void set_worker(Worker* pWorker)
        {
            mxb_assert(m_dcalls.empty() || m_dcalls_suspended);
            m_pWorker = pWorker;
        }

    private:
        friend class Worker;
        void register_dcall(DCall* pCall);
        void unregister_dcall(DCall* pCall);
        void unregister_dcall(DCId id);

    private:
        Worker*                m_pWorker;
        std::map<DCId, DCall*> m_dcalls;
        bool                   m_dcalls_suspended { false };
    };

    /**
     * A delegating timer that delegates the timer tick handling
     * to another object.
     */
    template<class T>
    class DelegatingTimer : public Timer
    {
        DelegatingTimer(const DelegatingTimer&) = delete;
        DelegatingTimer& operator=(const DelegatingTimer&) = delete;

    public:
        typedef void (T::* PMethod)();

        /**
         * @brief Constructor
         *
         * @param pWorker     The worker in whose context the timer runs.
         * @param pDelegatee  The object to whom the timer tick is delivered.
         * @param pMethod     The method to call on @c pDelegatee when the
         *                    timer is triggered.
         */
        DelegatingTimer(Worker* pWorker, T* pDelegatee, PMethod pMethod)
            : Timer(pWorker)
            , m_pDelegatee(pDelegatee)
            , m_pMethod(pMethod)
        {
        }

    private:
        void tick() override final
        {
            (m_pDelegatee->*m_pMethod)();
        }

    private:
        T*      m_pDelegatee;
        PMethod m_pMethod;
    };

    enum class EventLoop
    {
        NOT_STARTED,
        RUNNING,
        FINISHED
    };

    enum execute_mode_t
    {
        EXECUTE_DIRECT, /**< Always execute directly using the calling thread/worker. */
        EXECUTE_QUEUED, /**< Always execute via the event loop using this thread/worker. */
        EXECUTE_AUTO,   /**< If calling thread/worker is this worker, call directly otherwise queued. */
    };

    enum
    {
        MAX_EVENTS = 1000
    };

    /**
     * Constructs a worker.
     *
     * @param max_events  The maximum number of events that can be returned by
     *                    one call to epoll_wait.
     */
    Worker(int max_events = MAX_EVENTS);

    virtual ~Worker();

    /**
     * Returns the id of the worker
     *
     * @return The id of the worker.
     */
    int32_t id() const
    {
        return m_id;
    }

    /**
     * Returns the thread name of the worker. Note that if the worker has not
     * been started or already has finished, then "unknown" will be returned.
     *
     * @return The name of the worker.
     */
    std::string thread_name() const;

    int load(Load::counter_t counter) const
    {
        return m_load.percentage(counter);
    }

    /**
     * Returns the state of the worker.
     *
     * @return The current state.
     *
     * @attentions The state might have changed the moment after the function returns.
     */
    EventLoop event_loop_state() const
    {
        return m_event_loop_state;
    }

    /**
     * Returns statistics for this worker.
     *
     * @return The worker specific statistics.
     *
     * @attentions The statistics may change at any time.
     */
    const Statistics& statistics() const
    {
        return m_statistics;
    }

    int64_t current_fd_count() const;
    int64_t total_fd_count() const;

    /**
     * Return the random engine of this worker.
     */
    RandomEngine& random_engine();

    /**
     * Write random bytes to a buffer using the random generator of this worker. Should be only used
     * from within a worker thread.
     *
     * @param pOutput Output buffer
     * @param nBytes Bytes to write
     */
    static void gen_random_bytes(uint8_t* pOutput, size_t nBytes);

    /**
     * Returns the TimePoint when epoll_tick() was called. Use this in worker threads
     * instead of maxbase::Clock::now() for timeouts, time tracking etc. where absolute
     * precision is not needed (i.e. almost always).
     */
    TimePoint epoll_tick_now()
    {
        return m_epoll_tick_now;
    }

    /**
     * Add a Pollable to the epoll instance of the worker.
     *
     * @param events     Mask of epoll event types.
     * @param pPollable  The pollable instance. *Must* remain valid
     *                   until the pollable is removed.
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    bool add_pollable(uint32_t events, Pollable* pPollable);

    /**
     * Remove a Pollable from the worker's epoll instance.
     *
     * @param pPollable  The pollable instance. *Must* remain valid
     *
     * @return True on success, false on failure.
     */
    bool remove_pollable(Pollable* pPollable);

    /**
     * Modifies the events the pollable waits for
     *
     * @param events    Mask of epoll event types
     * @param pPollable The pollable instance
     *
     * @return True if event modification was successful
     */
    bool modify_pollable(uint32_t events, Pollable* pPollable);

    /**
     * Main function of worker.
     *
     * The worker will run the poll loop, until it is told to shut down.
     *
     * @attention  This function will run in the calling thread.
     */
    void run()
    {
        run(nullptr);
    }

    /**
     * Run worker in separate thread.
     *
     * This function will start a new thread, in which the `run`
     * function will be executed.
     *
     * @param name  The name of the worker.
     * @return True if the thread could be started, false otherwise.
     */
    virtual bool start(const std::string& name);

    /**
     * Waits for the worker to finish.
     */
    void join();

    /**
     * Initiate shutdown of worker.
     *
     * @attention A call to this function will only initiate the shutdown,
     *            the worker will not have shut down when the function returns.
     *
     * @attention This function is signal safe.
     */
    void shutdown();

    /**
     * Executes a task on the worker thread.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once the task's `execute` return.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted to the worker (i.e. not executed yet),
     *          false otherwise.
     *
     * @attention  The instance must remain valid for as long as it takes for the
     *             task to be transferred to the worker and its `execute` function
     *             to be called.
     *
     * The semaphore can be used for waiting for the task to be finished.
     *
     * @code
     *     mxb::Semaphore sem;
     *     MyTask task;
     *
     *     pWorker->execute(&task, &sem);
     *     sem.wait();
     *
     *     MyResult& result = task.result();
     * @endcode
     */
    bool execute(Task* pTask, mxb::Semaphore* pSem, enum execute_mode_t mode);

    bool execute(Task* pTask, enum execute_mode_t mode)
    {
        return execute(pTask, NULL, mode);
    }

    /**
     * Executes a task on the worker thread.
     *
     * @param pTask  The task to be executed.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted (i.e. not executed yet), false otherwise.
     *
     * @attention  Once the task has been executed, it will be deleted.
     */
    bool execute(std::unique_ptr<DisposableTask> sTask, enum execute_mode_t mode);

    /**
     * Execute a function on the worker thread.
     *
     * @param func The function to call
     * @param pSem If non-NULL, will be posted once the task's `execute` return.
     * @param mode Execution mode
     *
     * @return True, if task was posted to the worker
     */
    bool execute(const std::function<void ()>& func, mxb::Semaphore* pSem, enum execute_mode_t mode);

    bool execute(const std::function<void ()>& func, enum execute_mode_t mode)
    {
        return execute(func, NULL, mode);
    }

    /**
     * Executes a task on the worker thread and returns only when the task
     * has finished.
     *
     * @param task   The task to be executed.
     * @param mode   Execution mode
     *
     * @return True if the task was executed on the worker.
     */
    bool call(Task& task, enum execute_mode_t mode);

    /**
     * Executes function on worker thread and returns only when the function
     * has finished.
     *
     * @param func Function to execute
     * @param mode Execution mode
     *
     * @return True if function was executed on the worker.
     */
    bool call(const std::function<void ()>& func, enum execute_mode_t mode);

    /**
     * Post a message to a worker.
     *
     * @param msg_id  The message id.
     * @param arg1    Message specific first argument.
     * @param arg2    Message specific second argument.
     *
     * @return True if the message could be sent, false otherwise. If the message
     *         posting fails, errno is set appropriately.
     *
     * @attention The return value tells *only* whether the message could be sent,
     *            *not* that it has reached the worker.
     *
     * @attention This function is signal safe.
     */
    bool post_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

    /**
     * Return the worker associated with the current thread.
     *
     * @return The worker instance, or NULL if the current thread does not have a worker.
     */
    static Worker* get_current();

    /**
     * @return True, if this worker is the current one.
     */
    bool is_current() const;

    /**
     * Loop call; the provided function will be called right before the
     * control returns back to epoll_wait(). Note that it is far more efficient
     * to call lcall() than dcall() with a 0 delay.
     *
     * @param f  A function.
     *
     * @note All loop-calls are processed before the control returns back to
     *       epoll_wait(). That is, if a loop call adds another loop call, which
     *       adds a loop call, ad infinitum, the system will hang.
     *       Safe usage is e.g. to add a loop call to clientReply() from a filter
     *       that short-circuits the routeQuery() handling.
     */
    void lcall(std::function<void ()>&& f);

protected:
    const int m_epoll_fd;                                  /*< The epoll file descriptor. */
    EventLoop m_event_loop_state {EventLoop::NOT_STARTED}; /*< The event loop state of the worker. */

    /**
     * Enable/disable message sending to this worker.
     *
     * @bool enabled  Whether to enable or disable.
     */
    void set_messages_enabled(bool enabled)
    {
        m_messages_enabled.store(enabled, std::memory_order_relaxed);
    }

    /**
     * @return Whether messages to this worker are enabled or not.
     */
    bool messages_enabled() const
    {
        return m_messages_enabled.load(std::memory_order_relaxed);
    }

    static void inc_ref(WorkerDisposableTask* pTask)
    {
        pTask->inc_ref();
    }

    static void dec_ref(WorkerDisposableTask* pTask)
    {
        pTask->dec_ref();
    }

    bool post_disposable(DisposableTask* pTask, enum execute_mode_t mode);

    /**
     * Called by Worker::run() before starting the epoll loop.
     *
     * Default implementation returns True.
     *
     * @return True, if the epoll loop should be started, false otherwise.
     */
    virtual bool pre_run();

    /**
     * Called by Worker::run() after the epoll loop has finished.
     *
     * Default implementation does nothing.
     */
    virtual void post_run();

    /**
     * Called by Worker::run() once per epoll loop.
     *
     * Default implementation calls @c epoll_tick().
     */
    virtual void call_epoll_tick();

    /**
     * Called by Worker::run() once per epoll loop.
     *
     * Default implementation does nothing.
     */
    virtual void epoll_tick();

    /**
     * Reset statistics as if worker would just have been started.
     */
    void reset_statistics()
    {
        mxb_assert(get_current() == this);
        m_nTotal_descriptors = m_nCurrent_descriptors;
        m_statistics.reset();
    }

    /**
     * Set minimum timeout when calling epoll_wait() in the Worker
     * event-loop.
     *
     * @note The granularity of the worker load calculation is
     *       1000ms. Unless the minimum timeout is significantly
     *       less than that, the load calculation will not work
     *       properly.
     *
     * @param timeout  The minimum timeout in milliseconds. If less
     *                 than 1, will silently be set to 1.
     */
    void set_min_timeout(int timeout)
    {
        mxb_assert(get_current() == this);
        mxb_assert(timeout >= 1);

        if (timeout < 1)
        {
            timeout = 1;
        }

        m_min_timeout = timeout;
    }

    /**
     * Helper for resolving epoll-errors. In case of fatal ones, SIGABRT
     * will be raised.
     *
     * @param fd      The epoll file descriptor.
     * @param errnum  The errno of the operation.
     * @param op      Either EPOLL_CTL_ADD or EPOLL_CTL_DEL.
     */
    static void resolve_poll_error(int fd, int err, int op);

private:
    friend class Initer;
    static bool init();
    static void finish();

private:
    friend class Callable;

    template<class D>
    DCId dcall(Callable* pOwner,
               const std::chrono::milliseconds& delay,
               bool (* pFunction)(Callable::Action action, D data),
               D data)
    {
        auto id = next_dcall_id();
        return add_dcall(new DCallFunction<D>(pOwner, delay, id, pFunction, data));
    }

    template<class T>
    DCId dcall(Callable* pOwner,
               const std::chrono::milliseconds& delay,
               bool (T::* pMethod)(Callable::Action action),
               T* pT)
    {
        return add_dcall(new DCallMethodWithCancel<T>(pOwner, delay, next_dcall_id(), pMethod, pT));
    }

    template<class T>
    DCId dcall(Callable* pOwner,
               const std::chrono::milliseconds& delay,
               bool (T::* pMethod)(void),
               T* pT)
    {
        return add_dcall(new DCallMethodWithoutCancel<T>(pOwner, delay, next_dcall_id(), pMethod, pT));
    }

    template<class T>
    DCId dcall(const std::chrono::milliseconds& delay,
               bool (T::* pMethod)(Callable::Action action),
               T* pT)
    {
        static_assert(std::is_base_of_v<Callable, T>,
                      "If a specific owner is not provided as first argument, the call object "
                      "must be derived from Worker::Callable.");
        return dcall(pT, delay, pMethod, pT);
    }

    template<class T>
    DCId dcall(const std::chrono::milliseconds& delay,
               bool (T::* pMethod)(),
               T* pT)
    {
        static_assert(std::is_base_of_v<Callable, T>,
                      "If a specific owner is not provided as first argument, the call object "
                      "must be derived from Worker::Callable.");
        return dcall(pT, delay, pMethod, pT);
    }

    DCId dcall(Callable* pOwner,
               const std::chrono::milliseconds& delay,
               std::function<bool(Callable::Action action)>&& f)
    {
        return add_dcall(new DCallFunctorWithCancel(pOwner, delay, next_dcall_id(), std::move(f)));
    }

    DCId dcall(Callable* pOwner,
               const std::chrono::milliseconds& delay,
               std::function<bool(void)>&& f)
    {
        return add_dcall(new DCallFunctorWithoutCancel(pOwner, delay, next_dcall_id(), std::move(f)));
    }

    void cancel_dcall(DCall* pCall, bool call = true);

private:
    friend class DCall;

    DCId next_dcall_id()
    {
        // Called in single-thread context. Wrapping does not matter
        // as it is unlikely there would be 4 billion pending delayed
        // calls.
        return ++m_prev_dcid;
    }

    class DCall
    {
        DCall(const DCall&) = delete;
        DCall& operator=(const DCall&) = delete;

    public:
        virtual ~DCall()
        {
        }

        Callable& owner() const
        {
            mxb_assert(m_pOwner);
            return *m_pOwner;
        }

        int32_t delay() const
        {
            return m_delay;
        }

        DCId id() const
        {
            return m_id;
        }

        int64_t at() const
        {
            return m_at;
        }

        bool call(Callable::Action action)
        {
            bool rv = do_call(action);
            // We try to invoke the function as often as it was specified. If the
            // delay is very short and the execution time for the function very long,
            // then we will not succeed with that and the function will simply be
            // invoked as frequently as possible.
            int64_t now = WorkerLoad::get_time_ms(mxb::Clock::now());
            int64_t then = m_at + m_delay;

            if (now > then)
            {
                m_at = now;
            }
            else
            {
                m_at = then;
            }
            return rv;
        }

    protected:
        DCall(Callable* pOwner, const std::chrono::milliseconds& delay, DCId id)
            : m_pOwner(pOwner)
            , m_id(id)
            , m_delay(delay >= std::chrono::milliseconds(0) ? delay.count() : 0)
            , m_at(get_at(m_delay, mxb::Clock::now()))
        {
            mxb_assert(delay.count() >= 0);
        }

        virtual bool do_call(Callable::Action action) = 0;

    private:
        static int64_t get_at(int32_t delay, mxb::TimePoint tp)
        {
            mxb_assert(delay >= 0);

            int64_t now = WorkerLoad::get_time_ms(tp);

            return now + delay;
        }

    private:
        Callable* m_pOwner; // The target object.
        DCId      m_id;      // The id of the delayed call.
        int32_t   m_delay;   // The delay in milliseconds.
        int64_t   m_at;      // The next time the function should be invoked.
    };

    template<class D>
    class DCallFunction : public DCall
    {
        DCallFunction(const DCallFunction&) = delete;
        DCallFunction& operator=(const DCallFunction&) = delete;

    public:
        DCallFunction(Callable* pOwner,
                      const std::chrono::milliseconds& delay,
                      DCId id,
                      bool (*pFunction)(Callable::Action action, D data),
                      D data)
            : DCall(pOwner, delay, id)
            , m_pFunction(pFunction)
            , m_data(data)
        {
        }

    private:
        bool do_call(Callable::Action action) override final
        {
            return m_pFunction(action, m_data);
        }

    private:
        bool (* m_pFunction)(Callable::Action, D);
        D m_data;
    };

    template<class T>
    class DCallMethodWithCancel : public DCall
    {
        DCallMethodWithCancel(const DCallMethodWithCancel&) = delete;
        DCallMethodWithCancel& operator=(const DCallMethodWithCancel&) = delete;

    public:
        DCallMethodWithCancel(Callable* pOwner,
                              const std::chrono::milliseconds& delay,
                              DCId id,
                              bool (T::* pMethod)(Callable::Action),
                              T* pT)
            : DCall(pOwner, delay, id)
            , m_pMethod(pMethod)
            , m_pT(pT)
        {
        }

    private:
        bool do_call(Callable::Action action) override final
        {
            return (m_pT->*m_pMethod)(action);
        }

    private:
        bool (T::* m_pMethod)(Callable::Action);
        T* m_pT;
    };

    template<class T>
    class DCallMethodWithoutCancel : public DCall
    {
        DCallMethodWithoutCancel(const DCallMethodWithoutCancel&) = delete;
        DCallMethodWithoutCancel& operator=(const DCallMethodWithoutCancel&) = delete;

    public:
        DCallMethodWithoutCancel(Callable* pOwner,
                                 const std::chrono::milliseconds& delay,
                                 DCId id,
                                 bool (T::* pMethod)(void),
                                 T* pT)
            : DCall(pOwner, delay, id)
            , m_pMethod(pMethod)
            , m_pT(pT)
        {
        }

    private:
        bool do_call(Callable::Action action) override final
        {
            bool rv = false;

            if (action == Callable::EXECUTE)
            {
                rv = (m_pT->*m_pMethod)();
            }

            return rv;
        }

    private:
        bool (T::* m_pMethod)(void);
        T* m_pT;
    };

    class DCallFunctorWithCancel : public DCall
    {
        DCallFunctorWithCancel(const DCallFunctorWithCancel&) = delete;
        DCallFunctorWithCancel& operator=(const DCallFunctorWithCancel&) = delete;

    public:
        DCallFunctorWithCancel(Callable* pOwner,
                               const std::chrono::milliseconds& delay,
                               DCId id,
                               std::function<bool (Callable::Action)> f)
            : DCall(pOwner, delay, id)
            , m_f(std::move(f))
        {
        }

    private:
        bool do_call(Callable::Action action) override
        {
            return m_f(action);
        }

    private:
        std::function<bool(Callable::Action)> m_f;
    };

    class DCallFunctorWithoutCancel : public DCall
    {
        DCallFunctorWithoutCancel(const DCallFunctorWithoutCancel&) = delete;
        DCallFunctorWithoutCancel& operator=(const DCallFunctorWithoutCancel&) = delete;

    public:
        DCallFunctorWithoutCancel(Callable* pOwner,
                                  const std::chrono::milliseconds& delay,
                                  DCId id,
                                  std::function<bool (void)> f)
            : DCall(pOwner, delay, id)
            , m_f(f)
        {
        }

    private:
        bool do_call(Callable::Action action) override
        {
            bool rv = false;

            if (action == Callable::EXECUTE)
            {
                rv = m_f();
            }

            return rv;
        }

    private:
        std::function<bool(void)> m_f;
    };

    DCId add_dcall(DCall* pdcall);
    void adjust_timer();

    DCall* remove_dcall(DCId id);
    void remove_dcall(DCall* pCall);
    void restore_dcall(DCall* pCall);

    void handle_message(MessageQueue& queue, const MessageQueue::Message& msg) override;

    static void thread_main(Worker* pThis, mxb::Semaphore* pSem);

    TimePoint deliver_events(uint64_t cycle_start,
                             TimePoint loop_now,
                             Pollable* pPollable,
                             uint32_t events,
                             Pollable::Context context);

    void poll_waitevents();

    void tick();

private:
    class LaterAt : public std::binary_function<const DCall*, const DCall*, bool>
    {
    public:
        bool operator()(const DCall* pLhs, const DCall* pRhs)
        {
            return pLhs->at() > pRhs->at();
        }
    };

    void run(mxb::Semaphore* pSem);

    struct PendingPoll
    {
        uint32_t  events;
        Pollable* pPollable;
    };

    using PrivateTimer = DelegatingTimer<Worker>;
    using DCallsByTime = std::multimap<int64_t, DCall*>;
    using DCallsById   = std::unordered_map<DCId, DCall*>;
    using PendingPolls = std::unordered_map<int, PendingPoll>;
    using LCalls       = std::vector<std::function<void ()>>;

    const int32_t    m_id;               /*< The id of the worker. */
    MessageQueue*    m_pQueue {nullptr}; /*< The message queue of the worker. */
    std::thread      m_thread;           /*< The thread object of the worker. */
    std::atomic_bool m_started {false};  /*< Whether the thread has been started or not. */

    uint32_t         m_max_events;                /*< Maximum numer of events in each epoll_wait call. */
    Statistics       m_statistics;                /*< Worker statistics. */
    bool             m_should_shutdown {false};   /*< Whether shutdown should be performed. */
    bool             m_shutdown_initiated {false};/*< Whether shutdown has been initiated. */
    int64_t          m_nCurrent_descriptors {0};  /*< Current number of descriptors. */
    int64_t          m_nTotal_descriptors {0};    /*< Total number of descriptors. */
    Load             m_load;                      /*< The worker load. */
    PrivateTimer*    m_pTimer;                    /*< The worker's own timer. */
    DCallsByTime     m_sorted_calls;              /*< Current delayed calls sorted by time. */
    DCallsById       m_calls;                     /*< Current delayed calls indexed by id. */
    DCall*           m_pCurrent_call { nullptr }; /*< The current call, or nullptr if there is not one. */
    RandomEngine     m_random_engine;             /*< Random engine for this worker (this thread). */
    TimePoint        m_epoll_tick_now;            /*< TimePoint when epoll_tick() was called */
    DCId             m_prev_dcid {NO_CALL};       /*< Previous delayed call id. */
    LCalls           m_lcalls;                    /*< Calls to be made before return to epoll_wait(). */
    PendingPolls     m_scheduled_polls;           /*< Calls to be made during current epoll_wait(). */
    PendingPolls     m_incomplete_polls;          /*< Calls to be made at next epoll_wait(). */
    int              m_min_timeout {1};           /*< Minimum timeout when calling epoll_wait(). */
    std::atomic_bool m_messages_enabled {true};   /*< Are messages to this worker enabled. */
};
}
