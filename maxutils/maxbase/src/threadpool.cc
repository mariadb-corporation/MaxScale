/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/threadpool.hh>
#include <maxbase/assert.h>

namespace maxbase
{

ThreadPool::Thread::Thread()
{
    m_thread = std::thread(&Thread::main, this);
}

ThreadPool::Thread::~Thread()
{
    if (!m_stop)
    {
        stop(true);
    }

    m_thread.join();
}

void ThreadPool::Thread::execute(const Task& task)
{
    mxb_assert(!m_stop);

    std::unique_lock<std::mutex> lock(m_tasks_mx);
    m_tasks.push(task);
    lock.unlock();

    m_tasks_cv.notify_one();
}

void ThreadPool::Thread::stop(bool abandon_tasks)
{
    mxb_assert(!m_stop);

    std::unique_lock<std::mutex> lock(m_tasks_mx);
    m_stop = true;
    m_abandon_tasks = abandon_tasks;
    lock.unlock();

    m_tasks_cv.notify_one();
}

void ThreadPool::Thread::main()
{
    bool terminate = false;

    while (!terminate)
    {
        std::unique_lock<std::mutex> lock(m_tasks_mx);
        m_tasks_cv.wait(lock, [this]() {
                return m_stop || !m_tasks.empty();
            });

        if (m_stop && (m_tasks.empty() || m_abandon_tasks))
        {
            lock.unlock();
            terminate = true;
        }
        else
        {
            Task task = std::move(m_tasks.front());
            m_tasks.pop();
            lock.unlock();

            task();
        }
    }
}

ThreadPool::ThreadPool::ThreadPool(int nMax_threads)
    : m_nMax_threads(nMax_threads)
{
}

ThreadPool::~ThreadPool()
{
    if (!m_stop)
    {
        stop(true);
    }
}

int ThreadPool::num_of_threads() const
{
    std::lock_guard<std::mutex> guard(m_idle_threads_mx);

    return m_nThreads;
}

void ThreadPool::execute(const Task& task)
{
    mxb_assert(!m_stop);

    Thread* pThread = nullptr;

    std::unique_lock<std::mutex> threads_lock(m_idle_threads_mx);

    if (m_idle_threads.empty())
    {
        if (m_nThreads < m_nMax_threads)
        {
            ++m_nThreads;

            pThread = new Thread;
        }
    }
    else
    {
        pThread = m_idle_threads.top();
        m_idle_threads.pop();
    }

    if (pThread)
    {
        threads_lock.unlock();

        pThread->execute([this, task, pThread]() {
                bool ready = false;

                task();

                do
                {
                    std::unique_lock<std::mutex> threads_lock(m_idle_threads_mx);
                    std::unique_lock<std::mutex> tasks_lock(m_tasks_mx);
                    if (!m_tasks.empty())
                    {
                        threads_lock.unlock();

                        Task t = std::move(m_tasks.front());
                        m_tasks.pop();
                        tasks_lock.unlock();

                        t();
                    }
                    else
                    {
                        tasks_lock.unlock();

                        m_idle_threads.push(pThread);
                        threads_lock.unlock();

                        ready = true;
                    }
                }
                while (!ready);

                m_idle_threads_cv.notify_one();
            });
    }
    else
    {
        std::unique_lock<std::mutex> tasks_lock(m_tasks_mx);
        m_tasks.push(task);
        tasks_lock.unlock();
        threads_lock.unlock();
    }
}

void ThreadPool::stop(bool abandon_tasks)
{
    mxb_assert(!m_stop);
    m_stop = true;

    int n = 0;

    std::unique_lock<std::mutex> threads_lock(m_idle_threads_mx);

    while (n != m_nThreads)
    {
        while (m_idle_threads.size() != 0)
        {
            Thread* pThread = m_idle_threads.top();
            m_idle_threads.pop();

            pThread->stop(abandon_tasks);
            delete pThread;

            ++n;
        }

        if (n != m_nThreads)
        {
            m_idle_threads_cv.wait(threads_lock, [this] () {
                    return !m_idle_threads.empty();
                });
        }
    }
}

}
