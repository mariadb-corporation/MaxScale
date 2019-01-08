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
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/worker.hh>
#include <maxscale/housekeeper.h>

namespace maxscale
{

class MainWorker : public mxb::Worker
{
    MainWorker(const MainWorker&) = delete;
    MainWorker& operator=(const MainWorker&) = delete;

public:
    /**
     * Construct the main worker.
     *
     * @note There can be exactly one instance of @c MainWorker.
     */
    MainWorker();

    ~MainWorker();

    /**
     * Does the main worker exist. It is only at startup and shutdown that this
     * function may return false. When MaxScale is running normally, it will
     * always return true.
     *
     * @return True, if the main worker has been created, false otherwise.
     */
    static bool created();

    /**
     * Returns the main worker.
     *
     * @return The main worker.
     */
    static MainWorker& get();

    void add_task(const char* zName, TASKFN func, void* pData, int frequency);
    void remove_task(const char* zName);

    void show_tasks(DCB* pDcb) const;
    json_t* tasks_to_json(const char* zhost) const;

    static int64_t ticks();

private:
    bool pre_run() override;
    void post_run() override;
    void epoll_tick() override;

    struct Task
    {
    public:
        Task(const char* zName, TASKFN func, void* pData, int frequency)
            : name(zName)
            , func(func)
            , pData(pData)
            , frequency(frequency)
            , nextdue(time(0) + frequency)
            , id(0)
        {
        };

        std::string name;
        TASKFN      func;
        void*       pData;
        int         frequency;
        time_t      nextdue;
        uint32_t    id;
    };

    bool call_task(Worker::Call::action_t action, Task* pTask);
    static bool inc_ticks(Worker::Call::action_t action);

    std::map<std::string, Task> m_tasks_by_name;
};

}
