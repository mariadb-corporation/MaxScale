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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 21/06/2016   Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
int debug_check_fail = 1;
#else
// This is defined in the queuemanager code but only in debug builds
extern int debug_check_fail;
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <maxscale/random_jkiss.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/alloc.h>

#include "../maxscale/queuemanager.h"
#include "test_utils.h"

/**
 * test1    Allocate a queue and do lots of other things
 *
 */

#define TEST_QUEUE_SIZE 5
#define HEARTBEATS_TO_EXPIRE 3
#define NUMBER_OF_THREADS 4
#define THREAD_TEST_COUNT 1000000

static QUEUE_CONFIG *thread_queue;

static int
test1()
{
    QUEUE_CONFIG *queue;
    int filled = 0;
    int emptied = 0;
    int expired = 0;
    int input_counter = 0;
    int output_counter = 0;

    random_jkiss_init();
    hkheartbeat = 0;

    queue = mxs_queue_alloc(TEST_QUEUE_SIZE, HEARTBEATS_TO_EXPIRE);

    {
        QUEUE_ENTRY entry;
        if (mxs_dequeue(queue, &entry))
        {
            ss_dfprintf(stderr, "\nError mxs_dequeue on empty queue did not return false.\n");
            return 1;
        }

        if (mxs_dequeue_if_expired(queue, &entry))
        {
            ss_dfprintf(stderr, "\nError mxs_dequeue_if_expired on empty queue did not return false.\n");
            return 1;
        }
    }

    while (filled < 250 || emptied < 250 || expired < 250)
    {
        ss_dfprintf(stderr, "Input counter %d and output counter %d\n", input_counter, output_counter);
        ss_dfprintf(stderr, "Difference between counters %d\n", input_counter - output_counter);
        ss_dfprintf(stderr, "Filled %d, emptied %d, expired %d\n", filled, emptied, expired);
        if (random_jkiss() % 2)
        {
            int *entrynumber = (int*)MXS_MALLOC(sizeof(int));
            *entrynumber = input_counter;
            if (mxs_enqueue(queue, entrynumber))
            {
                input_counter++;
                if ((input_counter - output_counter) > TEST_QUEUE_SIZE)
                {
                    ss_dfprintf(stderr, "\nQueue full, but mxs_enqueue accepted entry.\n");
                    return 3;
                }
            }
            else
            {
                QUEUE_ENTRY entry;

                if ((input_counter - output_counter) != TEST_QUEUE_SIZE)
                {
                    ss_dfprintf(stderr, "\nFailed enqueue, but input counter %d and output counter %d do not differ by %d.\n",
                                input_counter,
                                output_counter,
                                TEST_QUEUE_SIZE);
                    return 4;
                }
                filled++;
                if (0 == (random_jkiss() % 5))
                {
                    if ((mxs_dequeue_if_expired(queue, &entry)))
                    {
                        if ((entry.heartbeat) > (hkheartbeat - HEARTBEATS_TO_EXPIRE))
                        {
                            ss_dfprintf(stderr, "\nReturned an expired entry even though none or not expired.\n");
                            return 5;
                        }
                        if (*(int *)entry.queued_object != output_counter)
                        {
                            ss_dfprintf(stderr, "\nOutput counter was %d, but dequeue gave %d.\n",
                                        output_counter,
                                        *(int *)entry.queued_object);
                            return 10;
                        }
                        output_counter++;
                        MXS_FREE(entry.queued_object);
                    }
                    else
                    {
                        hkheartbeat += (HEARTBEATS_TO_EXPIRE + 1);
                        if (mxs_dequeue_if_expired(queue, &entry))
                        {
                            if (*(int *)entry.queued_object != output_counter)
                            {
                                ss_dfprintf(stderr, "\nOutput counter was %d, but dequeue gave %d.\n",
                                            output_counter,
                                            *(int *)entry.queued_object);
                                return 6;
                            }
                            output_counter++;
                            MXS_FREE(entry.queued_object);
                        }
                        else
                        {
                            ss_dfprintf(stderr, "\nReturned no expired entry even though all are expired.\n");
                            return 7;
                        }
                        expired++;
                    }
                }
            }
        }
        else
        {
            QUEUE_ENTRY entry;
            if (mxs_dequeue(queue, &entry))
            {
                if (*(int *)entry.queued_object != output_counter)
                {
                    ss_dfprintf(stderr, "\nOutput counter was %d, but dequeue gave %d.\n",
                                output_counter,
                                *(int *)entry.queued_object);
                    return 8;
                }
                output_counter++;
                MXS_FREE(entry.queued_object);
            }
            else
            {
                if (input_counter != output_counter)
                {
                    ss_dfprintf(stderr, "\nNULL from dequeue, but input counter %d and output counter %d.\n",
                                input_counter,
                                output_counter);
                    return 9;
                }
                emptied++;
            }
        }
    }

    ss_dfprintf(stderr, "Successfully ended test\n");
    mxs_queue_free(queue);
    return 0;
}

static void *
thread_test(void *arg)
{
    int i;
    QUEUE_ENTRY entry;
    int emptied = 0;
    int filled = 0;

    for (i = 0; i < THREAD_TEST_COUNT; i++)
    {
        if (random_jkiss() % 2)
        {
            if (!mxs_enqueue(thread_queue, (void *)"Just for test"))
            {
                filled++;
            }
        }
        else
        {
            if (!mxs_dequeue(thread_queue, &entry))
            {
                emptied++;
            }
        }
    }
    ss_dfprintf(stderr, "Queue was full %d times, empty %d times\n", filled, emptied);

    return NULL;
}

static int
test2()
{
    pthread_t   tid[NUMBER_OF_THREADS];
    int err, i, limit;

    thread_queue = mxs_queue_alloc(TEST_QUEUE_SIZE, HEARTBEATS_TO_EXPIRE);
    limit = NUMBER_OF_THREADS;
    for (i = 0; i < limit; i++)
    {
        err = pthread_create(&tid[i], NULL, thread_test, NULL);
        ss_info_dassert((0 == err), "Must create threads successfully");
    }
    for (i = 0; i < limit; i++)
    {
        err = pthread_join(tid[i], NULL);
        ss_info_dassert((0 == err), "Must join threads successfully");
        ss_dfprintf(stderr, "\nThread %d ended with debug check fail at %d.\n", i, debug_check_fail);
    }
    mxs_queue_free(thread_queue);
    return debug_check_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += (test1() ? 1 : 0);
    result += (test2() ? 1 : 0);

    exit(result);
}
