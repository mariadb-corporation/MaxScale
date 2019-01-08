/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxscale/mainworker.hh>
#include <signal.h>
#include <maxscale/config.hh>

namespace
{

static struct ThisUnit
{
    maxscale::MainWorker* pCurrent_main;
    int64_t               clock_ticks;
} this_unit;

}

namespace maxscale
{

MainWorker::MainWorker()
{
    mxb_assert(!this_unit.pCurrent_main);

    this_unit.pCurrent_main = this;

    delayed_call(100, &MainWorker::inc_ticks);
}

MainWorker::~MainWorker()
{
    mxb_assert(this_unit.pCurrent_main);

    this_unit.pCurrent_main = nullptr;
}

//static
MainWorker& MainWorker::get()
{
    mxb_assert(this_unit.pCurrent_main);

    return *this_unit.pCurrent_main;
}

void MainWorker::add_task(const char* zName, TASKFN func, void* pData, int frequency)
{
    call([=]() {
            mxb_assert(m_tasks_by_name.find(zName) == m_tasks_by_name.end());

            Task task(zName, func, pData, frequency);

            auto p = m_tasks_by_name.insert(std::make_pair(std::string(zName), task));
            Task& inserted_task = (*p.first).second;

            inserted_task.id = delayed_call(frequency * 1000,
                                            &MainWorker::call_task,
                                            this,
                                            &inserted_task);
        },
        EXECUTE_AUTO);
}

void MainWorker::remove_task(const char* zName)
{
    call([this, zName]() {
            auto it = m_tasks_by_name.find(zName);
            mxb_assert(it != m_tasks_by_name.end());

            if (it != m_tasks_by_name.end())
            {
                MXB_AT_DEBUG(bool cancelled =) cancel_delayed_call(it->second.id);
                mxb_assert(cancelled);

                m_tasks_by_name.erase(it);
            }
        },
        EXECUTE_AUTO);
}

void MainWorker::show_tasks(DCB* pDcb) const
{
    // TODO: Make call() const.
    MainWorker* pThis = const_cast<MainWorker*>(this);
    pThis->call([this, pDcb] () {
            dcb_printf(pDcb, "%-25s | Frequency | Next Due\n", "Name");
            dcb_printf(pDcb, "--------------------------+-----------+-------------------------\n");

            for (auto it = m_tasks_by_name.begin(); it != m_tasks_by_name.end(); ++it)
            {
                const Task& task = it->second;

                struct tm tm;
                char buf[40];
                localtime_r(&task.nextdue, &tm);
                asctime_r(&tm, buf);
                dcb_printf(pDcb, "%-25s | %-9d | %s", task.name.c_str(), task.frequency, buf);
            }
        },
        EXECUTE_AUTO);
}

json_t* MainWorker::tasks_to_json(const char* zHost) const
{
    json_t* pResult = json_array();

    // TODO: Make call() const.
    MainWorker* pThis = const_cast<MainWorker*>(this);
    pThis->call([this, zHost, pResult]() {
            for (auto it = m_tasks_by_name.begin(); it != m_tasks_by_name.end(); ++it)
            {
                const Task& task = it->second;

                struct tm tm;
                char buf[40];
                localtime_r(&task.nextdue, &tm);
                asctime_r(&tm, buf);
                char* nl = strchr(buf, '\n');
                mxb_assert(nl);
                *nl = '\0';

                json_t* pObject = json_object();

                json_object_set_new(pObject, CN_ID, json_string(task.name.c_str()));
                json_object_set_new(pObject, CN_TYPE, json_string("tasks"));

                json_t* pAttrs = json_object();
                json_object_set_new(pAttrs, "frequency", json_integer(task.frequency));
                json_object_set_new(pAttrs, "next_execution", json_string(buf));

                json_object_set_new(pObject, CN_ATTRIBUTES, pAttrs);
                json_array_append_new(pResult, pObject);
            }
        },
        EXECUTE_AUTO);

    return pResult;
}

//static
int64_t MainWorker::ticks()
{
    return mxb::atomic::load(&this_unit.clock_ticks, mxb::atomic::RELAXED);
}

bool MainWorker::pre_run()
{
    return true;
}

void MainWorker::post_run()
{
}

void MainWorker::epoll_tick()
{
}

bool MainWorker::call_task(Worker::Call::action_t action, MainWorker::Task* pTask)
{
    bool call_again = false;

    if (action == Worker::Call::EXECUTE)
    {
        mxb_assert(m_tasks_by_name.find(pTask->name) != m_tasks_by_name.end());

        call_again = pTask->func(pTask->pData);

        if (call_again)
        {
            pTask->nextdue = time(0) + pTask->frequency;
        }
        else
        {
            auto it = m_tasks_by_name.find(pTask->name);

            if (it != m_tasks_by_name.end()) // Not found, if task function removes task.
            {
                m_tasks_by_name.erase(it);
            }
        }
    }

    return call_again;
}

//static
bool MainWorker::inc_ticks(Worker::Call::action_t action)
{
    if (action == Worker::Call::EXECUTE)
    {
        mxb::atomic::add(&this_unit.clock_ticks, 1, mxb::atomic::RELAXED);
    }

    return true;
}

}
