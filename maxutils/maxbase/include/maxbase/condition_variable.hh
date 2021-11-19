/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

#include <pthread.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace maxbase
{

// A replacement for std::condition_variable with a working wait_for implementation. The standard
// implementation has a bug in it that was fixed in GCC 10: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41861
class ConditionVariable
{
public:
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable(ConditionVariable&&) = delete;

    ConditionVariable()
    {
        pthread_condattr_init(&m_attr);
        pthread_condattr_setclock(&m_attr, CLOCK_MONOTONIC);
        pthread_cond_init(&m_cond, &m_attr);
    }

    ~ConditionVariable()
    {
        pthread_cond_destroy(&m_cond);
        pthread_condattr_destroy(&m_attr);
    }

    std::cv_status wait_for(std::unique_lock<std::mutex>& guard, std::chrono::steady_clock::duration d)
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
        long total_ns = ns.count() + ts.tv_nsec;
        ts.tv_sec += total_ns / 1000000000;
        ts.tv_nsec = total_ns % 1000000000;

        mxb_assert(guard.owns_lock());
        int rc = pthread_cond_timedwait(&m_cond, guard.mutex()->native_handle(), &ts);
        return rc != 0 && errno == ETIMEDOUT ? std::cv_status::timeout : std::cv_status::no_timeout;
    }

    void notify_one()
    {
        pthread_cond_signal(&m_cond);
    }

private:
    pthread_condattr_t m_attr;
    pthread_cond_t     m_cond;
};
}
