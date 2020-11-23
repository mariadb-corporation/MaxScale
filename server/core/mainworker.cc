/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/mainworker.hh>

#include <signal.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/routingworker.hh>

#include "internal/modules.hh"
#include "internal/admin.hh"
#include "internal/monitormanager.hh"
#include "internal/listener.hh"

namespace
{

static struct ThisUnit
{
    maxscale::MainWorker* pMain;
    int64_t               clock_ticks;
} this_unit;

thread_local struct ThisThread
{
    maxscale::MainWorker* pMain;
} this_thread;
}

namespace maxscale
{

MainWorker::MainWorker(mxb::WatchdogNotifier* pNotifier)
    : mxb::WatchedWorker(pNotifier)
{
    mxb_assert(!this_unit.pMain);

    this_unit.pMain = this;
    this_thread.pMain = this;

    delayed_call(100, &MainWorker::inc_ticks);

    const auto& config = mxs::Config::get();

    if (config.rebalance_period.get() != std::chrono::milliseconds(0))
    {
        order_balancing_dc();
    }
}

MainWorker::~MainWorker()
{
    mxb_assert(this_unit.pMain);

    this_thread.pMain = nullptr;
    this_unit.pMain = nullptr;
}

// static
bool MainWorker::created()
{
    return this_unit.pMain ? true : false;
}

// static
MainWorker* MainWorker::get()
{
    mxb_assert(this_unit.pMain);

    return this_unit.pMain;
}

void MainWorker::add_task(const std::string& name, TASKFN func, void* pData, int frequency)
{
    execute([=]() {
                mxb_assert_message(m_tasks_by_name.find(name) == m_tasks_by_name.end(), "%s", name.c_str());

                Task task(name.c_str(), func, pData, frequency);

                auto p = m_tasks_by_name.insert(std::make_pair(name, task));
                Task& inserted_task = (*p.first).second;

                inserted_task.id = delayed_call(frequency * 1000,
                                                &MainWorker::call_task,
                                                this,
                                                &inserted_task);
            },
            EXECUTE_AUTO);
}

void MainWorker::remove_task(const std::string& name)
{

    call([this, name]() {
             auto it = m_tasks_by_name.find(name);
             mxb_assert(it != m_tasks_by_name.end());

             if (it != m_tasks_by_name.end())
             {
                 MXB_AT_DEBUG(bool cancelled = ) cancel_delayed_call(it->second.id);
                 mxb_assert(cancelled);

                 m_tasks_by_name.erase(it);
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

// static
int64_t MainWorker::ticks()
{
    return mxb::atomic::load(&this_unit.clock_ticks, mxb::atomic::RELAXED);
}

// static
bool MainWorker::is_main_worker()
{
    return this_thread.pMain != nullptr;
}

void MainWorker::start_rebalancing()
{
    mxb_assert(is_main_worker());

    const auto& config = mxs::Config::get();

    std::chrono::milliseconds period = config.rebalance_period.get();

    if (m_rebalancing_dc == 0 && period != std::chrono::milliseconds(0))
    {
        // If the rebalancing delayed call is not currently active and the
        // period is now != 0, then we order a delayed call.
        order_balancing_dc();
    }
    else if (m_rebalancing_dc != 0 && period == std::chrono::milliseconds(0))
    {
        // If the rebalancing delayed call is currently active and the
        // period is now 0, then we cancel the call, effectively shutting
        // down the rebalancing.
        cancel_delayed_call(m_rebalancing_dc);
        m_rebalancing_dc = 0;
    }
}

bool MainWorker::pre_run()
{
    bool rval = false;

    if (modules_thread_init() && qc_thread_init(QC_INIT_SELF))
    {
        rval = true;
        qc_use_local_cache(false);
    }

    return rval;
}

void MainWorker::post_run()
{
    qc_thread_end(QC_INIT_SELF);
    modules_thread_finish();
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

            if (it != m_tasks_by_name.end())    // Not found, if task function removes task.
            {
                m_tasks_by_name.erase(it);
            }
        }
    }

    return call_again;
}

// static
bool MainWorker::inc_ticks(Worker::Call::action_t action)
{
    if (action == Worker::Call::EXECUTE)
    {
        mxb::atomic::add(&this_unit.clock_ticks, 1, mxb::atomic::RELAXED);
    }

    return true;
}

bool MainWorker::balance_workers(BalancingApproach approach, int threshold)
{
    bool rebalanced = false;

    const auto& config = mxs::Config::get();

    if (threshold == -1)
    {
        threshold = config.rebalance_threshold.get();
    }

    RoutingWorker::collect_worker_load(config.rebalance_window.get());

    std::chrono::milliseconds period = config.rebalance_period.get();

    mxb::TimePoint now = epoll_tick_now();

    if (approach == BALANCE_UNCONDITIONALLY
        || now - m_last_rebalancing >= period)
    {
        rebalanced = RoutingWorker::balance_workers(threshold);
        m_last_rebalancing = now;
    }

    return rebalanced;
}

bool MainWorker::balance_workers_dc(Worker::Call::action_t action)
{
    bool rv = true;

    if (action == Worker::Call::EXECUTE)
    {
        balance_workers(BALANCE_ACCORDING_TO_PERIOD);
    }

    return rv;
}

void MainWorker::order_balancing_dc()
{
    mxb_assert(m_rebalancing_dc == 0);

    m_rebalancing_dc = delayed_call(1000, &MainWorker::balance_workers_dc, this);
}

// static
void MainWorker::start_shutdown()
{
    auto func = []() {
            // Stop all monitors and listeners to prevent any state changes during shutdown and to prevent the
            // creation of new sessions. Stop the REST API to prevent any conflicting changes from being
            // executed while we're shutting down.
            MonitorManager::stop_all_monitors();
            mxs_admin_shutdown();
            Listener::stop_all();

            // The RoutingWorkers proceed with the shutdown on their own. Once all sessions have closed, they
            // will exit the event loop.
            mxs::RoutingWorker::start_shutdown();

            // Wait until RoutingWorkers have stopped before proceeding with MainWorker shudown
            auto self = MainWorker::get();
            self->delayed_call(100, &MainWorker::wait_for_shutdown, self);
        };

    MainWorker::get()->execute(func, EXECUTE_QUEUED);
}

bool MainWorker::wait_for_shutdown(Call::action_t action)
{
    if (action == Call::EXECUTE)
    {
        if (RoutingWorker::shutdown_complete())
        {
            shutdown();
        }
    }

    return true;
}
}

extern "C"
{

void hktask_add(const char* zName, TASKFN func, void* pData, int frequency)
{
    mxs::MainWorker::get()->add_task(zName, func, pData, frequency);
}

void hktask_remove(const char* zName)
{
    mxs::MainWorker::get()->remove_task(zName);
}

json_t* hk_tasks_json(const char* zHost)
{
    return mxs::MainWorker::get()->tasks_to_json(zHost);
}

int64_t mxs_clock()
{
    return mxs::MainWorker::ticks();
}
}
