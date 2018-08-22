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

/**
 * @file spinlock.h
 *
 * Spinlock implementation for MaxScale.
 *
 * Spinlocks are cheap locks that can be used to protect short code blocks, they are
 * generally wasteful as any blocked threads will spin, consuming CPU cycles, waiting
 * for the lock to be released. However they are useful in that they do not involve
 * system calls and are light weight when the expected wait time for a lock is low.
 */

#include <maxscale/cdefs.h>
#include <stdbool.h>

MXS_BEGIN_DECLS

#define SPINLOCK_PROFILE 0

/**
 * The spinlock structure.
 *
 * In normal builds the structure merely contains a lock value which
 * is 0 if the spinlock is not taken and greater than zero if it is held.
 *
 * In builds with the SPINLOCK_PROFILE option set this structure also holds
 * a number of profile related fields that count the number of spins, number
 * of waiting threads and the number of times the lock has been acquired.
 */
typedef struct spinlock
{
    int lock;              /*< Is the lock held? */
#if SPINLOCK_PROFILE
    uint64_t spins;        /*< Number of spins on this lock */
    uint64_t maxspins;     /*< Max no of spins to acquire lock */
    uint64_t acquired;     /*< No. of times lock was acquired */
    uint64_t waiting;      /*< No. of threads acquiring this lock */
    uint64_t max_waiting;  /*< Max no of threads waiting for lock */
    uint64_t contended;    /*< No. of times acquire was contended */
    THREAD owner;          /*< Last owner of this lock */
#endif
} SPINLOCK;

#if SPINLOCK_PROFILE
#define SPINLOCK_INIT { 0, 0, 0, 0, 0, 0, 0, 0 }
#else
#define SPINLOCK_INIT { 0 }
#endif

/**
 * Debugging macro for testing the state of a spinlock.
 *
 * @attention ONLY to be used in debugging context.
 */
#define SPINLOCK_IS_LOCKED(l) ((l)->lock != 0 ? true : false)

/**
 * Initialise a spinlock.
 *
 * @param lock The spinlock to initialise.
 */
extern void spinlock_init(SPINLOCK *lock);

/**
 * Acquire a spinlock.
 *
 * @param lock The spinlock to acquire
 */
extern void spinlock_acquire(const SPINLOCK *lock);

/**
 * Acquire a spinlock if it is not already locked.
 *
 * @param lock The spinlock to acquire
 * @return True if the spinlock was acquired, otherwise false
 */
extern bool spinlock_acquire_nowait(const SPINLOCK *lock);

/*
 * Release a spinlock.
 *
 * @param lock The spinlock to release
 */
extern void spinlock_release(const SPINLOCK *lock);

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
extern void spinlock_stats(const SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl);

MXS_END_DECLS
