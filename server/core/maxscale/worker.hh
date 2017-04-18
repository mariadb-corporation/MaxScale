#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/platform.h>
#include "messagequeue.hh"
#include "worker.h"

namespace maxscale
{

class Worker : public MXS_WORKER
             , private MessageQueue::Handler
{
    Worker(const Worker&);
    Worker& operator = (const Worker&);

public:
    /**
     * Initialize the worker mechanism.
     *
     * To be called once at process startup. This will cause as many workers
     * to be created as the number of threads defined.
     */
    static void init();

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
     * Remove a file descriptor from a poll set.
     *
     * @param fd       The file descriptor to be removed.
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
     */
    void run();

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
           int epoll_fd,
           POLL_STATS* pPoll_stats,
           QUEUE_STATS* pQueue_stats);
    virtual ~Worker();

    static Worker* create(int id,
                          POLL_STATS* pPoll_stats,
                          QUEUE_STATS* pQueue_stats);

    void handle_message(MessageQueue& queue, const MessageQueue::Message& msg); // override

    static void thread_main(void* arg);

    static void poll_waitevents(int epoll_fd,
                                int thread_id,
                                POLL_STATS* poll_stats,
                                QUEUE_STATS* queue_stats,
                                bool (*should_shutdown)(void* data),
                                void* data);

private:
    int           m_id;                 /*< The id of the worker. */
    int           m_epoll_fd;           /*< The epoll file descriptor. */
    POLL_STATS*   m_pPoll_stats;        /*< Statistics for worker. */
    QUEUE_STATS*  m_pQueue_stats;       /*< Statistics for queue. */
    MessageQueue* m_pQueue;             /*< The message queue of the worker. */
    THREAD        m_thread;             /*< The thread handle of the worker. */
    bool          m_started;            /*< Whether the thread has been started or not. */
    bool          m_should_shutdown;    /*< Whether shutdown should be performed. */
    bool          m_shutdown_initiated; /*< Whether shutdown has been initated. */
};

}
