/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#ifdef SS_DEBUG
#include <maxbase/assert.hh>
#include <atomic>

namespace
{
static std::atomic<uint64_t> s_counter {1};
static uint64_t s_exception_freq {0};
}

namespace maxbase
{
void set_exception_frequency(uint64_t num)
{
    s_exception_freq = num;
}

bool maybe_error()
{
    bool error = false;

    if (s_exception_freq)
    {
        auto n = s_counter.fetch_add(1, std::memory_order_relaxed) % s_exception_freq;

        if (n == 0)
        {
            error = true;
        }
    }

    return error;
}
}
#endif
