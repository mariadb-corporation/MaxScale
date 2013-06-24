#ifndef _SPINLOCK_H
#define _SPINLOCK_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file spinlock.h
 *
 * Spinlock implementation for ther gateway.
 *
 * Spinlocks are cheap locks that can be used to protect short code blocks, they are
 * generally wasteful as any blocked threads will spin, consuming CPU cycles, waiting
 * for the lock to be released. However they are useful in that they do not involve
 * system calls and are light weight when the expected wait time for a lock is low.
 */
#include <thread.h>

typedef struct spinlock {
	volatile int	lock;
#if DEBUG
	int		spins;
	int		acquired;
	THREAD		owner;
#endif
} SPINLOCK;

#ifndef TRUE
#define TRUE	(1 == 1)
#endif
#ifndef FALSE
#define FALSE	(1 == 0)
#endif

#if DEBUG
#define SPINLOCK_INIT { 0, 0, 0, NULL }
#else
#define SPINLOCK_INIT { 0 }
#endif

extern void	spinlock_init(SPINLOCK *lock);
extern void	spinlock_acquire(SPINLOCK *lock);
extern int	spinlock_acquire_nowait(SPINLOCK *lock);
extern void	spinlock_release(SPINLOCK *lock);
#endif
