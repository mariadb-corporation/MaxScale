#pragma once
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

#include <maxscale/cppdefs.hh>
#include <memory>
#include <vector>
#include <maxscale/platform.h>
#include <maxscale/session.h>
#include <maxscale/utils.hh>
#include "messagequeue.hh"
#include "poll.h"
#include "worker.h"
#include "workertask.hh"
#include "session.hh"

namespace maxscale
{

class Semaphore;

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
    int64_t  evq_length;                  /*< Event queue length */
    int64_t  evq_max;                     /*< Maximum event queue length */
    int64_t  blockingpolls;               /*< Number of epoll_waits with a timeout specified */
    uint32_t qtimes[N_QUEUE_TIMES + 1];
    uint32_t exectimes[N_QUEUE_TIMES + 1];
    int64_t  maxqtime;
    int64_t  maxexectime;
};

class Worker : public MXS_WORKER
             , private MessageQueue::Handler
             , private MXS_POLL_DATA
{
    Worker(const Worker&);
    Worker& operator = (const Worker&);

public:
    typedef WORKER_STATISTICS     STATISTICS;
    typedef WorkerTask            Task;
    typedef WorkerDisposableTask  DisposableTask;
    typedef Registry<MXS_SESSION> SessionsById;
    typedef std::vector<DCB*>     Zombies;

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

    /**
     * Initialize the worker mechanism.
     *
     * To be called once at process startup. This will cause as many workers
     * to be created as the number of threads defined.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    static bool init();

    /**
     * Finalize the worker mechanism.
     *
     * To be called once at process shutdown. This will cause all workers
     * to be destroyed. When the function is called, no worker should be
     * running anymore.
     */
    static void finish();

    /**
     * Returns the id of the worker
     *
     * @return The id of the worker.
     */
    int id() const
    {
        return m_id;
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
     * Returns statistics for all workers.
     *
     * @return Combined statistics.
     *
     * @attentions The statistics may no longer be accurate by the time it has
     *             been returned. The returned values may also not represent a
     *             100% consistent set.
     */
    static STATISTICS get_statistics();

    /**
     * Return a specific combined statistic value.
     *
     * @param what  What to return.
     *
     * @return The corresponding value.
     */
    static int64_t get_one_statistic(POLL_STAT what);

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
     * Add a file descriptor to the epoll instance shared between all workers.
     * Events occuring on the provided file descriptor will be handled by all
     * workers. This is primarily intended for listening sockets where the
     * only event is EPOLLIN, signaling that accept() can be used on the listening
     * socket for creating a connected socket to a client.
     *
     * @param fd      The file descriptor to be added.
     * @param events  Mask of epoll event types.
     * @param pData   The poll data associated with the descriptor:
     *
     *                  data->handler  : Handler that knows how to deal with events
     *                                   for this particular type of 'struct mxs_poll_data'.
     *                  data->thread.id: 0
     *
     * @return True, if the descriptor could be added, false otherwise.
     */
    static bool add_shared_fd(int fd, uint32_t events, MXS_POLL_DATA* pData);

    /**
     * Remove a file descriptor from the worker's epoll instance.
     *
     * @param fd  The file descriptor to be removed.
     *
     * @return True on success, false on failure.
     */
    bool remove_fd(int fd);

    /**
     * Remove a file descriptor from the epoll instance shared between all workers.
     *
     * @param fd  The file descriptor to be removed.
     *
     * @return True on success, false on failure.
     */
    static bool remove_shared_fd(int fd);

    /**
     * Register zombie for later deletion.
     *
     * @param pZombie  DCB that will be deleted at end of event loop.
     *
     * @note The DCB must be owned by this worker.
     */
    void register_zombie(DCB* pZombie);

    /**
     * Main function of worker.
     *
     * The worker will run the poll loop, until it is told to shut down.
     *
     * @attention  This function will run in the calling thread.
     */
    void run();

    /**
     * Run worker in separate thread.
     *
     * This function will start a new thread, in which the `run`
     * function will be executed.
     *
     * @param stack_size The stack size of the new thread. A value of 0 means
     *                   that the pthread default should be used.
     *
     * @return True if the thread could be started, false otherwise.
     */
    bool start(size_t stack_size = 0);

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
     * Posts a task to a worker for execution.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once the task's `execute` return.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted (i.e. not executed), false otherwise.
     *
     * @attention  The instance must remain valid for as long as it takes for the
     *             task to be transferred to the worker and its `execute` function
     *             to be called.
     *
     * The semaphore can be used for waiting for the task to be finished.
     *
     * @code
     *     Semaphore sem;
     *     MyTask task;
     *
     *     pWorker->execute(&task, &sem);
     *     sem.wait();
     *
     *     MyResult& result = task.result();
     * @endcode
     */
    bool post(Task* pTask, Semaphore* pSem = NULL, enum execute_mode_t mode = EXECUTE_AUTO);

    /**
     * Posts a task to a worker for execution.
     *
     * @param pTask  The task to be executed.
     * @param mode   Execution mode
     *
     * @return True if the task could be posted (i.e. not executed), false otherwise.
     *
     * @attention  Once the task has been executed, it will be deleted.
     */
    bool post(std::auto_ptr<DisposableTask> sTask, enum execute_mode_t mode = EXECUTE_AUTO);

    template<class T>
    bool post(std::auto_ptr<T> sTask, enum execute_mode_t mode = EXECUTE_AUTO)
    {
        return post(std::auto_ptr<DisposableTask>(sTask.release()), mode);
    }

    /**
     * Posts a task to all workers for execution.
     *
     * @param pTask  The task to be executed.
     * @param pSem   If non-NULL, will be posted once per worker when the task's
     *               `execute` return.
     *
     * @return How many workers the task was posted to.
     *
     * @attention The very same task will be posted to all workers. The task
     *            should either not have any sharable data or then it should
     *            have data specific to each worker that can be accessed
     *            without locks.
     */
    static size_t broadcast(Task* pTask, Semaphore* pSem = NULL);

    /**
     * Posts a task to all workers for execution.
     *
     * @param pTask  The task to be executed.
     *
     * @return How many workers the task was posted to.
     *
     * @attention The very same task will be posted to all workers. The task
     *            should either not have any sharable data or then it should
     *            have data specific to each worker that can be accessed
     *            without locks.
     *
     * @attention Once the task has been executed by all workers, it will
     *            be deleted.
     */
    static size_t broadcast(std::auto_ptr<DisposableTask> sTask);

    template<class T>
    static size_t broadcast(std::auto_ptr<T> sTask)
    {
        return broadcast(std::auto_ptr<DisposableTask>(sTask.release()));
    }

    /**
     * Executes a task on all workers in serial mode (the task is executed
     * on at most one worker thread at a time). When the function returns
     * the task has been executed on all workers.
     *
     * @param task  The task to be executed.
     *
     * @return How many workers the task was posted to.
     *
     * @warning This function is extremely inefficient and will be slow compared
     * to the other functions. Only use this function when printing thread-specific
     * data to stdout.
     */
    static size_t execute_serially(Task& task);

    /**
     * Executes a task on all workers concurrently and waits until all workers
     * are done. That is, when the function returns the task has been executed
     * by all workers.
     *
     * @param task  The task to be executed.
     *
     * @return How many workers the task was posted to.
     */
    static size_t execute_concurrently(Task& task);

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
     * Return a reference to the session registry of this worker.
     *
     * @return Session registry.
     */
    SessionsById& session_registry();

    /**
     * Broadcast a message to all worker.
     *
     * @param msg_id  The message id.
     * @param arg1    Message specific first argument.
     * @param arg2    Message specific second argument.
     *
     * @return The number of messages posted; if less that ne number of workers
     *         then some postings failed.
     *
     * @attention The return value tells *only* whether message could be posted,
     *            *not* that it has reached the worker.
     *
     * @attentsion Exactly the same arguments are passed to all workers. Take that
     *             into account if the passed data must be freed.
     *
     * @attention This function is signal safe.
     */
    static size_t broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

    /**
     * Initate shutdown of all workers.
     *
     * @attention A call to this function will only initiate the shutdowm,
     *            the workers will not have shut down when the function returns.
     *
     * @attention This function is signal safe.
     */
    static void shutdown_all();

    /**
     * Return the worker associated with the provided worker id.
     *
     * @param worker_id  A worker id.
     *
     * @return The corresponding worker instance, or NULL if the id does
     *         not correspond to a worker.
     */
    static Worker* get(int worker_id);

    /**
     * Return the worker associated with the current thread.
     *
     * @return The worker instance, or NULL if the current thread does not have a worker.
     */
    static Worker* get_current();

    /**
     * Return the worker id associated with the current thread.
     *
     * @return A worker instance, or -1 if the current thread does not have a worker.
     */
    static int get_current_id();

    /**
     * Set the number of non-blocking poll cycles that will be done before
     * a blocking poll will take place.
     *
     * @param nbpolls  Number of non-blocking polls to perform before blocking.
     */
    static void set_nonblocking_polls(unsigned int nbpolls);

    /**
     * Maximum time to block in epoll_wait.
     *
     * @param maxwait  Maximum wait time in millliseconds.
     */
    static void set_maxwait(unsigned int maxwait);

private:
    Worker(int id,
           int epoll_fd);
    virtual ~Worker();

    static Worker* create(int id, int epoll_listener_fd);

    void delete_zombies();

    bool post_disposable(DisposableTask* pTask, enum execute_mode_t mode = EXECUTE_AUTO);

    void handle_message(MessageQueue& queue, const MessageQueue::Message& msg); // override

    static void thread_main(void* arg);

    void poll_waitevents();

    static uint32_t epoll_instance_handler(struct mxs_poll_data* data, int wid, uint32_t events);
    uint32_t handle_epoll_events(uint32_t events);

private:
    int           m_id;                 /*< The id of the worker. */
    state_t       m_state;              /*< The state of the worker */
    int           m_epoll_fd;           /*< The epoll file descriptor. */
    STATISTICS    m_statistics;         /*< Worker statistics. */
    MessageQueue* m_pQueue;             /*< The message queue of the worker. */
    THREAD        m_thread;             /*< The thread handle of the worker. */
    bool          m_started;            /*< Whether the thread has been started or not. */
    bool          m_should_shutdown;    /*< Whether shutdown should be performed. */
    bool          m_shutdown_initiated; /*< Whether shutdown has been initated. */
    SessionsById  m_sessions;           /*< A mapping of session_id->MXS_SESSION. The map
                                         *  should contain sessions exclusive to this
                                         *  worker and not e.g. listener sessions. For now,
                                         *  it's up to the protocol to decide whether a new
                                         *  session is added to the map. */
    Zombies       m_zombies;            /*< DCBs to be deleted. */
};

}
