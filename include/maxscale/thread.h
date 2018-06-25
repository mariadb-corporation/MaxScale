#pragma once
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
 * @file thread.h       The gateway threading interface
 *
 * An encapsulation of the threading used by the gateway. This is designed to
 * isolate the majority of the gateway code from the pthread library, enabling
 * the gateway to be ported to a different threading package with the minimum
 * of changes.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * Thread type and thread identifier function macros
 */
#include <pthread.h>

typedef pthread_t THREAD;

/**
 * Obtain a handle to the calling thread
 *
 * @return The thread handle of the calling thread.
 */
static inline THREAD thread_self()
{
    return pthread_self();
}

/**
 * Start a secondary thread
 *
 * @param thd         Pointer to the THREAD object
 * @param entry       The entry point to call
 * @param arg         The argument to pass the thread entry point
 * @param stack_size  The stack size of the thread. If 0, the default
 *                    size will be used.
 *
 * @return            The thread handle or NULL if an error occurred
 */
extern THREAD *thread_start(THREAD *thd, void (*entry)(void *), void *arg, size_t stack_size);

/**
 * Wait for a running thread to complete.
 *
 * @param thd   The thread handle
 */
extern void thread_wait(THREAD thd);

/**
 * Put the calling thread to sleep for a number of milliseconds
 *
 * @param ms  Number of milliseconds to sleep
 */
extern void thread_millisleep(int ms);

MXS_END_DECLS
