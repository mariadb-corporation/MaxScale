#pragma once
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
#define THREAD         pthread_t
#define thread_self()  pthread_self()

extern THREAD *thread_start(THREAD *thd, void (*entry)(void *), void *arg);
extern void thread_wait(THREAD thd);
extern void thread_millisleep(int ms);

MXS_END_DECLS
