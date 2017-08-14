/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>

#include <stdio.h>

#include <maxscale/atomic.h>
#include <maxscale/debug.h>
#include <maxscale/thread.h>


#define NTHR 10

static int running = 0;
static int expected = 0;

void test_add(void* data)
{
    int id = (size_t)data;

    while (atomic_load_int32(&running))
    {
        atomic_add(&expected, id);
        atomic_add(&expected, -id);
        ss_dassert(atomic_load_int32(&expected) >= 0);
    }
}

void test_load_store(void* data)
{
    int id = (size_t)data;

    while (atomic_load_int32(&running))
    {
        if (atomic_load_int32(&expected) % NTHR == id)
        {
            ss_dassert(atomic_add(&expected, 1) % NTHR == id + 1);
        }
    }
}

static void* cas_dest = (void*)1;

void test_cas(void* data)
{
    int id = (size_t)data;
    static int loops = 0;

    while (atomic_load_int32(&running))
    {
        intptr_t my_value = (id + 1) % NTHR;
        intptr_t my_expected = id;

        while (!atomic_cas_ptr(&cas_dest, (void**)&my_expected, (void*)&my_value))
        {
            ;
        }

        loops++;
    }

    ss_dassert(loops > 0);
}

int run_test(void(*func)(void*))
{
    THREAD threads[NTHR];

    atomic_store_int32(&expected, 0);
    atomic_store_int32(&running, 1);

    for (size_t i = 0; i < NTHR; i++)
    {
        if (thread_start(&threads[i], func, (void*)(i + 1), 0) == NULL)
        {
            ss_dassert(false);
        }
    }

    thread_millisleep(2500);
    atomic_store_int32(&running, 0);

    for (int i = 0; i < NTHR; i++)
    {
        thread_wait(threads[i]);
    }

    return atomic_load_int32(&expected);
}

int main(int argc, char** argv)
{
    int rval = 0;

    run_test(test_load_store);
    run_test(test_add);
    run_test(test_cas);

    return rval;
}
