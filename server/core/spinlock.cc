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

#include <maxscale/spinlock.h>
#include <maxscale/atomic.h>
#include <time.h>
#include <maxscale/debug.h>


void spinlock_init(SPINLOCK *lock)
{
    lock->lock = 0;
#if SPINLOCK_PROFILE
    lock->spins = 0;
    lock->maxspins = 0;
    lock->acquired = 0;
    lock->waiting = 0;
    lock->max_waiting = 0;
    lock->contended = 0;
    lock->owner = 0;
#endif
}

void spinlock_acquire(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
#if SPINLOCK_PROFILE
    int spins = 0;

    atomic_add(&(lock->waiting), 1);
#endif

    while (__sync_lock_test_and_set(&(lock->lock), 1))
    {
#if SPINLOCK_PROFILE
        atomic_add(&(lock->spins), 1);
        spins++;
#endif
    }

#if SPINLOCK_PROFILE
    if (spins)
    {
        lock->contended++;
        if (lock->maxspins < spins)
        {
            lock->maxspins = spins;
        }
    }
    lock->acquired++;
    lock->owner = thread_self();
    atomic_add(&(lock->waiting), -1);
#endif
}

bool
spinlock_acquire_nowait(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
    if (__sync_lock_test_and_set(&(lock->lock), 1))
    {
        return false;
    }

#if SPINLOCK_PROFILE
    lock->acquired++;
    lock->owner = thread_self();
#endif

    return true;
}

void spinlock_release(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
    ss_dassert(lock->lock != 0);
#if SPINLOCK_PROFILE
    if (lock->waiting > lock->max_waiting)
    {
        lock->max_waiting = lock->waiting;
    }
#endif

    __sync_lock_release(&lock->lock);
}

void spinlock_stats(const SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl)
{
#if SPINLOCK_PROFILE
    reporter(hdl, "Spinlock acquired", lock->acquired);
    if (lock->acquired)
    {
        reporter(hdl, "Total no. of spins", lock->spins);
        if (lock->acquired)
        {
            reporter(hdl, "Average no. of spins (overall)", lock->spins / lock->acquired);
        }
        if (lock->contended)
        {
            reporter(hdl, "Average no. of spins (when contended)", lock->spins / lock->contended);
        }
        reporter(hdl, "Maximum no. of spins", lock->maxspins);
        reporter(hdl, "Maximim no. of blocked threads", lock->max_waiting);
        reporter(hdl, "Contended locks", lock->contended);
        if (lock->acquired)
        {
            reporter(hdl, "Contention percentage", (lock->contended * 100) / lock->acquired);
        }
    }
#endif
}
