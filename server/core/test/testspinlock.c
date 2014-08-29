/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright SkySQL Ab 2014
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 18/08-2014	Mark Riddoch		Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spinlock.h>
#include <thread.h>


/**
 * test1	spinlock_acquire_nowait tests
 *
 * Test that spinlock_acquire_nowait returns false if the spinlock
 * is already taken.
 *
 * Test that spinlock_acquire_nowait returns true if the spinlock
 * is not taken.
 *
 * Test that spinlock_acquire_nowait does hold the spinlock.
 */
static int
test1()
{
SPINLOCK	lck;

	spinlock_init(&lck);
	spinlock_acquire(&lck);
	if (spinlock_acquire_nowait(&lck))
	{
		fprintf(stderr, "spinlock_acquire_nowait: test 1 failed.\n");
		return 1;
	}
	spinlock_release(&lck);
	if (!spinlock_acquire_nowait(&lck))
	{
		fprintf(stderr, "spinlock_acquire_nowait: test 2 failed.\n");
		return 1;
	}
	if (spinlock_acquire_nowait(&lck))
	{
		fprintf(stderr, "spinlock_acquire_nowait: test 3 failed.\n");
		return 1;
	}
	spinlock_release(&lck);

	return 0;
}

static int acquire_time;

static void
test2_helper(void *data)
{
SPINLOCK   *lck = (SPINLOCK *)data;
unsigned long	t1 = time(0);

	spinlock_acquire(lck);
	acquire_time = time(0) - t1;
	spinlock_release(lck);
	return;
}

/**
 * Check that spinlock correctly blocks another thread whilst the spinlock
 * is held.
 *
 * Take out a lock.
 * Start a second thread to take the same lock
 * sleep for 10 seconds
 * release lock
 * verify that second thread took at least 8 seconds to obtain the lock
 */
static int
test2()
{
SPINLOCK	lck;
void		*handle;

	acquire_time = 0;
	spinlock_init(&lck);
	spinlock_acquire(&lck);
	handle = thread_start(test2_helper, (void *)&lck);
	sleep(10);
	spinlock_release(&lck);
	thread_wait(handle);

	if (acquire_time < 8)
	{
		fprintf(stderr, "spinlock: test 1 failed.\n");
		return 1;
	}
	return 0;
}

main(int argc, char **argv)
{
int	result = 0;

	result += test1();
	result += test2();

	exit(result);
}

