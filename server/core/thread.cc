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

#include <maxscale/thread.h>
#include <maxscale/log_manager.h>

THREAD *thread_start(THREAD *thd, void (*entry)(void *), void *arg, size_t stack_size)
{
    THREAD* rv = NULL;

    pthread_attr_t attr;
    int error = pthread_attr_init(&attr);

    if (error == 0)
    {
        if (stack_size != 0)
        {
            error = pthread_attr_setstacksize(&attr, stack_size);
        }

        if (error == 0)
        {
            error = pthread_create(thd, &attr, (void *(*)(void *))entry, arg);

            if (error == 0)
            {
                rv = thd;
            }
            else
            {
                MXS_ERROR("Could not start thread: %s", mxs_strerror(error));
            }
        }
        else
        {
            MXS_ERROR("Could not set thread stack size to %lu: %s", stack_size, mxs_strerror(error));
        }
    }
    else
    {
        MXS_ERROR("Could not initialize thread attributes: %s", mxs_strerror(error));
    }

    return rv;
}

void thread_wait(THREAD thd)
{
    void *rval;

    pthread_join((pthread_t)thd, &rval);
}

void thread_millisleep(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}
