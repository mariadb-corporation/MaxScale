/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxbase/ccdefs.hh>
#include <cerrno>
#include <climits>
#include <maxbase/assert.h>
#include <maxbase/semaphore.h>

namespace maxbase
{

class Semaphore
{
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator = (const Semaphore&) = delete;

public:
    enum signal_approach_t
    {
        HONOUR_SIGNALS, /* Honour signals and return when interrupted. */
        IGNORE_SIGNALS  /* Ignore signals and re-issue the comment when signals occur. */
    };

    /**
     * @brief Constructor
     *
     * @param initial_count  The initial count of the semaphore.
     *
     * @attention If the value `initial_count` is larger than `SEM_VALUE_MAX`,
     *            the the value will be adjusted down to `SEM_VALUE_MAX`.
     */
    Semaphore(uint32_t initial_count = 0)
    {
        if (initial_count > SEM_VALUE_MAX)
        {
            initial_count = SEM_VALUE_MAX;
        }

        MXB_AT_DEBUG(int rc =) sem_init(&m_sem, 0, initial_count);
        mxb_assert(rc == 0);
    }

    /**
     * @brief Destructor
     *
     * When the semaphore is destructed, its count should be 0 and nobody
     * should be waiting on it.
     */
    ~Semaphore()
    {
#ifdef SS_DEBUG
        int count;
        int rc = sem_getvalue(&m_sem, &count);
        mxb_assert(rc == 0);
        mxb_assert(count == 0);
#endif
        MXB_AT_DEBUG(rc =) sem_destroy(&m_sem);
        mxb_assert(rc == 0);
    }

    /**
     * @brief Post the semaphore.
     *
     * Increments the semaphore. If others threads were blocked in `wait`
     * one of them will subsequently return.
     *
     * @return `True` if the semaphore could be posed, otherwise `false`.
     *         If `false` is returned, then the maximum count of the sempahore
     *         has been reached.
     */
    bool post() const
    {
        int rc = sem_post(&m_sem);
        mxb_assert((rc == 0) || (errno == EOVERFLOW));
#ifdef SS_DEBUG
        if ((rc != 0) && (errno == EOVERFLOW))
        {
            mxb_assert_message(!true, "Semaphore overflow; indicates endless loop.");
        }
#endif
        return rc == 0;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * If the semaphore count is greater than zero, decrements the count and
     * returns immediately. Otherwise blocks the caller until someone posts
     * the semaphore.
     *
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return `True` if the semaphore was waited for, `false` otherwise.
     *
     * @attention The function can return `false` only if `signal_approach`
     *            is `HONOUR_SIGNALS`.
     */
    bool wait(signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        int rc;
        do
        {
            rc = sem_wait(&m_sem);
        }
        while ((rc != 0) && ((errno == EINTR) && (signal_approach == IGNORE_SIGNALS)));

        mxb_assert((rc == 0) || ((errno == EINTR) && (signal_approach == HONOUR_SIGNALS)));

        return rc == 0;
    }

    /**
     * @brief Waits multiple times on the semaphore.
     *
     * If the semaphore count is greater than or equal to the specified amount,
     * decrements the count and returns immediately. Otherwise blocks the caller
     * until the semaphore has been posted the required number of times.
     *
     * @param n_wait           How many times should be waited.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return How many times the semaphore has been waited on.
     *
     * @attention The function can return a different number than `n_wait` only
     *            if `signal_approach` is `HONOUR_SIGNALS`.
     */
    size_t wait_n(size_t n_wait,
                  signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        bool waited = true;
        size_t n_waited = 0;

        while (waited && n_wait--)
        {
            waited = wait(signal_approach);
            if (waited)
            {
                ++n_waited;
            }
        }

        return n_waited;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * If the semaphore count is greater that zero, decrements the count and
     * returns immediately.
     *
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return `True` if the semaphore was waited for, `false` otherwise.
     *
     * @attention If the function returns `false` and `signal_approch` is
     *            `HONOUR_SIGNALS` then the caller must check the value of
     *            `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `EAGAIN`
     *            and in the latter `EINTR`.
     */
    bool trywait(signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        errno = 0;

        int rc;
        do
        {
            rc = sem_trywait(&m_sem);
        }
        while ((rc != 0) && ((errno == EINTR) && (signal_approach == IGNORE_SIGNALS)));

        mxb_assert((rc == 0) ||
                   (errno == EAGAIN) ||
                   ((errno == EINTR) && (signal_approach == HONOUR_SIGNALS)));

        return rc == 0;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore at most until the specified time.
     *
     * @param ts               The *absolute* time until which the waiting at
     *                         most is performed.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return True if the waiting could be performed, false otherwise.
     *
     * @attention If the function returns `false` and `signal_approch` is
     *            `HONOUR_SIGNALS` then the caller must check the value of
     *            `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    bool timedwait(struct timespec& ts,
                   signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        errno = 0;

        int rc;
        do
        {
            rc = sem_timedwait(&m_sem, &ts);
        }
        while ((rc != 0) && ((errno == EINTR) && (signal_approach == IGNORE_SIGNALS)));

        mxb_assert((rc == 0) ||
                   (errno == ETIMEDOUT) ||
                   ((errno == EINTR) && (signal_approach == HONOUR_SIGNALS)));

        return rc == 0;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore the specified number of times, at most until the
     * specified time.
     *
     * @param n_wait           How many times should be waited.
     * @param ts               The *absolute* time until which the waiting at
     *                         most is performed.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return How many times the semaphore has been waited on. If the
     *         function times out or is interrupted, then the returned
     *         value will be less than `n_wait`.
     *
     * @attention If the function returns a value less than `n_count` and
     *            `signal_approch` is `HONOUR_SIGNALS` then the caller must check
     *            the value of `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    size_t timedwait_n(size_t n_wait,
                       struct timespec& ts,
                       signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        bool waited = true;
        size_t n_waited = 0;

        while (waited && n_wait--)
        {
            waited = timedwait(ts, signal_approach);
            if (waited)
            {
                ++n_waited;
            }
        }

        return n_waited;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore at most until the specified amount of time
     * has passed.
     *
     * @param seconds          How many seconds to wait at most.
     * @param nseconds         How many nanonseconds to wait at most.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return True if the waiting could be performed, false otherwise.
     *
     * @attention `nseconds` must be less than 1000000000.
     *
     * @attention If the function returns `false` and `signal_approch` is
     *            `HONOUR_SIGNALS` then the caller must check the value of
     *            `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    bool timedwait(time_t seconds,
                   long nseconds,
                   signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        timespec ts;
        get_current_timespec(seconds, nseconds, &ts);
        return timedwait(ts, signal_approach);
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore the specified number of times at most until the
     * specified time.
     *
     * @param n_wait           How many times should be waited.
     * @param seconds          How many seconds to wait at most.
     * @param nseconds         How many nanonseconds to wait at most.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return How many times the semaphore has been waited on. If the
     *         function times out or is interrupted, then the returned
     *         value will be less than `n_wait`.
     *
     * @attention If the function returns a value less than `n_count` and
     *            `signal_approch` is `HONOUR_SIGNALS` then the caller must check
     *            the value of `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    size_t timedwait_n(size_t n_wait,
                       time_t seconds,
                       long nseconds,
                       signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        timespec ts;
        get_current_timespec(seconds, nseconds, &ts);
        return timedwait_n(n_wait, ts, signal_approach);
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore at most until the specified amount of time
     * has passed.
     *
     * @param seconds          How many seconds to wait at most.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return True if the waiting could be performed, false otherwise.
     *
     * @attention If the function returns `false` and `signal_approch` is
     *            `HONOUR_SIGNALS` then the caller must check the value of
     *            `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    bool timedwait(time_t seconds,
                   signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        return timedwait(seconds, 0, signal_approach);
    }

    /**
     * @brief Waits on the semaphore.
     *
     * Waits on the sempahore the specified number of times at most until the
     * specified time.
     *
     * @param n_wait           How many times should be waited.
     * @param seconds          How many seconds to wait at most.
     * @param signal_approach  Whether signals should be ignored or honoured.
     *
     * @return How many times the semaphore has been waited on. If the
     *         function times out or is interrupted, then the returned
     *         value will be less than `n_wait`.
     *
     * @attention If the function returns a value less than `n_count` and
     *            `signal_approch` is `HONOUR_SIGNALS` then the caller must check
     *            the value of `errno` to find out whether the call was timed out or
     *            interrupted. In the former case the value will be `ETIMEDOUT`
     *            and in the latter `EINTR.
     */
    bool timedwait_n(size_t n_wait,
                     time_t seconds,
                     signal_approach_t signal_approach = IGNORE_SIGNALS) const
    {
        return timedwait_n(n_wait, seconds, 0, signal_approach);
    }

private:
    static void get_current_timespec(time_t seconds, long nseconds, timespec* pTs);

private:
    mutable sem_t m_sem;
};

}
