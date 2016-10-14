#pragma once
#ifndef _MAXSCALE_SPINLOCK_H
#define _MAXSCALE_SPINLOCK_H
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
#include <maxscale/debug.h>

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
    int lock;         /*< Is the lock held? */
#if SPINLOCK_PROFILE
    int spins;        /*< Number of spins on this lock */
    int maxspins;     /*< Max no of spins to acquire lock */
    int acquired;     /*< No. of times lock was acquired */
    int waiting;      /*< No. of threads acquiring this lock */
    int max_waiting;  /*< Max no of threads waiting for lock */
    int contended;    /*< No. of times acquire was contended */
    THREAD owner;     /*< Last owner of this lock */
#endif
} SPINLOCK;

#ifndef TRUE
#define TRUE    true
#endif
#ifndef FALSE
#define FALSE   false
#endif

#if SPINLOCK_PROFILE
#define SPINLOCK_INIT { 0, 0, 0, 0, 0, 0, 0, 0 }
#else
#define SPINLOCK_INIT { 0 }
#endif

#define SPINLOCK_IS_LOCKED(l) ((l)->lock != 0 ? true : false)

extern void spinlock_init(SPINLOCK *lock);
extern void spinlock_acquire(const SPINLOCK *lock);
extern int spinlock_acquire_nowait(const SPINLOCK *lock);
extern void spinlock_release(const SPINLOCK *lock);
extern void spinlock_stats(const SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl);

MXS_END_DECLS

#endif
