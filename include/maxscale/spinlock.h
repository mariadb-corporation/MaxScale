/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <pthread.h>

MXS_BEGIN_DECLS

#define SPINLOCK pthread_mutex_t
#define SPINLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define spinlock_init(a) pthread_mutex_init(a, NULL)
#define spinlock_acquire(a) pthread_mutex_lock((pthread_mutex_t*)a)
#define spinlock_release(a) pthread_mutex_unlock((pthread_mutex_t*)a)

MXS_END_DECLS
