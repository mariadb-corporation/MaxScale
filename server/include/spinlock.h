#ifndef _SPINLOCK_H
#define _SPINLOCK_H
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
 * @file spinlock.h
 *
 * Spinlock implementation for MaxScale.
 *
 * Spinlocks are cheap locks that can be used to protect short code blocks, they are
 * generally wasteful as any blocked threads will spin, consuming CPU cycles, waiting
 * for the lock to be released. However they are useful in that they do not involve
 * system calls and are light weight when the expected wait time for a lock is low.
 */
#include <thread.h>
#include <stdbool.h>

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
extern void spinlock_acquire(SPINLOCK *lock);
extern int spinlock_acquire_nowait(SPINLOCK *lock);
extern void spinlock_release(SPINLOCK *lock);
extern void spinlock_stats(SPINLOCK *lock, void (*reporter)(void *, char *, int), void *hdl);

#endif
