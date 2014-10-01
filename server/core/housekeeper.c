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
 * Copyright SkySQL Ab 2014
 */
#include <stdlib.h>
#include <string.h>
#include <housekeeper.h>
#include <thread.h>
#include <spinlock.h>

/**
 * @file housekeeper.c  Provide a mechanism to run periodic tasks
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 29/08/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * List of all tasks that need to be run
 */
static HKTASK	*tasks = NULL;
/**
 * Spinlock to protect the tasks list
 */
static SPINLOCK	tasklock = SPINLOCK_INIT;

static int	do_shutdown = 0;
unsigned long	hkheartbeat = 0;

static	void	hkthread(void *);

/**
 * Initialise the housekeeper thread
 */
void
hkinit()
{
	thread_start(hkthread, NULL);
}

/**
 * Add a new task to the housekeepers lists of tasks that should be
 * run periodically.
 *
 * The task will be first run frequency seconds after this call is
 * made and will the be executed repeatedly every frequency seconds
 * until the task is removed.
 *
 * Task names must be unique.
 *
 * @param name		The unique name for this housekeeper task
 * @param taskfn	The function to call for the task
 * @param data		Data to pass to the task function
 * @param frequency	How often to run the task, expressed in seconds
 * @return		Return the tiem in seconds when the task will be first run if the task was added, otherwise 0
 */
int
hktask_add(char *name, void (*taskfn)(void *), void *data, int frequency)
{
HKTASK	*task, *ptr;

	if ((task = (HKTASK *)malloc(sizeof(HKTASK))) == NULL)
	{
		return 0;
	}
	if ((task->name = strdup(name)) == NULL)
	{
		free(task);
		return 0;
	}
	task->task = taskfn;
	task->data = data;
	task->frequency = frequency;
	task->nextdue = time(0) + frequency;
	task->next = NULL;
	spinlock_acquire(&tasklock);
	ptr = tasks;
	while (ptr && ptr->next)
	{
		if (strcmp(ptr->name, name) == 0)
		{
			spinlock_release(&tasklock);
			free(task->name);
			free(task);
			return 0;
		}
		ptr = ptr->next;
	}
	if (ptr)
		ptr->next = task;
	else
		tasks = task;
	spinlock_release(&tasklock);

	return task->nextdue;
}

/**
 * Remove a named task from the housekeepers task list
 *
 * @param name		The task name to remove
 * @return		Returns 0 if the task could not be removed
 */
int
hktask_remove(char *name)
{
HKTASK	*ptr, *lptr = NULL;

	spinlock_acquire(&tasklock);
	ptr = tasks;
	while (ptr && strcmp(ptr->name, name) != 0)
	{
		lptr = ptr;
		ptr = ptr->next;
	}
	if (ptr && lptr)
		lptr->next = ptr->next;
	else if (ptr)
		tasks = ptr->next;
	spinlock_release(&tasklock);

	if (ptr)
	{
		free(ptr->name);
		free(ptr);
		return 1;
	}
	else
	{
		return 0;
	}
}


/**
 * The housekeeper thread implementation.
 *
 * This function is responsible for executing the housekeeper tasks.
 *
 * The implementation of the callng of the task functions is such that
 * the tasks are called without the tasklock spinlock being held. This
 * allows manipulation of the housekeeper task list during execution of
 * one of the tasks. The resutl is that upon completion of a task the
 * search for tasks to run must restart from the start of the queue.
 * It is vital that the task->nextdue tiem is updated before the task
 * is run.
 *
 * @param	data		Unused, here to satisfy the thread system
 */
void
hkthread(void *data)
{
HKTASK	*ptr;
time_t	now;
void	(*taskfn)(void *);
void	*taskdata;
int	i;

	for (;;)
	{
		for (i = 0; i < 10; i++)
		{
			if (do_shutdown)
				return;
			thread_millisleep(100);
			hkheartbeat++;
		}
		now = time(0);
		spinlock_acquire(&tasklock);
		ptr = tasks;
		while (ptr)
		{
			if (ptr->nextdue <= now)
			{
				ptr->nextdue = now + ptr->frequency;
				taskfn = ptr->task;
				taskdata = ptr->data;
				spinlock_release(&tasklock);
				(*taskfn)(taskdata);
				spinlock_acquire(&tasklock);
				ptr = tasks;
			}
			else
				ptr = ptr->next;
		}
		spinlock_release(&tasklock);
	}
}

/**
 * Called to shutdown the housekeeper
 *
 */
void
hkshutdown()
{
	do_shutdown = 1;
}
