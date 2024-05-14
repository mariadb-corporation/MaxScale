/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#pragma once
#include <maxbase/ccdefs.hh>

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <maxbase/assert.hh>

// Identical to std::latch from C++20. Probably less efficient as it uses a mutex and a condition_variable
// instead of std::atomic::wait.
namespace maxbase
{
class latch
{
public:
    latch(const latch&) = delete;
    latch& operator=(const latch&) = delete;

    latch(std::ptrdiff_t expected)
        : m_value(expected)
    {
    }

    void count_down(std::ptrdiff_t n = 1)
    {
        auto old = m_value.fetch_sub(n, std::memory_order_release);

        if (old == n)
        {
            m_cv.notify_all();
        }
    }

    bool try_wait() const noexcept
    {
        return m_value.load(std::memory_order_acquire) == 0;
    }

    void wait() const
    {
        std::unique_lock guard(m_lock);
        m_cv.wait(guard, [this](){
            return try_wait();
        });
    }

    void arrive_and_wait(std::ptrdiff_t n = 1)
    {
        count_down(n);
        wait();
    }

private:
    std::atomic<std::ptrdiff_t>     m_value;
    mutable std::mutex              m_lock;
    mutable std::condition_variable m_cv;
};
}
