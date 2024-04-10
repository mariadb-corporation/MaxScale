/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#ifndef SS_DEBUG
#define SS_DEBUG
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <maxbase/ccdefs.hh>

#include <thread>
#include <vector>
#include <algorithm>
#include <functional>

#include <maxbase/log.hh>
#include <maxbase/latch.hh>

int main(int argc, char* argv[])
{
    mxb::Log logger(MXB_LOG_TARGET_STDOUT);

    for (int loops = 0; loops < 100; loops++)
    {
        std::vector<std::thread> threads;
        std::atomic<int> value {0};
        mxb::latch sync_latch{105};

        for (int i = 0; i < 100; i++)
        {
            threads.emplace_back([&](){
                value.fetch_add(1, std::memory_order_relaxed);
                sync_latch.arrive_and_wait();
                int v = value.load(std::memory_order_relaxed);
                mxb_assert_message(v == 100, "Value should be 100 after arrive_and_wait(): %d", v);
            });
        }

        sync_latch.count_down(5);

        while (!sync_latch.try_wait())
        {
            std::this_thread::yield();
        }

        int v = value.load(std::memory_order_relaxed);
        mxb_assert_message(v == 100, "Value should be 100 after try_wait() returns true: %d", v);

        std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
        v = value.load(std::memory_order_relaxed);
        mxb_assert_message(v == 100, "Value should be 100 after joining threads: %d", v);
    }

    return 0;
}
