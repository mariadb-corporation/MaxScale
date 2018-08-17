#pragma once
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

#include <maxscale/ccdefs.hh>

#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <unordered_set>

#include <maxbase/semaphore.hh>
#include <maxscale/platform.h>
#include <maxscale/semaphore.hh>
#include <maxscale/session.h>
#include <maxscale/utils.hh>
#include <maxscale/worker.h>
#include <maxscale/workertask.hh>

#include "messagequeue.hh"

namespace maxscale
{

struct WORKER_STATISTICS
{
    WORKER_STATISTICS()
    {
        memset(this, 0, sizeof(WORKER_STATISTICS));
    }

    enum
    {
        MAXNFDS = 10,
        N_QUEUE_TIMES = 30
    };

    int64_t  n_read;                      /*< Number of read events   */
    int64_t  n_write;                     /*< Number of write events  */
    int64_t  n_error;                     /*< Number of error events  */
    int64_t  n_hup;                       /*< Number of hangup events */
    int64_t  n_accept;                    /*< Number of accept events */
    int64_t  n_polls;                     /*< Number of poll cycles   */
    int64_t  n_pollev;                    /*< Number of polls returning events */
    int64_t  n_nbpollev;                  /*< Number of polls returning events */
    int64_t  n_fds[MAXNFDS];              /*< Number of wakeups with particular n_fds value */
    int64_t  evq_avg;                     /*< Average event queue length */
    int64_t  evq_max;                     /*< Maximum event queue length */
    int64_t  blockingpolls;               /*< Number of epoll_waits with a timeout specified */
    uint32_t qtimes[N_QUEUE_TIMES + 1];
    uint32_t exectimes[N_QUEUE_TIMES + 1];
    int64_t  maxqtime;
    int64_t  maxexectime;
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
    WorkerLoad& operator = (const WorkerLoad&) = delete;

public:
    enum counter_t
    {
        ONE_SECOND = 1000,
        ONE_MINUTE = 60 * ONE_SECOND,
        ONE_HOUR   = 60 * ONE_MINUTE,
    };

    enum
    {
        GRANULARITY = ONE_SECOND
    };

    /**
     * Constructor
     */
    WorkerLoad();

    /**
     * Reset the load calculation. Should be called immediately before the
     * worker enters its eternal epoll_wait()-loop.
     */
    void reset()
    {
        uint64_t now = get_time();

        m_start_time = now;
        m_wait_start = 0;
        m_wait_time = 0;
    }

    /**
     * To be used for signaling that the worker is about to call epoll_wait().
     *
     * @param now  The current time.
     */
    void about_to_wait(uint64_t now)
    {
        m_wait_start = now;
    }

    void about_to_wait()
    {
        about_to_wait(get_time());
    }

    /**
     * To be used for signaling that the worker has returned from epoll_wait().
     *
     * @param now  The current time.
     */
    void about_to_work(uint64_t now);

    void about_to_work()
    {
        about_to_work(get_time());
    }

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
            ss_dassert(!true);
            return 0;
        };
    }

    /**
     * When was the last 1 second period started.
     *
     * @return The start time.
     */
    uint64_t start_time() const
    {
        return m_start_time;
    }

    /**
     * Returns the current time using CLOCK_MONOTONIC.
     *
     * @return Current time in milliseconds.
     */
    static uint64_t get_time();

private:
    /**
     * Average is a base class for classes intended to be used for calculating
     * averages. An Average may have a dependant Average whose value depends
     * upon the value of the first. At certain moments, an Average may trigger
     * its dependant Average to update itself.
     */
    class Average
    {
        Average(const Average&) = delete;
        Average& operator = (const Average&) = delete;

    public:
        /**
         * Constructor
         *
         * @param pDependant An optional dependant average.
         */
        Average(Average* pDependant = NULL)
            : m_pDependant(pDependant)
            , m_value(0)
        {}

        virtual ~Average();

        /**
         * Add a value to the Average. The exact meaning depends upon the
         * concrete Average class.
         *
         * If the addition of the value in some sense represents a full cycle
         * in the average calculation, then the instance will call add_value()
         * on its dependant, otherwise it will call update_value(). In both cases
         * with its own value as argument.
         *
         * @param value  The value to be added.
         *
         * @return True if the addition of the value caused a full cycle
         *         in the average calculation, false otherwise.
         */
        virtual bool add_value(uint8_t value) = 0;

        /**
         * Update the value of the Average. The exact meaning depends upon the
         * concrete Average class. Will also call update_value() of its dependant
         * with its own value as argument.
         *
         * @param value  The value to be updated.
         */
        virtual void update_value(uint8_t value) = 0;

        /**
         * Return the average value.
         *
         * @return The value represented by the Average.
         */
        uint8_t value() const
        {
            return atomic_load_uint32(&m_value);
        }

    protected:
        Average* m_pDependant; /*< The optional dependant Average. */
        uint32_t m_value;      /*< The current average value. */

    protected:
        void set_value(uint32_t value)
        {
            atomic_store_uint32(&m_value, value);
        }
    };

    /**
     * An Average consisting of a single value.
     */
    class Average1 : public Average
    {
    public:
        Average1(Average* pDependant = NULL)
            : Average(pDependant)
        {
        }

        bool add_value(uint8_t value)
        {
            set_value(value);

            // Every addition of a value represents a full cycle.
            if (m_pDependant)
            {
                m_pDependant->add_value(value);
            }

            return true;
        }

        void update_value(uint8_t value)
        {
            set_value(value);

            if (m_pDependant)
            {
                m_pDependant->update_value(value);
            }
        }
    };

    /**
     * An Average calculated from N values.
     */
    template<size_t N>
    class AverageN : public Average
    {
    public:
        AverageN(Average* pDependant = NULL)
            : Average(pDependant)
            , m_end(m_begin + N)
            , m_i(m_begin)
            , m_sum(0)
            , m_nValues(0)
        {
        }

        bool add_value(uint8_t value)
        {
            if (m_nValues == N)
            {
                // If as many values that fit has been added, then remove the
                // least recent value from the sum.
                m_sum -= *m_i;
            }
            else
            {
                // Otherwise make a note that a new value is added.
                ++m_nValues;
            }

            *m_i = value;
            m_sum += *m_i; // Update the sum of all values.

            m_i = next(m_i);

            uint32_t average = m_sum / m_nValues;

            set_value(average);

            if (m_pDependant)
            {
                if (m_i == m_begin)
                {
                    // If we have looped around we have performed a full cycle and will
                    // add a new value to the dependant average.
                    m_pDependant->add_value(average);
                }
                else
                {
                    // Otherwise we just update the most recent value.
                    m_pDependant->update_value(average);
                }
            }

            return m_i == m_begin;
        }

        void update_value(uint8_t value)
        {
            if (m_nValues == 0)
            {
                // If no values have been added yet, there's nothing to update but we
                // need to add the value.
                add_value(value);
            }
            else
            {
                // Otherwise we update the most recent value.
                uint8_t* p = prev(m_i);

                m_sum -= *p;
                *p = value;
                m_sum += *p;

                uint32_t average = m_sum / m_nValues;

                set_value(average);

                if (m_pDependant)
                {
                    m_pDependant->update_value(average);
                }
            }
        }

    private:
        uint8_t* prev(uint8_t* p)
        {
            ss_dassert(p >= m_begin);
            ss_dassert(p < m_end);

            if (p > m_begin)
            {
                --p;
            }
            else
            {
                ss_dassert(p == m_begin);
                p = m_end - 1;
            }

            ss_dassert(p >= m_begin);
            ss_dassert(p < m_end);

            return p;
        }

        uint8_t* next(uint8_t* p)
        {
            ss_dassert(p >= m_begin);
            ss_dassert(p < m_end);

            ++p;

            if (p == m_end)
            {
                p = m_begin;
            }

            ss_dassert(p >= m_begin);
            ss_dassert(p < m_end);

            return p;
        }

    private:
        uint8_t    m_begin[N];  /*< Buffer containing values from which the average is calculated. */
        uint8_t*   m_end;       /*< Points to one past the end of the buffer. */
        uint8_t*   m_i;         /*< Current position in the buffer. */
        uint32_t   m_sum;       /*< Sum of all values in the buffer. */
        uint32_t   m_nValues;   /*< How many values the buffer contains. */
    };

    uint64_t      m_start_time;     /*< When was the current 1-second period started. */
    uint64_t      m_wait_start;     /*< The time when the worker entered epoll_wait(). */
    uint64_t      m_wait_time;      /*< How much time the worker has spent in epoll_wait(). */
    AverageN<60>  m_load_1_hour;    /*< The average load during the last hour. */
    AverageN<60>  m_load_1_minute;  /*< The average load during the last minute. */
    Average1      m_load_1_second;  /*< The load during the last 1-second period. */
};

/**
 * WorkerTimer is a timer class built on top of timerfd_create(2),
 * which means that each WorkerTimer instance will consume one file
 * descriptor. The implication of that is that there should not be
 * too many WorkerTimer instances. In order to be used, a WorkerTimer
 * needs a Worker instance in whose context the timer is triggered.
 */
class WorkerTimer : private MXS_POLL_DATA
{
    WorkerTimer(const WorkerTimer&) = delete;
    WorkerTimer& operator = (const WorkerTimer&) = delete;

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
    uint32_t handle(Worker* pWorker, uint32_t events);

    static uint32_t handler(MXS_POLL_DATA* pThis, void* pWorker, uint32_t events);

private:
    int     m_fd;      /**< The timerfd descriptor. */
    Worker* m_pWorker; /**< The worker in whose context the timer runs. */
};

/**
 * A Worker is a class capable of asynchronously processing events
 * associated with file descriptors. Internally Worker has a thread
 * and an epoll-instance of its own.
 */
class Worker : public MXS_WORKER
             , private MessageQueue::Handler
{
    Worker(const Worker&) = delete;
    Worker& operator = (const Worker&) = delete;

public:
    typedef WORKER_STATISTICS      STATISTICS;
    typedef WorkerTask             Task;
    typedef WorkerDisposableTask   DisposableTask;
    typedef WorkerLoad             Load;
    typedef WorkerTimer            Timer;

    /**
     * A delegating timer that delegates the timer tick handling
     * to another object.
     */
    template<class T>
    class DelegatingTimer : public Timer
    {
        DelegatingTimer(const DelegatingTimer&) = delete;
        DelegatingTimer& operator = (const DelegatingTimer&) = delete;

    public:
        typedef void (T::*PMethod)();

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
        void tick() /* final */
        {
            (m_pDelegatee->*m_pMethod)();
        }

    private:
        T*      m_pDelegatee;
        PMethod m_pMethod;
    };

    enum state_t
    {
        STOPPED,
        IDLE,
        POLLING,
        PROCESSING,
        ZPROCESSING
    };

    enum execute_mode_t
    {
        EXECUTE_AUTO,  /**< Execute tasks immediately */
        EXECUTE_QUEUED /**< Only queue tasks for execution */
    };

    struct Call
    {
        enum action_t
        {
            EXECUTE, /**< Execute the call */
            CANCEL   /**< Cancel the call */
        };
    };

    /**
     * Initialize the worker mechanism.
     *
     * To be called once at process startup.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    static bool init();

    /**
     * Finalize the worker mechanism.
     *
     * To be called once at process shutdown.
     */
    static void finish();

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

    int load(Load::counter_t counter)
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
    state_t state() const
    {
        return m_state;
    }

    /**
     * Returns statistics for this worker.
     *
     * @return The worker specific statistics.
     *
     * @attentions The statistics may change at any time.
     */
    const STATISTICS& statistics() const
    {
        return m_statistics;
    }

    /**
     * Return this worker's statistics.
     *
     * @return Local statistics for this worker.
     */
    const STATISTICS& get_local_statistics() const
    {
        return m_statistics;
    }

    /**
     * Return the count of descriptors.
     *
     * @param pnCurrent  On output the current number of descriptors.
     * @param pnTotal    On output the total number of descriptors.
     */
    void get_descriptor_counts(uint32_t* pnCurrent, uint64_t* pnTotal);

    /**
     * Add a file descriptor to the epoll instance of the worker.
     *
     * @param fd      The file descriptor to be added.
     * @param events  Mask of epoll event types.
     * @param pData   The poll data associated with the descriptor:
     *
     *                 data->handler  : Handler that knows how to deal with events
     *                                  for this particular type of 'struct mxs_poll_data'.
     *                 data->thread.id: Will be updated by the worker.
     *
     * @attention The provided file descriptor must be non-blocking.
     * @attention @c pData must remain valid until the file descriptor is
     *            removed from the worker.
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    bool add_fd(int fd, uint32_t events, MXS_POLL_DATA* pData);

    /**
     * Remove a file descriptor from the worker's epoll instance.
     *
     * @param fd  The file descriptor to be removed.
     *
     * @return True on success, false on failure.
     */
    bool remove_fd(int fd);

    /**
     * Main function of worker.
     *
     * The worker will run the poll loop, until it is told to shut down.
     *
     * @attention  This function will run in the calling thread.
     *
     * @param pSem Semaphore that is posted on once the thread has started
     */
    void run(mxs::Semaphore* pSem = NULL);

    /**
     * Run worker in separate thread.
     *
     * This function will start a new thread, in which the `run`
     * function will be executed.
     *
     * @return True if the thread could be started, false otherwise.
     */
    bool start();

    /**
     * Waits for the worker to finish.
     */
    void join();

    /**
     * Initate shutdown of worker.
     *
     * @attention A call to this function will only initiate the shutdowm,
     *            the worker will not have shut down when the function returns.
     *
     * @attention This function is signal safe.
     */
    void shutdown();

    /**
     * Query whether worker should shutdown.
     *
     * @return True, if the worker should shut down, false otherwise.
     */
    bool should_shutdown() const
    {
        return m_should_shutdown;
    }

    /**
     * Executes a task on the worker thread.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once the task's `execute` return.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted to the worker (i.e. not executed yet),
               false otherwise.
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
    bool execute(std::function<void ()> func, mxb::Semaphore* pSem, enum execute_mode_t mode);

    bool execute(std::function<void ()> func, enum execute_mode_t mode)
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
    bool call(std::function<void ()> func, enum execute_mode_t mode);

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
     * Push a function for delayed execution.
     *
     * @param delay      The delay in milliseconds.
     * @param pFunction  The function to call.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    uint32_t delayed_call(int32_t delay,
                          bool (*pFunction)(Worker::Call::action_t action))
    {
        return add_delayed_call(new DelayedCallFunctionVoid(delay, pFunction));
    }

    /**
     * Push a function for delayed execution.
     *
     * @param delay      The delay in milliseconds.
     * @param pFunction  The function to call.
     * @param data       The data to be provided to the function when invoked.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class D>
    uint32_t delayed_call(int32_t delay,
                          bool (*pFunction)(Worker::Call::action_t action, D data),
                          D data)
    {
        return add_delayed_call(new DelayedCallFunction<D>(delay, pFunction, data));
    }

    /**
     * Push a member function for delayed execution.
     *
     * @param delay    The delay in milliseconds.
     * @param pMethod  The member function to call.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class T>
    uint32_t delayed_call(int32_t delay,
                          bool (T::*pMethod)(Worker::Call::action_t action),
                          T* pT)
    {
        return add_delayed_call(new DelayedCallMethodVoid<T>(delay, pMethod, pT));
    }

    /**
     * Push a member function for delayed execution.
     *
     * @param delay    The delay in milliseconds.
     * @param pMethod  The member function to call.
     * @param data     The data to be provided to the function when invoked.
     *
     * @return A unique identifier for the delayed call. Using that identifier
     *         the call can be cancelled.
     *
     * @attention When invoked, if @c action is @c Worker::Call::EXECUTE, the
     *            function should perform the delayed call and return @true, if
     *            the function should be called again. If the function returns
     *            @c false, it will not be called again.
     *
     *            If @c action is @c Worker::Call::CANCEL, then the function
     *            should perform whatever canceling actions are needed. In that
     *            case the return value is ignored and the function will not
     *            be called again.
     */
    template<class T, class D>
    uint32_t delayed_call(int32_t delay,
                          bool (T::*pMethod)(Worker::Call::action_t action, D data),
                          T* pT,
                          D data)
    {
        return add_delayed_call(new DelayedCallMethod<T, D>(delay, pMethod, pT, data));
    }

    /**
     * Cancel delayed call.
     *
     * When this function is called, the delayed call in question will be called
     * *synchronously* with the @c action argument being @c Worker::Call::CANCEL.
     * That is, when this function returns, the function has been canceled.
     *
     * @param id  The id that was returned when the delayed call was scheduled.
     *
     * @return True, if the id represented an existing delayed call.
     */
    bool cancel_delayed_call(uint32_t id);

protected:
    const int m_epoll_fd;             /*< The epoll file descriptor. */
    state_t   m_state;                /*< The state of the worker */

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
     * Default implementation does nothing.
     */
    virtual void epoll_tick();

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
    class DelayedCall;
    friend class DelayedCall;

    static uint32_t next_delayed_call_id()
    {
        // Called in single-thread context. Wrapping does not matter
        // as it is unlikely there would be 4 billion pending delayed
        // calls.
        return ++s_next_delayed_call_id;
    }

    class DelayedCall
    {
        DelayedCall(const DelayedCall&) = delete;;
        DelayedCall& operator = (const DelayedCall&) = delete;

    public:
        virtual ~DelayedCall()
        {
        }

        int32_t delay() const
        {
            return m_delay;
        }

        uint32_t id() const
        {
            return m_id;
        }

        int64_t at() const
        {
            return m_at;
        }

        bool call(Worker::Call::action_t action)
        {
            bool rv = do_call(action);
            // We try to invoke the function as often as it was specified. If the
            // delay is very short and the execution time for the function very long,
            // then we will not succeed with that and the function will simply be
            // invoked as frequently as possible.
            m_at += m_delay;
            return rv;
        }

    protected:
        DelayedCall(int32_t delay)
            : m_id(Worker::next_delayed_call_id())
            , m_delay(delay)
            , m_at(get_at(delay))
        {
            ss_dassert(delay > 0);
        }

        virtual bool do_call(Worker::Call::action_t action) = 0;

    private:
        static int64_t get_at(int32_t delay)
        {
            ss_dassert(delay > 0);

            struct timespec ts;
            ss_debug(int rv =) clock_gettime(CLOCK_MONOTONIC, &ts);
            ss_dassert(rv == 0);

            return delay + (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
        }

    private:
        uint32_t m_id;    // The id of the delayed call.
        int32_t  m_delay; // The delay in milliseconds.
        int64_t  m_at;    // The next time the function should be invoked.
    };

    template<class D>
    class DelayedCallFunction : public DelayedCall
    {
        DelayedCallFunction(const DelayedCallFunction&) = delete;
        DelayedCallFunction& operator = (const DelayedCallFunction&) = delete;

    public:
        DelayedCallFunction(int32_t delay,
                            bool (*pFunction)(Worker::Call::action_t action, D data), D data)
            : DelayedCall(delay)
            , m_pFunction(pFunction)
            , m_data(data)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action)
        {
            return m_pFunction(action, m_data);
        }

    private:
        bool (*m_pFunction)(Worker::Call::action_t, D);
        D      m_data;
    };

    // Explicit specialization requires namespace scope
    class DelayedCallFunctionVoid : public DelayedCall
    {
        DelayedCallFunctionVoid(const DelayedCallFunctionVoid&) = delete;
        DelayedCallFunctionVoid& operator = (const DelayedCallFunctionVoid&) = delete;

    public:
        DelayedCallFunctionVoid(int32_t delay,
                                bool (*pFunction)(Worker::Call::action_t action))
            : DelayedCall(delay)
            , m_pFunction(pFunction)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action)
        {
            return m_pFunction(action);
        }

    private:
        bool (*m_pFunction)(Worker::Call::action_t action);
    };

    template<class T, class D>
    class DelayedCallMethod : public DelayedCall
    {
        DelayedCallMethod(const DelayedCallMethod&) = delete;
        DelayedCallMethod& operator = (const DelayedCallMethod&) = delete;

    public:
        DelayedCallMethod(int32_t delay,
                          bool (T::*pMethod)(Worker::Call::action_t action, D data),
                          T* pT,
                          D data)
            : DelayedCall(delay)
            , m_pMethod(pMethod)
            , m_pT(pT)
            , m_data(data)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action)
        {
            return (m_pT->*m_pMethod)(action, m_data);
        }

    private:
        bool (T::*m_pMethod)(Worker::Call::action_t, D);
        T* m_pT;
        D m_data;
    };

    template<class T>
    class DelayedCallMethodVoid : public DelayedCall
    {
        DelayedCallMethodVoid(const DelayedCallMethodVoid&) = delete;
        DelayedCallMethodVoid& operator = (const DelayedCallMethodVoid&) = delete;

    public:
        DelayedCallMethodVoid(int32_t delay,
                              bool (T::*pMethod)(Worker::Call::action_t),
                              T* pT)
            : DelayedCall(delay)
            , m_pMethod(pMethod)
            , m_pT(pT)
        {
        }

    private:
        bool do_call(Worker::Call::action_t action)
        {
            return (m_pT->*m_pMethod)(action);
        }

    private:
        bool (T::*m_pMethod)(Worker::Call::action_t);
        T* m_pT;
    };

    uint32_t add_delayed_call(DelayedCall* pDelayed_call);
    void adjust_timer();

    void handle_message(MessageQueue& queue, const MessageQueue::Message& msg); // override

    static void thread_main(Worker* pThis, mxs::Semaphore* pSem);

    void poll_waitevents();

    void tick();

private:
    class LaterAt : public std::binary_function<const DelayedCall*, const DelayedCall*, bool>
    {
    public:
        bool operator () (const DelayedCall* pLhs, const DelayedCall* pRhs)
        {
            return pLhs->at() > pRhs->at();
        }
    };

    typedef DelegatingTimer<Worker>                         PrivateTimer;
    typedef std::multimap<int64_t, DelayedCall*>            DelayedCallsByTime;
    typedef std::unordered_map<uint32_t, DelayedCall*>      DelayedCallsById;

    uint32_t           m_max_events;           /*< Maximum numer of events in each epoll_wait call. */
    STATISTICS         m_statistics;           /*< Worker statistics. */
    MessageQueue*      m_pQueue;               /*< The message queue of the worker. */
    std::thread        m_thread;               /*< The thread object of the worker. */
    bool               m_started;              /*< Whether the thread has been started or not. */
    bool               m_should_shutdown;      /*< Whether shutdown should be performed. */
    bool               m_shutdown_initiated;   /*< Whether shutdown has been initated. */
    uint32_t           m_nCurrent_descriptors; /*< Current number of descriptors. */
    uint64_t           m_nTotal_descriptors;   /*< Total number of descriptors. */
    Load               m_load;                 /*< The worker load. */
    PrivateTimer*      m_pTimer;               /*< The worker's own timer. */
    DelayedCallsByTime m_sorted_calls;         /*< Current delayed calls sorted by time. */
    DelayedCallsById   m_calls;                /*< Current delayed calls indexed by id. */

    static uint32_t    s_next_delayed_call_id; /*< The next delayed call id. */
};

}
