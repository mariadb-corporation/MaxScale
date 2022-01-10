/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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

#include "internal/admin.hh"
#include "internal/http_sql.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/listener.hh"
#include "internal/configmanager.hh"

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
    return this_unit.pMain;
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

    delayed_call(100, &MainWorker::inc_ticks);

    const auto& config = mxs::Config::get();

    if (config.rebalance_period.get() != std::chrono::milliseconds(0))
    {
        order_balancing_dc();
    }

    if (modules_thread_init() && qc_thread_init(QC_INIT_SELF))
    {
        rval = true;
        qc_use_local_cache(false);
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

    qc_thread_end(QC_INIT_SELF);
    modules_thread_finish();
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
            if (mxs::Config::get().admin_enabled)
            {
                mxs_admin_shutdown();
                // Stop cleanup-thread only after rest-api is shut down, so that no queries are active.
                HttpSql::stop_cleanup();
            }
            mxs::ConfigManager::get()->stop_sync();
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

int64_t mxs_clock()
{
    return mxs::MainWorker::ticks();
}
