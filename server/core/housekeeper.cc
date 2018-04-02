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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/clock.h>
#include <maxscale/config.h>
#include <maxscale/housekeeper.h>
#include <maxscale/json_api.h>
#include <maxscale/query_classifier.h>
#include <maxscale/semaphore.h>
#include <maxscale/spinlock.h>
#include <maxscale/spinlock.hh>
#include <maxscale/thread.h>

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

namespace
{

typedef void (*TASKFN)(void *data);

// A task to perform
struct Task
{
    Task(std::string name, TASKFN func, void* data, int frequency, hktask_type type):
        name(name),
        func(func),
        data(data),
        frequency(frequency),
        nextdue(time(0) + frequency),
        type(type)
    {
    }

    std::string name;      /*< Task name */
    TASKFN      func;      /*< The function to call */
    void*       data;      /*< Data to pass to the function */
    int         frequency; /*< How often to call the tasks, in seconds */
    time_t      nextdue;   /*< When the task should be next run */
    hktask_type type;      /*< The task type */
};

class Housekeeper
{
public:
    Housekeeper();
    ~Housekeeper();

    static bool init();
    void stop();
    void run();
    void add(std::string name, TASKFN func, void* data, int frequency, hktask_type type);
    void remove(std::string name);

    void print_tasks(DCB* pDcb);
    json_t* tasks_json(const char* host);

private:
    THREAD            m_thread;
    uint32_t          m_running;
    std::vector<Task> m_tasks;
    mxs::SpinLock     m_lock;
};

// Helper struct used to initialize the housekeeper
struct hkinit_result
{
    sem_t sem;
    bool ok;
};

// The Housekeeper instance
static Housekeeper* hk = NULL;

static void hkthread(void*);

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
        m_lock.acquire();

        for (auto it = m_tasks.begin(); it != m_tasks.end() && atomic_load_uint32(&m_running); it++)
        {
            if (it->nextdue <= now)
            {
                it->nextdue = now + it->frequency;
                // We need to copy type and name, in case hktask_remove is called from
                // the callback. Otherwise we will access freed data.
                enum hktask_type type = it->type;
                std::string name = it->name;

                m_lock.release();
                it->func(it->data);
                if (type == HK_ONESHOT)
                {
                    remove(name);
                }
                m_lock.acquire();
            }
        }
        m_lock.release();
    }
}

void Housekeeper::stop()
{
    atomic_store_uint32(&m_running, 0);
}

void Housekeeper::add(std::string name, TASKFN func, void* data, int frequency, hktask_type type)
{
    mxs::SpinLockGuard guard(m_lock);
    m_tasks.push_back(Task(name, func, data, frequency, type));
}

void Housekeeper::remove(std::string name)
{
    mxs::SpinLockGuard guard(m_lock);
    auto it = m_tasks.begin();

    while (it != m_tasks.end())
    {
        if (it->name == name)
        {
            it = m_tasks.erase(it);
            continue;
        }
        it++;
    }
}

void Housekeeper::print_tasks(DCB* pDcb)
{
    mxs::SpinLockGuard guard(m_lock);
    dcb_printf(pDcb, "%-25s | Type     | Frequency | Next Due\n", "Name");
    dcb_printf(pDcb, "--------------------------+----------+-----------+-------------------------\n");

    for (auto ptr = m_tasks.begin(); ptr != m_tasks.end(); ptr++)
    {
        struct tm tm;
        char buf[40];
        localtime_r(&ptr->nextdue, &tm);
        asctime_r(&tm, buf);
        dcb_printf(pDcb, "%-25s | %-8s | %-9d | %s", ptr->name.c_str(),
                   ptr->type == HK_REPEATED ? "Repeated" : "One-Shot",
                   ptr->frequency, buf);
    }
}

json_t* Housekeeper::tasks_json(const char* host)
{
    json_t* arr = json_array();

    mxs::SpinLockGuard guard(m_lock);

    for (auto ptr = m_tasks.begin(); ptr != m_tasks.end(); ptr++)
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

        json_object_set_new(obj, CN_ID, json_string(ptr->name.c_str()));
        json_object_set_new(obj, CN_TYPE, json_string("tasks"));

        json_t* attr = json_object();
        json_object_set_new(attr, "task_type", json_string(task_type));
        json_object_set_new(attr, "frequency", json_integer(ptr->frequency));
        json_object_set_new(attr, "next_execution", json_string(buf));

        json_object_set_new(obj, CN_ATTRIBUTES, attr);
        json_array_append_new(arr, obj);
    }

    return mxs_json_resource(host, MXS_JSON_API_TASKS, arr);
}

}

void hktask_add(const char *name, void (*taskfn)(void *), void *data, int frequency)
{
    hk->add(name, taskfn, data, frequency, HK_REPEATED);
}

void hktask_oneshot(const char *name, void (*taskfn)(void *), void *data, int when)
{
    hk->add(name, taskfn, data, when, HK_ONESHOT);
}

void hktask_remove(const char *name)
{
    hk->remove(name);
}

void hkthread(void *data)
{
    struct hkinit_result* res = (struct hkinit_result*)data;
    res->ok = qc_thread_init(QC_INIT_BOTH);

    if (!res->ok)
    {
        MXS_ERROR("Could not initialize housekeeper thread.");
    }

    sem_post(&res->sem);

    if (res->ok)
    {
        hk->run();
    }

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
    MXS_NOTICE("Waiting for housekeeper to shut down.");
    delete hk;
    hk = NULL;
    MXS_NOTICE("Housekeeper has shut down.");
}

void hkshow_tasks(DCB *pDcb)
{
    hk->print_tasks(pDcb);
}

json_t* hk_tasks_json(const char* host)
{
    return hk->tasks_json(host);
}
