/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <unordered_set>
#include <maxbase/stopwatch.hh>
#include <maxbase/watchedworker.hh>
#include <maxscale/indexedstorage.hh>

namespace maxscale
{

class MainWorker : public mxb::WatchedWorker
{
    MainWorker(const MainWorker&) = delete;
    MainWorker& operator=(const MainWorker&) = delete;

public:
    /**
     * Construct the main worker.
     *
     * @param pNotifier The watchdog notifier.
     *
     * @note There can be exactly one instance of @c MainWorker.
     */
    MainWorker(mxb::WatchdogNotifier* pNotifier);

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
    static MainWorker* get();

    static int64_t ticks();

    /**
     * @return True, if the calling thread is the main worker.
     */
    static bool is_main_worker();

    /**
     * @return The indexed storage of this worker.
     */
    IndexedStorage& storage()
    {
        return m_storage;
    }

    const IndexedStorage& storage() const
    {
        return m_storage;
    }

    /**
     * Starts the rebalancing.
     *
     * @note Must *only* be called from the main worker thread.
     */
    void start_rebalancing();

    enum BalancingApproach
    {
        BALANCE_UNCONDITIONALLY,
        BALANCE_ACCORDING_TO_PERIOD
    };

    /**
     * Balance worker load.
     *
     * @param approach   Unconditionally or according to 'rebalance_period'.
     * @param threshold  The rebalance threshold. If -1, then the value of
     *                   'rebalance_threshold' will be used.
     *
     * @return True, if balancing actually was performed.
     */
    bool balance_workers(BalancingApproach approach, int threshold = -1);

    /**
     * Starts the shutdown process
     */
    static void start_shutdown();

private:
    bool pre_run() override;
    void post_run() override;

    static bool inc_ticks(Worker::Call::action_t action);

    bool balance_workers_dc(Worker::Call::action_t action);
    void order_balancing_dc();

    // Waits until all RoutingWorkers have stopped and then stops the MainWorker
    bool wait_for_shutdown(Worker::Call::action_t action);

    IndexedStorage m_storage;
    uint32_t       m_rebalancing_dc {0};
    mxb::TimePoint m_last_rebalancing;
};
}
