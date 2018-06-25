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

#include <maxscale/thread.h>
#include <maxscale/thread.hh>
#include <maxscale/log_manager.h>

THREAD *thread_start(THREAD *thd, void (*entry)(void *), void *arg, size_t stack_size)
{
    THREAD* rv = NULL;

    pthread_attr_t attr;
    int error = pthread_attr_init(&attr);

    if (error == 0)
    {
        if (stack_size != 0)
        {
            error = pthread_attr_setstacksize(&attr, stack_size);
        }

        if (error == 0)
        {
            error = pthread_create(thd, &attr, (void *(*)(void *))entry, arg);

            if (error == 0)
            {
                rv = thd;
            }
            else
            {
                MXS_ERROR("Could not start thread: %s", mxs_strerror(error));
            }
        }
        else
        {
            MXS_ERROR("Could not set thread stack size to %lu: %s", stack_size, mxs_strerror(error));
        }
    }
    else
    {
        MXS_ERROR("Could not initialize thread attributes: %s", mxs_strerror(error));
    }

    return rv;
}

void thread_wait(THREAD thd)
{
    void *rval;

    pthread_join((pthread_t)thd, &rval);
}

void thread_millisleep(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}

//
// maxscale::thread
//

namespace maxscale
{

thread::thread()
    : m_pInternal(NULL)
{
}

thread::thread(const thread& other)
    : m_pInternal(other.m_pInternal)
{
    other.m_pInternal = NULL;
}

thread& thread::operator = (const thread& rhs)
{
    thread copy(rhs);
    copy.swap(*this);
    return *this;
}

thread::~thread()
{
    ss_dassert(!joinable());
    if (joinable())
    {
        MXS_ERROR("A thread that has not been joined is destructed.");
    }
    else
    {
        delete m_pInternal;
    }
}

bool thread::joinable() const
{
    return m_pInternal ? m_pInternal->joinable() : false;
}

void thread::join()
{
    ss_dassert(m_pInternal);
    if (!m_pInternal)
    {
        MXS_ERROR("Attempt to join a non-joinable thread.");
    }
    else
    {
        m_pInternal->join();
    }
}

void thread::swap(thread& rhs)
{
    std::swap(m_pInternal, rhs.m_pInternal);
}

void thread::run()
{
    ss_dassert(m_pInternal);
    m_pInternal->run();
}

thread::internal::internal(thread::task* pTask)
    : m_pTask(pTask)
    , m_thread(0)
{
}

thread::internal::~internal()
{
    ss_info_dassert(!m_pTask, "Thread not joined before destructed.");
    ss_dassert(m_thread == 0);
}

bool thread::internal::joinable() const
{
    return m_thread != 0;
}

void thread::internal::join()
{
    ss_dassert(joinable());
    thread_wait(m_thread);
    delete m_pTask;
    m_pTask = NULL;
    m_thread = 0;
}

void thread::internal::run()
{
    if (!thread_start(&m_thread, &thread::internal::main, this, 0))
    {
        MXS_ALERT("Could not start thread, MaxScale is likely to malfunction.");
    }
}

void thread::internal::main()
{
    m_pTask->run();
}

void thread::internal::main(void* pArg)
{
    static_cast<internal*>(pArg)->main();
}

}
