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

#include <maxscale/cppdefs.hh>
#include <memory>
#include <maxscale/thread.h>
#include <maxscale/future.hh>

namespace maxscale
{

/**
 * The class maxscale::thread represents a single thread of execution. It
 * is based upon C++11's std::thread as documented here:
 * http://en.cppreference.com/w/cpp/thread/thread
 *
 * std::thread uses C++11's rvalue references, which are not available on the
 * environments where MaxScale is compiled. Consequently, some care is needed
 * when using maxscale::thread so that unintended copying does not occur.
 *
 * When C++11 is available, it should be straightforward to take the std::thread
 * into use.
 */
class thread
{
public:
    /**
     * Creates a thread object which does not represent a running thread.
     */
    thread();

    /**
     * Move constructor
     *
     * @note This looks like a regular copy-constructor, but should be treated as
     *       a move constructor.
     *
     * @param other  The thread to move. After the call, @c other will not refer
     *               to a thread.
     */
    thread(const thread& other);

    /**
     * Creates a new thread object and associates it with a thread of execution.
     * The new thread will executed the provided task using the provided argument.
     *
     * @note The actual thread is started by the constructor.
     *
     * @param task  The task to execute in the new thread.
     * @param arg   The argument to provide to the task when invoked.
     *              Must remain valid for the lifetime of the thread.
     */
    template<class R, class T>
    thread(packaged_task<R, T>& task, T arg)
        : m_pInternal(new internal(new task_packaged_task<R,T>(task, arg)))
    {
        run();
    }

    /**
     * Move a thread
     *
     * @note This looks like a regular assignment operator, but the assigned value
     *       is moved.
     *
     * @param rhs  The thread to move. After the call, @c rhs will not
     *             refer to a packaged task.
     */
    thread& operator = (const thread& rhs);

    /**
     * Destructor
     *
     * The thread must have been joined before the thread object is destructed.
     */
    ~thread();

    /**
     * Whether a thread is joinable
     *
     * @return True, if the thread can be joined, false otherwise.
     */
    bool joinable() const;

    /**
     * Join the thread.
     */
    void join();

    /**
     * Swap the content
     *
     * @param rhs  The thread to swap the contents with.
     */
    void swap(thread& rhs);

private:
    void run();

    class task
    {
    public:
        virtual ~task() {}

        virtual void run() = 0;
    };

    template<class R, class T>
    class task_packaged_task : public task
    {
    public:
        task_packaged_task(packaged_task<R, T>& task, T arg)
            : m_task(task)
            , m_arg(arg)
        {
        }

        void run()
        {
            m_task(m_arg);
        }

    private:
        packaged_task<R, T> m_task;
        T                   m_arg;
    };

    class internal
    {
    public:
        internal(task* pTask);
        ~internal();

        bool joinable() const;
        void join();

        void run();

    private:
        void main();
        static void main(void* pArg);

    private:
        task*  m_pTask;
        THREAD m_thread;
    };

private:
    mutable internal* m_pInternal;
};

}
