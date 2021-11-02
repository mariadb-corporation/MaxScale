/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxbase/ccdefs.hh>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stack>
#include <thread>

namespace maxbase
{

class ThreadPool
{
public:
    using Task = std::function<void()>;

    class Thread
    {
    public:
        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        /**
         * Creates a Thread object with an associated thread.
         */
        Thread();

        /**
         * Destroys the Thread object and its associated thread. If the
         * Thread has not already been stopped, @c stop(true) will be called.
         */
        ~Thread();

        /**
         * Enqueue a task (aka std::function<void()>) for execution on
         * the associated thread.
         *
         * @attn Must not be called if @s stop() has been called.
         *
         * @param task  The task to enqueue.
         */
        void execute(const Task& task);

        /**
         * Stop the thread.
         *
         * @attn Must not be called more than once.
         *
         * @param abandon_tasks  If false, then all pending tasks will be executed
         *                       before the thread exits. If true, then the current
         *                       task (if one is running) is allowed to finish after
         *                       which the thread exits without executing any pending
         *                       tasks.
         */
        void stop(bool abandon_tasks = false);

    private:
        void main();

    private:
        std::thread             m_thread;
        std::queue<Task>        m_tasks;
        std::mutex              m_tasks_mx;
        std::condition_variable m_tasks_cv;
        bool                    m_stop { false };
        bool                    m_abandon_tasks { false };
    };

    static constexpr int UNLIMITED = std::numeric_limits<int>::max();

    /**
     * Creates a thread pool with at most the specified number of threads.
     *
     * @param nMax_threads  The maximum number of threads created by the pool.
     */
    ThreadPool(int nMax_threads = UNLIMITED);

    /**
     * Terminates the pool. If the pool has not already be stopped, then
     * @c stop(true) will be called.
     */
    ~ThreadPool();

    /**
     * The maximum number of threads.
     *
     * @return The maximum number of thread as specified at construction time.
     */
    int max_num_of_threads() const
    {
        return m_nMax_threads;
    }

    /**
     * The current number of threads.
     *
     * @return The current number of threads.
     */
    int num_of_threads() const;

    /**
     * Execute a task on one thread in the pool.
     *
     * - If there are idle threads, then the most recently used will be used
     *   for executing the task.
     * - If there are no idle threads and the maximum number of threads has not
     *   been reached, then a new thread is created using which the task is
     *   executed.
     * - If there are no idle threads and the maximum number of threads has been
     *   reached, then the task is queued for execution and will be executed by
     *   the first thread that becomes idle.
     *
     * @attn Must not be called if @s stop() has been called.
     *
     * @param task  The task to execute.
     */
    void execute(const Task& task);

    /**
     * Stop the pool.
     *
     * @attn Must not be called more than at most once.
     *
     * @param abandon_tasks  If false, then all pending tasks will be executed
     *                       before the threads in the pool exits. If true, then
     *                       the current task (if one is running) of each thread is
     *                       allowed to finish after which the threads exit without
     *                       executing any pending tasks.
     */
    void stop(bool abandon_tasks = false);

private:
    bool                     m_stop { false };
    int                      m_nThreads { 0 };
    std::stack<Thread*>      m_idle_threads;
    mutable std::mutex       m_idle_threads_mx;
    std::condition_variable  m_idle_threads_cv;
    std::queue<Task>         m_tasks;
    std::mutex               m_tasks_mx;
    const int                m_nMax_threads;
};

}
