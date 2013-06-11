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

/*
 * spinlock.c  -  Spinlock operations for the SkySQL Gateway
 *
 * Revision History
 *
 * Date		Who		Description
 * 10/06/13	Mark Riddoch	Initial implementation
 *
 */

#include <spinlock.h>
#include <atomic.h>

/*
 * Initialise a spinlock.
 *
 * @param lock The spinlock to initialise.
 */
void
spinlock_init(SPINLOCK *lock)
{
	lock->lock = 0;
#ifdef DEBUG
	lock->spins = 0;
	lock->acquired = 0;
#endif
}

/*
 * Acquire a spinlock.
 *
 * @param lock The spinlock to acquire
 */
void
spinlock_acquire(SPINLOCK *lock)
{
	while (atomic_add(&(lock->lock), 1) != 0)
	{
		atomic_add(&(lock->lock), -1);
#ifdef DEBUG
		atomic_add(&(lock->spins), 1);
#endif
	}
#ifdef DEBUG
	lock->acquired++;
	lock->owner = THREAD_SHELF();
#endif
}

/*
 * Acquire a spinlock if it is not already locked.
 *
 * @param lock The spinlock to acquire
 * @return True ifthe spinlock was acquired, otherwise false
 */
int
spinlock_acquire_nowait(SPINLOCK *lock)
{
	if (atomic_add(&(lock->lock), 1) != 0)
	{
		atomic_add(&(lock->lock), -1);
		return FALSE;
	}
#ifdef DEBUG
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
	atomic_add(&(lock->lock), -1);
}
