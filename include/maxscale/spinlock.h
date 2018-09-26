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

typedef pthread_mutex_t SPINLOCK;
#define SPINLOCK_INIT PTHREAD_MUTEX_INITIALIZER

static inline void spinlock_init(SPINLOCK* a)
{
    pthread_mutex_init(a, NULL);
}

static inline void spinlock_acquire(const SPINLOCK* a)
{
    pthread_mutex_lock((SPINLOCK*)a);
}

static inline void spinlock_release(const SPINLOCK* a)
{
    pthread_mutex_unlock((SPINLOCK*)a);
}

MXS_END_DECLS
