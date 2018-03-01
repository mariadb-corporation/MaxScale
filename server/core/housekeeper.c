/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/housekeeper.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/semaphore.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>
#include <maxscale/query_classifier.h>

/**
 * @file housekeeper.c  Provide a mechanism to run periodic tasks
 *
 * The housekeeper provides a mechanism to allow for tasks, function
 * calls basically, to be run on a tiem basis. A task may be run
 * repeatedly, with a given frequency (in seconds), or may be a one
 * shot task that will only be run once after a specified number of
 * seconds.
 *
 * The housekeeper also maintains a global variable, hkheartbeat, that
 * is incremented every 100ms.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 29/08/14     Mark Riddoch    Initial implementation
 * 22/10/14     Mark Riddoch    Addition of one-shot tasks
 *
 * @endverbatim
 */

/**
 * List of all tasks that need to be run
 */
static HKTASK *tasks = NULL;
/**
 * Spinlock to protect the tasks list
 */
static SPINLOCK tasklock = SPINLOCK_INIT;

static bool do_shutdown = 0;

long hkheartbeat = 0; /*< One heartbeat is 100 milliseconds */
static THREAD hk_thr_handle;

static void hkthread(void *);

struct hkinit_result
{
    sem_t sem;
    bool ok;
};

bool
hkinit()
{
    struct hkinit_result res;
    sem_init(&res.sem, 0, 0);
    res.ok = false;

    if (thread_start(&hk_thr_handle, hkthread, &res) != NULL)
    {
        sem_wait(&res.sem);
    }
    else
    {
        MXS_ALERT("Failed to start housekeeper thread.");
    }

    sem_destroy(&res.sem);
    return res.ok;
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
 * @param name          The unique name for this housekeeper task
 * @param taskfn        The function to call for the task
 * @param data          Data to pass to the task function
 * @param frequency     How often to run the task, expressed in seconds
 * @return              Return the time in seconds when the task will be first run
 *                      if the task was added, otherwise 0
 */
int
hktask_add(const char *name, void (*taskfn)(void *), void *data, int frequency)
{
    HKTASK *task, *ptr;

    if ((task = (HKTASK *)MXS_MALLOC(sizeof(HKTASK))) == NULL)
    {
        return 0;
    }
    if ((task->name = MXS_STRDUP(name)) == NULL)
    {
        MXS_FREE(task);
        return 0;
    }
    task->task = taskfn;
    task->data = data;
    task->frequency = frequency;
    task->type = HK_REPEATED;
    task->nextdue = time(0) + frequency;
    task->next = NULL;
    spinlock_acquire(&tasklock);
    ptr = tasks;
    while (ptr && ptr->next)
    {
        if (strcmp(ptr->name, name) == 0)
        {
            spinlock_release(&tasklock);
            MXS_FREE(task->name);
            MXS_FREE(task);
            return 0;
        }
        ptr = ptr->next;
    }
    if (ptr)
    {
        if (strcmp(ptr->name, name) == 0)
        {
            spinlock_release(&tasklock);
            MXS_FREE(task->name);
            MXS_FREE(task);
            return 0;
        }
        ptr->next = task;
    }
    else
    {
        tasks = task;
    }
    spinlock_release(&tasklock);

    return task->nextdue;
}

/**
 * Add a one-shot task to the housekeeper task list
 *
 * Task names must be unique.
 *
 * @param name          The unique name for this housekeeper task
 * @param taskfn        The function to call for the task
 * @param data          Data to pass to the task function
 * @param when          How many second until the task is executed
 * @return              Return the time in seconds when the task will be first run
 *                      if the task was added, otherwise 0
 *
 */
int
hktask_oneshot(const char *name, void (*taskfn)(void *), void *data, int when)
{
    HKTASK *task, *ptr;

    if ((task = (HKTASK *)MXS_MALLOC(sizeof(HKTASK))) == NULL)
    {
        return 0;
    }
    if ((task->name = MXS_STRDUP(name)) == NULL)
    {
        MXS_FREE(task);
        return 0;
    }
    task->task = taskfn;
    task->data = data;
    task->frequency = 0;
    task->type = HK_ONESHOT;
    task->nextdue = time(0) + when;
    task->next = NULL;
    spinlock_acquire(&tasklock);
    ptr = tasks;
    while (ptr && ptr->next)
    {
        ptr = ptr->next;
    }
    if (ptr)
    {
        ptr->next = task;
    }
    else
    {
        tasks = task;
    }
    spinlock_release(&tasklock);

    return task->nextdue;
}


/**
 * Remove a named task from the housekeepers task list
 *
 * @param name          The task name to remove
 * @return              Returns 0 if the task could not be removed
 */
int
hktask_remove(const char *name)
{
    HKTASK *ptr, *lptr = NULL;

    spinlock_acquire(&tasklock);
    ptr = tasks;
    while (ptr && strcmp(ptr->name, name) != 0)
    {
        lptr = ptr;
        ptr = ptr->next;
    }
    if (ptr && lptr)
    {
        lptr->next = ptr->next;
    }
    else if (ptr)
    {
        tasks = ptr->next;
    }
    spinlock_release(&tasklock);

    if (ptr)
    {
        MXS_FREE(ptr->name);
        MXS_FREE(ptr);
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
 * @param       data            Unused, here to satisfy the thread system
 */
void
hkthread(void *data)
{
    HKTASK *ptr;
    time_t now;
    void (*taskfn)(void *);
    void *taskdata;
    int i;

    struct hkinit_result* res = (struct hkinit_result*)data;
    res->ok = qc_thread_init(QC_INIT_BOTH);

    if (!res->ok)
    {
        MXS_ERROR("Could not initialize housekeeper thread.");
    }

    sem_post(&res->sem);

    while (!do_shutdown)
    {
        for (i = 0; i < 10; i++)
        {
            thread_millisleep(100);
            hkheartbeat++;
        }
        now = time(0);
        spinlock_acquire(&tasklock);
        ptr = tasks;
        while (!do_shutdown && ptr)
        {
            if (ptr->nextdue <= now)
            {
                ptr->nextdue = now + ptr->frequency;
                taskfn = ptr->task;
                taskdata = ptr->data;
                // We need to copy type and name, in case hktask_remove is called from
                // the callback. Otherwise we will access freed data.
                HKTASK_TYPE type = ptr->type;
                char name[strlen(ptr->name) + 1];
                strcpy(name, ptr->name);
                spinlock_release(&tasklock);
                (*taskfn)(taskdata);
                if (type == HK_ONESHOT)
                {
                    hktask_remove(name);
                }
                spinlock_acquire(&tasklock);
                ptr = tasks;
            }
            else
            {
                ptr = ptr->next;
            }
        }
        spinlock_release(&tasklock);
    }

    qc_thread_end(QC_INIT_BOTH);
    MXS_NOTICE("Housekeeper shutting down.");
}

void
hkshutdown()
{
    do_shutdown = true;
    atomic_synchronize();
}

void hkfinish()
{
    ss_dassert(do_shutdown);

    MXS_NOTICE("Waiting for housekeeper to shut down.");
    thread_wait(hk_thr_handle);
    do_shutdown = false;
    MXS_NOTICE("Housekeeper has shut down.");
}

/**
 * Show the tasks that are scheduled for the house keeper
 *
 * @param pdcb          The DCB to send to output
 */
void
hkshow_tasks(DCB *pdcb)
{
    HKTASK *ptr;
    struct tm tm;
    char buf[40];

    dcb_printf(pdcb, "%-25s | Type     | Frequency | Next Due\n", "Name");
    dcb_printf(pdcb, "--------------------------+----------+-----------+-------------------------\n");
    spinlock_acquire(&tasklock);
    ptr = tasks;
    while (ptr)
    {
        localtime_r(&ptr->nextdue, &tm);
        asctime_r(&tm, buf);
        dcb_printf(pdcb, "%-25s | %-8s | %-9d | %s",
                   ptr->name,
                   ptr->type == HK_REPEATED ? "Repeated" : "One-Shot",
                   ptr->frequency,
                   buf);
        ptr = ptr->next;
    }
    spinlock_release(&tasklock);
}
