/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file spinlock.c  -  Spinlock operations for the MariaDB Corporation MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 10/06/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/spinlock.h>
#include <maxscale/atomic.h>
#include <time.h>
#include <maxscale/debug.h>

/**
 * Initialise a spinlock.
 *
 * @param lock The spinlock to initialise.
 */
void
spinlock_init(SPINLOCK *lock)
{
    lock->lock = 0;
#if SPINLOCK_PROFILE
    lock->spins = 0;
    lock->acquired = 0;
    lock->waiting = 0;
    lock->max_waiting = 0;
    lock->contended = 0;
#endif
}

/**
 * Acquire a spinlock.
 *
 * @param lock The spinlock to acquire
 */
void
spinlock_acquire(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
#if SPINLOCK_PROFILE
    int spins = 0;

    atomic_add(&(lock->waiting), 1);
#endif

#ifdef __GNUC__
    while (__sync_lock_test_and_set(&(lock->lock), 1))
        while (lock->lock)
        {
#else
    while (atomic_add(&(lock->lock), 1) != 0)
    {
        atomic_add(&(lock->lock), -1);
#endif
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

/**
 * Acquire a spinlock if it is not already locked.
 *
 * @param lock The spinlock to acquire
 * @return True if the spinlock was acquired, otherwise false
 */
int
spinlock_acquire_nowait(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
#ifdef __GNUC__
    if (__sync_lock_test_and_set(&(lock->lock), 1))
    {
        return FALSE;
    }
#else
    if (atomic_add(&(lock->lock), 1) != 0)
    {
        atomic_add(&(lock->lock), -1);
        return FALSE;
    }
#endif
#if SPINLOCK_PROFILE
    lock->acquired++;
    lock->owner = thread_self();
#endif
    return TRUE;
}

/*
 * Release a spinlock.
 *
 * @param lock The spinlock to release
 */
void
spinlock_release(const SPINLOCK *const_lock)
{
    SPINLOCK *lock = (SPINLOCK*)const_lock;
    ss_dassert(lock->lock != 0);
#if SPINLOCK_PROFILE
    if (lock->waiting > lock->max_waiting)
    {
        lock->max_waiting = lock->waiting;
    }
#endif
#ifdef __GNUC__
    __sync_synchronize(); /* Memory barrier. */
    lock->lock = 0;
#else
    atomic_add(&(lock->lock), -1);
#endif
}

/**
 * Report statistics on a spinlock. This only has an effect if the
 * spinlock code has been compiled with the SPINLOCK_PROFILE option set.
 *
 * NB A callback function is used to return the data rather than
 * merely printing to a DCB in order to avoid a dependency on the DCB
 * form the spinlock code and also to facilitate other uses of the
 * statistics reporting.
 *
 * @param lock          The spinlock to report on
 * @param reporter      The callback function to pass the statistics to
 * @param hdl           A handle that is passed to the reporter function
 */
void
spinlock_stats(const SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl)
{
#if SPINLOCK_PROFILE
    reporter(hdl, "Spinlock acquired", lock->acquired);
    if (lock->acquired)
    {
        reporter(hdl, "Total no. of spins", lock->spins);
        reporter(hdl, "Average no. of spins (overall)",
                 lock->spins / lock->acquired);
        if (lock->contended)
        {
            reporter(hdl, "Average no. of spins (when contended)",
                     lock->spins / lock->contended);
        }
        reporter(hdl, "Maximum no. of spins", lock->maxspins);
        reporter(hdl, "Maximim no. of blocked threads",
                 lock->max_waiting);
        reporter(hdl, "Contended locks", lock->contended);
        reporter(hdl, "Contention percentage",
                 (lock->contended * 100) / lock->acquired);
    }
#endif
}
