/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <set>
#include <unordered_set>
#include <maxbase/stopwatch.hh>
#include <maxbase/watchedworker.hh>
#include <maxscale/indexedstorage.hh>

class Server;
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
    static bool is_current();

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
    void update_rebalancing();

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

    const char* name() const override
    {
        return "MainWorker";
    }

    /**
     * Call a function in a signal-safe manner
     *
     * This function can be safely called from a signal handler since it only writes the address of the
     * callback function into an internal pipe that is added to epoll. This makes it possible to move the
     * execution away from the signal handler where it is very hard to do pretty much anything in a safe
     * manner.
     *
     * @param func The function that will be called by the MainWorker
     *
     * @return True if the execution of the function was queued successfully
     */
    bool execute_signal_safe(void (* func)(void));

private:
    bool pre_run() override;
    void post_run() override;

    struct SignalHandler final : public mxb::Pollable
    {
        uint32_t handle_poll_events(Worker* worker, uint32_t events, Context context) override;
        int      poll_fd() const override;
    };

    static bool inc_ticks(Callable::Action action);

    bool balance_workers_dc();
    void order_balancing_dc();

    void read_signal_from_pipe();

    void check_dependencies_dc();

    // Waits until all RoutingWorkers have stopped and then stops the MainWorker
    bool wait_for_shutdown();

    Worker::Callable      m_callable;
    IndexedStorage        m_storage;
    mxb::Worker::DCId     m_rebalancing_dc {0};
    mxb::TimePoint        m_last_rebalancing;
    std::set<std::string> m_tunables;   // Tunable parameters
    int                   m_signal_pipe[2] {-1, -1};
    SignalHandler         m_signal_handler;
};
}
