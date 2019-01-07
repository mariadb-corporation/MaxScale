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

namespace
{

static struct ThisUnit
{
    maxscale::MainWorker* pThis;
} this_unit;

}

namespace maxscale
{

MainWorker::MainWorker()
{
    mxb_assert(!this_unit.pThis);

    this_unit.pThis = this;
}

MainWorker::~MainWorker()
{
    mxb_assert(this_unit.pThis);

    this_unit.pThis = nullptr;
}

//static
MainWorker& MainWorker::get()
{
    mxb_assert(this_unit.pThis);

    return *this_unit.pThis;
}

void MainWorker::add_task(const char* zName, TASKFN func, void* pData, int frequency)
{
    call([=]() {
            mxb_assert(m_tasks_by_name.find(zName) == m_tasks_by_name.end());

            Task task(zName, func, pData);

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

        if (!call_again)
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

}
