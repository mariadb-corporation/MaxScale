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
#include <maxscale/debug.h>
#include <maxscale/semaphore.hh>

namespace maxscale
{

// Internal, not intended for public consumption.
class future_internal
{
public:
    virtual ~future_internal() {}
};

/**
 * The class template maxscale::future provides a mechanism to access the result of
 * asynchronous operations. It is based upon C++11's std::future as documented here:
 * http://en.cppreference.com/w/cpp/thread/future
 *
 * std::future uses C++11's rvalue references, which are not available on the
 * environments where MaxScale is compiled. Consequently, some care is needed
 * when using maxscale::future so that unintended copying does not occur.
 *
 * When C++11 is available, it should be straightforward to take the std::future
 * into use.
 */
template<class T>
class future
{
public:
    /**
     * Constructs a future with no shared state.
     */
    future()
    {
    }

    /**
     * Move constructor
     *
     * @note This looks like a regular copy-constructor, but should be treated as
     *       a move constructor.
     *
     * @param other  The future to move. After the call, @c other will not refer
     *               to a future result.
     */
    future(const future& other)
        : m_sInternal(other.m_sInternal)
    {
        other.m_sInternal.reset();
    }

    /**
     * Move a future.
     *
     * @note This looks like a regular assignment operator, but the assigned value
     *       is moved.
     *
     * @param rhs  The future to move. After the call, @c rhs will not refer
     *             to a future result.
     * @return *this
     */
    future& operator = (const future& rhs)
    {
        future copy(rhs);
        copy.swap(*this);
        return *this;
    }

    /**
     * Destructor
     */
    ~future()
    {
    }

    /**
     * Swap the content
     *
     * @param rhs  The future to swap the contents with.
     */
    void swap(future& rhs)
    {
        std::swap(m_sInternal, rhs.m_sInternal);
    }

    /**
     * Checks if the future refers to a shared state
     *
     * @return True, if this future refers to shared state, false otherwise.
     */
    bool valid() const
    {
        return m_sInternal.get() != NULL;
    }

    /**
     * Waits until the future has a valid result and returns it.
     *
     * @note After the function returns, the future will no longer be valid.
     *
     * @return The stored value.
     */
    T get()
    {
        ss_dassert(valid());
        if (valid())
        {
            T rv = m_sInternal->get();
            m_sInternal.reset();
            return rv;
        }
        else
        {
            MXS_ERROR("Get called on non-valid future.");
            return T();
        }
    }

    /**
     * Blocks until the result becomes available.
     *
     * @note Only a valid future can be waited for.
     */
    void wait() const
    {
        ss_dassert(valid());
        if (valid())
        {
            m_sInternal->wait();
        }
        else
        {
            MXS_ERROR("An attempt to wait on a non-valid future.");
        }
    }

public:
    class internal : public future_internal
    {
    public:
        internal()
            : m_t()
            , m_waited(false)
        {
        }

        ~internal()
        {
        }

        T get()
        {
            wait();
            return m_t;
        }

        void set(T t)
        {
            m_t = t;
            m_sem.post();
        }

        void wait() const
        {
            if (!m_waited)
            {
                m_sem.wait();
                m_waited = true;
            }
        }

    private:
        T            m_t;
        mutable bool m_waited;
        Semaphore    m_sem;
    };

    future(std::shared_ptr<internal> sInternal)
        : m_sInternal(sInternal)
    {
    }

private:
    mutable std::shared_ptr<internal> m_sInternal;
};

/**
 * The class template std::packaged_task wraps a function so that it can be called
 * asynchronously. It is based upon C++11 std::packaged_task as documented here:
 * http://en.cppreference.com/w/cpp/thread/packaged_task
 *
 * std::packaged_task uses C++11's rvalue references, which are not available on the
 * environments where MaxScale is compiled. Consequently, some care is needed
 * when using maxscale::packaged_task so that unintended copying does not occur.
 *
 * When C++11 is available, it should be straightforward to take the std::packed_task
 * into use.
 *
 * Contrary to std::packaged_task, also due to lack of functionality introduced by
 * C++11, maxscale::packaged_task is not fully generic, but can only package a function
 * returning a value and taking one argument.
 */
template<class R, class T>
class packaged_task
{
    typedef typename future<R>::internal internal;

public:
    typedef R (*function)(T);

    /**
     * Creates a packaged_task with no task and no shared state.
     */
    packaged_task()
        : m_f(NULL)
        , m_get_future_called(false)
    {
    }

    /**
     * Creates a packaged_task referring to the provided function.
     *
     * @param f The function to package.
     */
    packaged_task(function f)
        : m_f(f)
        , m_sInternal(new internal)
        , m_get_future_called(false)
    {
    }

    /**
     * Move constructor
     *
     * @note This looks like a regular copy-constructor, but should be treated as
     *       a move constructor.
     *
     * @param other  The packaged_task to move. After the call, @c other will not
     *               refer to a packaged task.
     */
    packaged_task(const packaged_task& other)
        : m_f(other.m_f)
        , m_sInternal(other.m_sInternal)
        , m_get_future_called(other.m_get_future_called)
    {
        other.m_f = NULL;
        other.m_sInternal.reset();
        other.m_get_future_called = false;
    }

    /**
     * Move a packaged_task
     *
     * @note This looks like a regular assignment operator, but the assigned value
     *       is moved.
     *
     * @param rhs  The packaged_task to move. After the call, @c rhs will not
     *             refer to a packaged task.
     */
    packaged_task& operator = (const packaged_task& rhs)
    {
        packaged_task copy(rhs);
        copy.swap(*this);
        return *this;
    }

    /**
     * Destructor
     */
    ~packaged_task()
    {
        if (m_sInternal.get())
        {
            ss_dassert(m_get_future_called);
            // The ownership of m_pFuture has moved to the future
            // that was obtained in the call to get_future().
            if (!m_get_future_called)
            {
                MXS_ERROR("Packaged task destructed without future result having been asked for.");
            }
        }
    }

    /**
     * Swap the content
     *
     * @param rhs  The packaged_task to swap the contents with.
     */
    void swap(packaged_task& rhs)
    {
        std::swap(m_f, rhs.m_f);
        std::swap(m_sInternal, rhs.m_sInternal);
        std::swap(m_get_future_called, rhs.m_get_future_called);
    }

    /**
     * Checks the validity of the packaged_task.
     *
     * @return True, if the packaged_task contains share state, false otherwise.
     */
    bool valid() const
    {
        return m_sInternal.get() != NULL;
    }

    /**
     * Returns a future which shares the same shared state as this packaged_task.
     *
     * @note @c get_future can be called only once for each packaged_task.
     *
     * @return A future.
     */
    future<R> get_future()
    {
        ss_dassert(!m_get_future_called);
        if (!m_get_future_called)
        {
            m_get_future_called = true;
            return future<R>(m_sInternal);
        }
        else
        {
            MXS_ERROR("get_future called more than once.");
            return future<R>();
        }
    };

    /**
     * Calls the stored task with the provided argument.
     *
     * After this call, anyone waiting for the shared result will be unblocked.
     */
    void operator()(T arg)
    {
        m_sInternal->set(m_f(arg));
    }

private:
    mutable function                  m_f;
    mutable std::shared_ptr<internal> m_sInternal;
    mutable bool                      m_get_future_called;
};

}
