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
#include <errno.h>
#include <maxscale/semaphore.h>
#include <maxscale/debug.h>

namespace maxscale
{

class Semaphore
{
    Semaphore(const Semaphore&);
    Semaphore& operator = (const Semaphore&);

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

        ss_debug(int rc =) sem_init(&m_sem, 0, initial_count);
        ss_dassert(rc == 0);
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
        ss_dassert(rc == 0);
        ss_dassert(count == 0);
#endif
        ss_debug(rc =) sem_destroy(&m_sem);
        ss_dassert(rc == 0);
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
        ss_dassert((rc == 0) || (errno == EOVERFLOW));
#ifdef SS_DEBUG
        if ((rc != 0) && (errno == EOVERFLOW))
        {
            ss_info_dassert(!true, "Semaphore overflow; indicates endless loop.");
        }
#endif
        return rc == 0;
    }

    /**
     * @brief Waits on the semaphore.
     *
     * If the semaphore count is greater that zero, decrements the count and
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

        ss_dassert((rc == 0) || ((errno == EINTR) && (signal_approach == HONOUR_SIGNALS)));

        return rc == 0;
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

        ss_dassert((rc == 0) ||
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

        ss_dassert((rc == 0) ||
                   (errno == ETIMEDOUT) ||
                   ((errno == EINTR) && (signal_approach == HONOUR_SIGNALS)));

        return rc == 0;
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
                   signal_approach_t signal_approach = IGNORE_SIGNALS) const;

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

private:
    mutable sem_t m_sem;
};

}
