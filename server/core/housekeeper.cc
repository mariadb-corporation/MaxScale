/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include <maxscale/cppdefs.hh>

#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>

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

static void hkthread(void*);

// TODO: Move these into a separate file
static int64_t mxs_clock_ticks = 0; /*< One clock tick is 100 milliseconds */

int64_t mxs_clock()
{
    return atomic_load_int64(&mxs_clock_ticks);
}

namespace
{

// A task to perform
struct Task
{
    Task(std::string name, TASKFN func, void* data, int frequency):
        name(name),
        func(func),
        data(data),
        frequency(frequency),
        nextdue(time(0) + frequency)
    {
    }

    struct NameMatch
    {
        NameMatch(std::string name):
            m_name(name) {}

        bool operator()(const Task& task)
        {
            return task.name == m_name;
        }

        std::string m_name;
    };


    std::string name;      /*< Task name */
    TASKFN      func;      /*< The function to call */
    void*       data;      /*< Data to pass to the function */
    int         frequency; /*< How often to call the tasks, in seconds */
    time_t      nextdue;   /*< When the task should be next run */
};

class Housekeeper
{
public:
    Housekeeper();

    static bool init();
    static bool start();
    void stop();
    void run();
    void add(const Task& task);
    void remove(std::string name);

    void print_tasks(DCB* pDcb);
    json_t* tasks_json(const char* host);

private:
    THREAD            m_thread;
    uint32_t          m_running;
    std::list<Task>   m_tasks;
    mxs::SpinLock     m_lock;

    bool is_running() const
    {
        return atomic_load_uint32(&m_running);
    }
};

// Helper struct used to initialize the housekeeper
struct hkinit_result
{
    sem_t sem;
    bool ok;
};

// The Housekeeper instance
static Housekeeper* hk = NULL;

Housekeeper::Housekeeper():
    m_running(1)
{
}

bool Housekeeper::init()
{
    hk = new (std::nothrow) Housekeeper;
    return hk != nullptr;
}

bool Housekeeper::start()
{
    struct hkinit_result res;
    sem_init(&res.sem, 0, 0);
    res.ok = false;

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
    while (is_running())
    {
        for (int i = 0; i < 10; i++)
        {
            thread_millisleep(100);
            atomic_add_int64(&mxs_clock_ticks, 1);
        }

        mxs::SpinLockGuard guard(m_lock);
        time_t now = time(0);
        auto it = m_tasks.begin();

        while (it != m_tasks.end() && is_running())
        {
            if (it->nextdue <= now)
            {
                it->nextdue = now + it->frequency;

                if (!it->func(it->data))
                {
                    it = m_tasks.erase(it);
                    continue;
                }
            }

            it++;
        }
    }
}

void Housekeeper::stop()
{
    atomic_store_uint32(&m_running, 0);
    thread_wait(m_thread);
}

void Housekeeper::add(const Task& task)
{
    mxs::SpinLockGuard guard(m_lock);
    m_tasks.push_back(task);
}

void Housekeeper::remove(std::string name)
{
    mxs::SpinLockGuard guard(m_lock);
    m_tasks.remove_if(Task::NameMatch(name));
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
        dcb_printf(pDcb, "%-25s | %-9d | %s", ptr->name.c_str(), ptr->frequency, buf);
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

        json_t* obj = json_object();

        json_object_set_new(obj, CN_ID, json_string(ptr->name.c_str()));
        json_object_set_new(obj, CN_TYPE, json_string("tasks"));

        json_t* attr = json_object();
        json_object_set_new(attr, "frequency", json_integer(ptr->frequency));
        json_object_set_new(attr, "next_execution", json_string(buf));

        json_object_set_new(obj, CN_ATTRIBUTES, attr);
        json_array_append_new(arr, obj);
    }

    return mxs_json_resource(host, MXS_JSON_API_TASKS, arr);
}

}

void hktask_add(const char *name, TASKFN func, void *data, int frequency)
{
    ss_dassert(hk);
    Task task(name, func, data, frequency);
    hk->add(task);
}

void hktask_remove(const char *name)
{
    ss_dassert(hk);
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

bool hkstart()
{
    return Housekeeper::start();
}

void hkfinish()
{
    if (hk)
    {
        MXS_NOTICE("Waiting for housekeeper to shut down.");
        hk->stop();
        delete hk;
        hk = NULL;
        MXS_NOTICE("Housekeeper has shut down.");
    }
}

void hkshow_tasks(DCB *pDcb)
{
    ss_dassert(hk);
    hk->print_tasks(pDcb);
}

json_t* hk_tasks_json(const char* host)
{
    ss_dassert(hk);
    return hk->tasks_json(host);
}
