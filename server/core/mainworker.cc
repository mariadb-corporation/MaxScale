/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/mainworker.hh>

#include <signal.h>
#include <vector>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include <maxscale/cachingparser.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/listener.hh>
#include <maxscale/routingworker.hh>

#include "internal/admin.hh"
#include "internal/configmanager.hh"
#include "internal/http_sql.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/service.hh"

namespace
{

static struct ThisUnit
{
    maxscale::MainWorker* pMain = nullptr;
    int64_t               clock_ticks;
} this_unit;

thread_local struct ThisThread
{
    maxscale::MainWorker* pMain = nullptr;
} this_thread;
}

namespace maxscale
{

MainWorker::MainWorker(mxb::WatchdogNotifier* pNotifier)
    : mxb::WatchedWorker(pNotifier)
    , m_callable(this)
{
    mxb_assert(!this_unit.pMain);

    this_unit.pMain = this;
    // Actually, pMain should be set in pre_run() and cleared in post_run(),
    // but when set here and cleared in the destructor, we will appear to be
    // running in the MainWorker also when MainWorker::run() has returned, which
    // is conceptually ok as the main worker runs in the main thread that stays
    // around until the program ends.
    this_thread.pMain = this;
}

MainWorker::~MainWorker()
{
    m_callable.cancel_dcalls();

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
    return this_unit.pMain;
}

// static
int64_t MainWorker::ticks()
{
    return mxb::atomic::load(&this_unit.clock_ticks, mxb::atomic::RELAXED);
}

// static
bool MainWorker::is_current()
{
    return this_thread.pMain != nullptr;
}

void MainWorker::update_rebalancing()
{
    mxb_assert(is_current());

    // MainWorker must be running
    if (get_current() == nullptr)
    {
        return;
    }

    const auto& config = mxs::Config::get();

    std::chrono::milliseconds period = config.rebalance_period.get();

    if (m_rebalancing_dc == 0 && period != 0ms)
    {
        // If the rebalancing delayed call is not currently active and the
        // period is now != 0, then we order a delayed call.
        order_balancing_dc();
    }
    else if (m_rebalancing_dc != 0 && period == 0ms)
    {
        // If the rebalancing delayed call is currently active and the
        // period is now 0, then we cancel the call, effectively shutting
        // down the rebalancing.
        m_callable.cancel_dcall(m_rebalancing_dc);
        m_rebalancing_dc = 0;
    }
}

bool MainWorker::pre_run()
{
    bool rval = false;

    if (modules_thread_init())
    {
        mxs::CachingParser::thread_init();
        // No point in wasting memory for the parser cache in the main thread.
        mxs::CachingParser::set_thread_cache_enabled(false);

        m_callable.dcall(100ms, &MainWorker::inc_ticks);
        update_rebalancing();

        rval = true;
    }

    if (rval)
    {
        const auto& auto_tune = Config::get().auto_tune;

        if (!auto_tune.empty())
        {
            if (auto_tune.size() == 1 && auto_tune.front() == CN_ALL)
            {
                const auto& server_dependencies = Service::specification()->server_dependencies();

                for (const auto* pDependency : server_dependencies)
                {
                    m_tunables.insert(pDependency->parameter().name());
                }
            }
            else
            {
                for (const auto& parameter : auto_tune)
                {
                    m_tunables.insert(parameter);
                }
            }

            MXB_NOTICE("The following parameters will be auto tuned: %s",
                       mxb::join(m_tunables, ", ", "'").c_str());

            m_callable.dcall(5s, [this]() {
                    check_dependencies_dc();
                    return true;
                });
        }
        else
        {
            MXB_INFO("No 'auto_tune' parameters specified, no auto tuning will be performed.");
        }
    }

    return rval;
}

void MainWorker::post_run()
{
    // Clearing the storage right after the main loop returns guarantees that both the MainWorker and the
    // RoutingWorkers are alive when stored data is destroyed. Without this, the destruction of filters is
    // delayed until the MainWorker is destroyed which is something that must be avoided. All objects in
    // MaxScale should be destroyed before the workers are destroyed.
    m_storage.clear();

    mxs::CachingParser::thread_finish();
    modules_thread_finish();
}

// static
bool MainWorker::inc_ticks(Worker::Callable::Action action)
{
    if (action == Callable::EXECUTE)
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

bool MainWorker::balance_workers_dc()
{
    balance_workers(BALANCE_ACCORDING_TO_PERIOD);

    return true;
}

void MainWorker::order_balancing_dc()
{
    mxb_assert(m_rebalancing_dc == 0);

    m_rebalancing_dc = m_callable.dcall(1000ms, &MainWorker::balance_workers_dc, this);
}

void MainWorker::check_dependencies_dc()
{
    auto services = Service::get_all();

    for (auto* pService : services)
    {
        pService->check_server_dependencies(m_tunables);
    }
}

// static
void MainWorker::start_shutdown()
{
    auto func = []() {
            // Stop all monitors and listeners to prevent any state changes during shutdown and to prevent the
            // creation of new sessions. Stop the REST API to prevent any conflicting changes from being
            // executed while we're shutting down.
            MonitorManager::stop_all_monitors();
            if (mxs::Config::get().admin_enabled)
            {
                mxs_admin_shutdown();
                // Stop cleanup-thread only after rest-api is shut down, so that no queries are active.
                HttpSql::finish();
            }

            auto* pConfig_manager = mxs::ConfigManager::get();
            if (pConfig_manager)
            {
                pConfig_manager->stop_sync();
            }

            Listener::stop_all();

            // If there was a problem with the config, the routing workers were never started
            // in which case they need not be shutdown.
            if (mxs::RoutingWorker::is_running())
            {
                // The RoutingWorkers proceed with the shutdown on their own. Once all sessions have closed, they
                // will exit the event loop.
                mxs::RoutingWorker::start_shutdown();
            }

            // Wait until RoutingWorkers have stopped before proceeding with MainWorker shutdown
            auto self = MainWorker::get();
            self->m_callable.dcall(100ms, &MainWorker::wait_for_shutdown, self);
        };

    MainWorker::get()->execute(func, EXECUTE_QUEUED);
}

bool MainWorker::wait_for_shutdown()
{
    bool again = true;

    if (RoutingWorker::shutdown_complete())
    {
        shutdown();
        again = false;
    }

    return again;
}
}

int64_t mxs_clock()
{
    return mxs::MainWorker::ticks();
}
