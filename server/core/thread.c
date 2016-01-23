/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <thread.h>

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
