/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/threadpool.hh>
#include <string.h>
#include <maxbase/assert.hh>

namespace maxbase
{

const size_t MAX_THREAD_NAME_LEN = 15;

void set_thread_name(std::thread& thread, const std::string& name)
{
    if (name.size() > MAX_THREAD_NAME_LEN)
    {
        pthread_setname_np(thread.native_handle(), name.substr(0, MAX_THREAD_NAME_LEN).c_str());
    }
    else
    {
        pthread_setname_np(thread.native_handle(), name.c_str());
    }
}

namespace
{

std::string get_pthread_name(pthread_t thread)
{
    char buffer[MAX_THREAD_NAME_LEN + 1];
    if (pthread_getname_np(thread, buffer, MAX_THREAD_NAME_LEN + 1) != 0)
    {
        strcpy(buffer, "unknown");
    }

    return buffer;
}

}

std::string get_thread_name(const std::thread& thread)
{
    return get_pthread_name(const_cast<std::thread&>(thread).native_handle());
}

std::string get_thread_name()
{
    return get_pthread_name(pthread_self());
}


ThreadPool::Thread::Thread(const std::string& name)
{
    m_thread = std::thread(&Thread::main, this);
    set_thread_name(m_thread, name);
}

ThreadPool::Thread::~Thread()
{
    if (!m_stop)
    {
        stop(true);
    }

    m_thread.join();
}

void ThreadPool::Thread::set_name(const std::string& name)
{
    set_thread_name(m_thread, name);
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

void ThreadPool::execute(const Task& task, const std::string& name)
{
    mxb_assert(!m_stop);

    Thread* pThread = nullptr;

    std::unique_lock<std::mutex> threads_lock(m_idle_threads_mx);

    if (m_idle_threads.empty())
    {
        if (m_nThreads < m_nMax_threads)
        {
            ++m_nThreads;

            pThread = new Thread(name);
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

        pThread->set_name(name);
        pThread->execute([this, task, pThread]() {
                bool ready = false;

                task();

                do
                {
                    std::unique_lock<std::mutex> thr_lock(m_idle_threads_mx);
                    std::unique_lock<std::mutex> tasks_lock(m_tasks_mx);
                    if (!m_tasks.empty())
                    {
                        thr_lock.unlock();

                        auto tp = std::move(m_tasks.front());
                        m_tasks.pop();
                        tasks_lock.unlock();
                        pThread->set_name(tp.second);
                        tp.first();
                    }
                    else
                    {
                        tasks_lock.unlock();
                        pThread->set_name("idle");
                        m_idle_threads.push(pThread);
                        thr_lock.unlock();

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
        m_tasks.emplace(task, name);
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
