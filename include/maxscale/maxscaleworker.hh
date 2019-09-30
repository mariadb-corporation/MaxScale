/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <atomic>
#include <maxbase/worker.hh>

namespace maxscale
{

class MainWorker;

/**
 * Base-class for all MaxScale workers, with the exception of MainWorker.
 * This class provides all functionality needed for being able to notify
 * the systemd watchdog in a timely manner.
 */
class MaxScaleWorker : public mxb::Worker
{
public:
    ~MaxScaleWorker();

    /**
     * Starts the watchdog workaround that will ensure that the systemd
     * watchdog is notified, even if the worker would perform a lengthy
     * synchronous operation.
     *
     * It is persmissible to call this function multiple times, but
     * each call should be matched with a call to
     * @c stop_watchdog_workaround().
     *
     * @note This should be considered as the last resort, the right
     *       approach is to replace the synchronous operation with
     *       an asynchronous one.
     */
    void start_watchdog_workaround();

    /**
     * Stops the watchdog workaround.
     */
    void stop_watchdog_workaround();

    /**
     * @class WatchdogWorkaround
     *
     * RAII-class using which the systemd watchdog notification can be
     * handled during synchronous worker activity that causes the epoll
     * event handling to be stalled.
     *
     * The constructor turns on the workaround and the destructor
     * turns it off.
     */
    class WatchdogWorkaround
    {
    public:
        WatchdogWorkaround(const WatchdogWorkaround&) = delete;
        WatchdogWorkaround& operator=(const WatchdogWorkaround&) = delete;

        /**
         * Turns on the watchdog workaround for a specific worker.
         *
         * @param pWorker  The worker for which the systemd notification
         *                 should be arranged. Need not be the calling worker.
         */
        WatchdogWorkaround(MaxScaleWorker* pWorker)
            : m_worker(*pWorker)
        {
            mxb_assert(pWorker);
            m_worker.start_watchdog_workaround();
        }

        /**
         * Turns off the watchdog workaround.
         */
        ~WatchdogWorkaround()
        {
            m_worker.stop_watchdog_workaround();
        }

    private:
        MaxScaleWorker& m_worker;
    };


private:
    friend class MainWorker;

    bool is_alive() const
    {
        return m_alive.load(std::memory_order_relaxed);
    }

    void mark_alive()
    {
        m_alive.store(true, std::memory_order_relaxed);
    }

    void mark_dead()
    {
        m_alive.store(false, std::memory_order_relaxed);
    }

    void resurrect_if_dead()
    {
        if (m_alive.load(std::memory_order_relaxed) == false)
        {
            m_alive.store(true, std::memory_order_relaxed);
        }
    }

protected:
    MaxScaleWorker(MainWorker* pMain);

    /**
     * Called once per epoll loop from epoll_tick().
     */
    virtual void epoll_tock() = 0;

    MainWorker& m_main;

private:
    class WatchdogNotifier;
    friend WatchdogNotifier;

    void epoll_tick() override final;

    std::atomic<bool> m_alive;
    WatchdogNotifier* m_pWatchdog_notifier { nullptr }; /*< Watchdog notifier, if systemd enabled. */
};

}
