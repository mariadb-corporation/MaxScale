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
#include <maxscale/thread.h>

/**
 * @file thread.c  - Implementation of thread related operations
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 25/06/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */


/**
 * Start a polling thread
 *
 * @param thd       Pointer to the THREAD object
 * @param entry     The entry point to call
 * @param arg       The argument to pass the thread entry point
 * @return          The thread handle or NULL if an error occurred
 */
THREAD *thread_start(THREAD *thd, void (*entry)(void *), void *arg)
{
    if (pthread_create(thd, NULL, (void *(*)(void *))entry, arg) != 0)
    {
        return NULL;
    }
    return thd;
}

/**
 * Wait for a running threads to complete.
 *
 * @param thd   The thread handle
 */
void
thread_wait(THREAD thd)
{
    void *rval;

    pthread_join((pthread_t)thd, &rval);
}

/**
 * Put the thread to sleep for a number of milliseconds
 *
 * @param ms    Number of milliseconds to sleep
 */
void
thread_millisleep(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}
