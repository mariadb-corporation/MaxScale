/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxscale/cppdefs.hh>

#include <maxscale/housekeeper.h>

#include <stdlib.h>
#include <string.h>
#include <string>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/clock.h>
#include <maxscale/config.h>
#include <maxscale/semaphore.h>
#include <maxscale/spinlock.h>
#include <maxscale/thread.h>
#include <maxscale/query_classifier.h>
#include <maxscale/json_api.h>

/**
 * @file housekeeper.cc  Provide a mechanism to run periodic tasks
 *
 * The housekeeper provides a mechanism to allow for tasks, function
 * calls basically, to be run on a time basis. A task may be run
 * repeatedly, with a given frequency (in seconds), or may be a one
 * shot task that will only be run once after a specified number of
 * seconds.
 *
 * The housekeeper also maintains a global variable that
 * is incremented every 100ms and can be read with the mxs_clock() function.
 */

// TODO: Move these into a separate file
static int64_t mxs_clock_ticks = 0; /*< One clock tick is 100 milliseconds */

int64_t mxs_clock()
{
    return atomic_load_int64(&mxs_clock_ticks);
}

enum hktask_type
{
    HK_REPEATED = 1,
    HK_ONESHOT
};

/**
 * The housekeeper task list
 */
struct HKTASK
{
    char* name;               /*< A simple task name */
    void (*task)(void *data); /*< The task to call */
    void *data;               /*< Data to pass the task */
    int frequency;            /*< How often to call the tasks (seconds) */
    time_t nextdue;           /*< When the task should be next run */
    enum hktask_type type;    /*< The task type */
    struct HKTASK* next;
};

class Housekeeper
{
public:
    Housekeeper();
    ~Housekeeper();

    static bool init();
    void stop();
    void run();

private:
    THREAD   m_thread;
    uint32_t m_running;
};

// Helper struct used to initialize the housekeeper
struct hkinit_result
{
    sem_t sem;
    bool ok;
};

/**
 * List of all tasks that need to be run
 */
static HKTASK* tasks = NULL;
/**
 * Spinlock to protect the tasks list
 */
static SPINLOCK tasklock = SPINLOCK_INIT;

static void hkthread(void *);

// The Housekeeper instance
static Housekeeper* hk = NULL;

Housekeeper::Housekeeper():
    m_running(1)
{
}

Housekeeper::~Housekeeper()
{
    thread_wait(m_thread);
}

bool Housekeeper::init()
{
    struct hkinit_result res;
    sem_init(&res.sem, 0, 0);
    res.ok = false;
    hk = new (std::nothrow) Housekeeper;

    if (hk && thread_start(&hk->m_thread, hkthread, &res, 0) != NULL)
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

void Housekeeper::run()
{
    while (atomic_load_uint32(&m_running))
    {
        for (int i = 0; i < 10; i++)
        {
            thread_millisleep(100);
            atomic_add_int64(&mxs_clock_ticks, 1);
        }
        time_t now = time(0);
        spinlock_acquire(&tasklock);
        HKTASK* ptr = tasks;

        while (atomic_load_uint32(&m_running) && ptr)
        {
            if (ptr->nextdue <= now)
            {
                ptr->nextdue = now + ptr->frequency;
                // We need to copy type and name, in case hktask_remove is called from
                // the callback. Otherwise we will access freed data.
                enum hktask_type type = ptr->type;
                std::string name = ptr->name;

                spinlock_release(&tasklock);
                ptr->task(ptr->data);
                if (type == HK_ONESHOT)
                {
                    hktask_remove(name.c_str());
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
}

void Housekeeper::stop()
{
    atomic_store_uint32(&m_running, 0);
}

int hktask_add(const char *name, void (*taskfn)(void *), void *data, int frequency)
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

int hktask_oneshot(const char *name, void (*taskfn)(void *), void *data, int when)
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

int hktask_remove(const char *name)
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
void hkthread(void *data)
{
    struct hkinit_result* res = (struct hkinit_result*)data;
    res->ok = qc_thread_init(QC_INIT_BOTH);

    if (!res->ok)
    {
        MXS_ERROR("Could not initialize housekeeper thread.");
    }

    sem_post(&res->sem);

    hk->run();

    qc_thread_end(QC_INIT_BOTH);
    MXS_NOTICE("Housekeeper shutting down.");
}

bool hkinit()
{
    return Housekeeper::init();
}

void hkshutdown()
{
    hk->stop();
}

void hkfinish()
{
    ss_dassert(hk);
    MXS_NOTICE("Waiting for housekeeper to shut down.");

    delete hk;

    MXS_NOTICE("Housekeeper has shut down.");
}

void hkshow_tasks(DCB *pdcb)
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

json_t* hk_tasks_json(const char* host)
{
    json_t* arr = json_array();

    spinlock_acquire(&tasklock);

    for (HKTASK* ptr = tasks; ptr; ptr = ptr->next)
    {
        struct tm tm;
        char buf[40];
        localtime_r(&ptr->nextdue, &tm);
        asctime_r(&tm, buf);
        char* nl = strchr(buf, '\n');
        ss_dassert(nl);
        *nl = '\0';

        const char* task_type = ptr->type == HK_REPEATED ? "Repeated" : "One-Shot";

        json_t* obj = json_object();

        json_object_set_new(obj, CN_ID, json_string(ptr->name));
        json_object_set_new(obj, CN_TYPE, json_string("tasks"));

        json_t* attr = json_object();
        json_object_set_new(attr, "task_type", json_string(task_type));
        json_object_set_new(attr, "frequency", json_integer(ptr->frequency));
        json_object_set_new(attr, "next_execution", json_string(buf));

        json_object_set_new(obj, CN_ATTRIBUTES, attr);
        json_array_append_new(arr, obj);
    }

    spinlock_release(&tasklock);

    return mxs_json_resource(host, MXS_JSON_API_TASKS, arr);
}
