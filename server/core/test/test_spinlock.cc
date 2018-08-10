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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 18/08-2014   Mark Riddoch        Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#include <maxscale/spinlock.h>


/**
 * test1    spinlock_acquire_nowait tests
 *
 * Test that spinlock_acquire_nowait returns false if the spinlock
 * is already taken.
 *
 * Test that spinlock_acquire_nowait returns true if the spinlock
 * is not taken.
 *
 * Test that spinlock_acquire_nowait does hold the spinlock.
 */
static int
test1()
{
    SPINLOCK    lck;

    spinlock_init(&lck);
    spinlock_acquire(&lck);
    if (spinlock_acquire_nowait(&lck))
    {
        fprintf(stderr, "spinlock_acquire_nowait: test 1.1 failed.\n");
        return 1;
    }
    spinlock_release(&lck);
    if (!spinlock_acquire_nowait(&lck))
    {
        fprintf(stderr, "spinlock_acquire_nowait: test 1.2 failed.\n");
        return 1;
    }
    if (spinlock_acquire_nowait(&lck))
    {
        fprintf(stderr, "spinlock_acquire_nowait: test 1.3 failed.\n");
        return 1;
    }
    spinlock_release(&lck);

    return 0;
}

static int acquire_time;

static void
test2_helper(void *data)
{
    SPINLOCK   *lck = (SPINLOCK *)data;
    unsigned long   t1 = time(0);

    spinlock_acquire(lck);
    acquire_time = time(0) - t1;
    spinlock_release(lck);
    return;
}

/**
 * test2    spinlock_acquire tests
 *
 * Check that spinlock correctly blocks another thread whilst the spinlock
 * is held.
 *
 * Take out a lock.
 * Start a second thread to take the same lock
 * sleep for 10 seconds
 * release lock
 * verify that second thread took at least 8 seconds to obtain the lock
 */
static int
test2()
{
    SPINLOCK    lck;
    std::thread handle;
    struct timespec sleeptime;

    sleeptime.tv_sec = 10;
    sleeptime.tv_nsec = 0;

    acquire_time = 0;
    spinlock_init(&lck);
    spinlock_acquire(&lck);
    handle = std::thread(test2_helper, (void *)&lck);
    nanosleep(&sleeptime, NULL);
    spinlock_release(&lck);
    handle.join();

    if (acquire_time < 8)
    {
        fprintf(stderr, "spinlock: test 2 failed.\n");
        return 1;
    }
    return 0;
}

/**
 * test3    spinlock_acquire tests process bound threads
 *
 * Check that spinlock correctly blocks all other threads whilst the spinlock
 * is held.
 *
 * Start multiple threads that obtain spinlock and run process bound
 */
#define THREADS 5
#define ITERATIONS 50000
#define PROCESS_LOOP 10000
#define SECONDS 15
#define NANOTIME 100000

static int  times_run, failures;
static volatile int active;
static int  threadrun[THREADS];
static int  nowait[THREADS];
static SPINLOCK lck;
static void
test3_helper(void *data)
{
// SPINLOCK   *lck = (SPINLOCK *)data;
    int         i;
    int         n = *(int *)data;
    time_t          rawtime;

#if defined(ADD_SOME_NANOSLEEP)
    struct timespec sleeptime;

    sleeptime.tv_sec = 0;
    sleeptime.tv_nsec = 1;
#endif

    while (1)
    {
        if (spinlock_acquire_nowait(&lck))
        {
            nowait[n]++;
        }
        else
        {
            spinlock_acquire(&lck);
        }
        if (times_run++ > ITERATIONS)
        {
            break;
        }
        threadrun[n]++;
        /*
        if (99 == (times_run % 100)) {
            time ( &rawtime );
            fprintf(stderr, "%s Done %d iterations of test, in thread %d.\n", asctime (localtime ( &rawtime )), times_run, n);
        }
         */
        if (0 != active)
        {
            fprintf(stderr, "spinlock: test 3 failed with active non-zero after lock obtained.\n");
            failures++;
        }
        else
        {
            active = 1;
            for (i = 0; i < PROCESS_LOOP; i++);
        }
        active = 0;
        spinlock_release(&lck);
        for (i = 0; i < (4 * PROCESS_LOOP); i++);
#if defined(ADD_SOME_NANOSLEEP)
        nanosleep(&sleeptime, NULL);
#endif
    }
    spinlock_release(&lck);
}

static int
test3()
{
// SPINLOCK lck;
    std::thread     handle[THREADS];
    int             i;
    int             tnum[THREADS];
    time_t          rawtime;

    times_run = 0;
    active = 0;
    failures = 0;
    spinlock_init(&lck);
    time ( &rawtime );
    fprintf(stderr, "%s Starting %d threads.\n", asctime (localtime ( &rawtime )), THREADS);
    for (i = 0; i < THREADS; i++)
    {
        threadrun[i] = 0;
        tnum[i] = i;
        handle[i] = std::thread(test3_helper, &tnum[i]);
    }
    for (i = 0; i < THREADS; i++)
    {
        fprintf(stderr, "spinlock_test 3 thread %d ran %d times, no wait %d times before waits.\n", i, threadrun[i],
                nowait[i]);
    }
    for (i = 0; i < THREADS; i++)
    {
        time ( &rawtime );
        fprintf(stderr, "%s spinlock_test 3 finished sleeps, about to wait for thread %d.\n",
                asctime (localtime ( &rawtime )), i);
        handle[i].join();
    }
    for (i = 0; i < THREADS; i++)
    {
        fprintf(stderr, "spinlock_test 3 thread %d ran %d times, no wait %d times.\n", i, threadrun[i], nowait[i]);
    }
    time ( &rawtime );
    fprintf(stderr, "%s spinlock_test 3 completed, %d failures.\n", asctime (localtime ( &rawtime )), failures);
    return 0 == failures ? 0 : 1;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();
    result += test2();
    result += test3();

    exit(result);
}

