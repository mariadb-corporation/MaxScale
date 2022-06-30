/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <thread>

#include <maxbase/assert.hh>
#include <maxbase/atomic.hh>


#define NTHR 10

static int running = 0;
static int expected = 0;

void test_load_store(void* data)
{
    int id = (size_t)data;

    while (atomic_load_int32(&running))
    {
        if (atomic_load_int32(&expected) % NTHR == id)
        {
            mxb_assert(__atomic_fetch_add(&expected, 1, __ATOMIC_SEQ_CST) % NTHR == id + 1);
        }
    }
}

int run_test(void (* func)(void*))
{
    std::thread threads[NTHR];

    atomic_store_int32(&expected, 0);
    atomic_store_int32(&running, 1);

    for (size_t i = 0; i < NTHR; i++)
    {
        threads[i] = std::thread(func, (void*)(i + 1));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    atomic_store_int32(&running, 0);

    for (int i = 0; i < NTHR; i++)
    {
        threads[i].join();
    }

    return atomic_load_int32(&expected);
}

int main(int argc, char** argv)
{
    int rval = 0;

    printf("test_load_store\n");
    run_test(test_load_store);
    return rval;
}
