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

#include <spinlock.h>
#include <atomic.h>
#include <time.h>

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
spinlock_acquire(SPINLOCK *lock)
{
#if SPINLOCK_PROFILE
    int spins = 0;

    atomic_add(&(lock->waiting), 1);
#endif

#ifdef __GNUC__
    while (__sync_lock_test_and_set(&(lock->lock), 1))
        while (lock->lock) {
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
    lock->owner = THREAD_SHELF();
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
spinlock_acquire_nowait(SPINLOCK *lock)
{
#ifdef __GNUC__
    if (__sync_lock_test_and_set(&(lock->lock), 1)) return FALSE;
#else
    if (atomic_add(&(lock->lock), 1) != 0)
    {
        atomic_add(&(lock->lock), -1);
        return FALSE;
    }
#endif
#if SPINLOCK_PROFILE
    lock->acquired++;
    lock->owner = THREAD_SHELF();
#endif
    return TRUE;
}

/*
 * Release a spinlock.
 *
 * @param lock The spinlock to release
 */
void
spinlock_release(SPINLOCK *lock)
{
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
spinlock_stats(SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl)
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
