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
#pragma once

#include <maxbase/ccdefs.hh>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <maxbase/assert.h>
#include <maxbase/atomic.hh>
#include <maxbase/condition_variable.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/stopwatch.hh>

namespace maxbase
{

/**
 * @class WatchdogNotifier
 *
 * An instance of this class performs systemd watchdog notifications at
 * regular intervals, provided all dependents are deemed to be alive.
 * Note that there at any moment may be at most one instance alive of
 * WatchdogNotifier.
 */
class WatchdogNotifier
{
public:
    class Workaround;

    /**
     * @class Dependent
     *
     * The liveness of an instance of a class derived from
     * WatchdogNotifier::Dependent will be considered when deciding whether
     * the process itself is alive.
     */
    class Dependent
    {
    public:
        virtual ~Dependent();

        const WatchdogNotifier& notifier() const
        {
            return m_notifier;
        }

        /**
         * Starts the watchdog workaround that will ensure that the systemd
         * watchdog is notified, even if the dependent would perform a lengthy
         * synchronous operation.
         *
         * It is permissible to call this function multiple times, but
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
         * @return True, if the dependent is alive and kicking.
         */
        bool is_ticking() const
        {
            return m_ticking.load(std::memory_order_acquire);
        }

    protected:
        Dependent(WatchdogNotifier* pNotifier);

        /**
         * To be regularly called by a derived class.
         */
        void mark_ticking_if_currently_not()
        {
            if (m_ticking.load(std::memory_order_acquire) == false)
            {
                m_ticking.store(true, std::memory_order_release);
            }
        }

    private:
        friend class WatchdogNotifier;

        void mark_not_ticking()
        {
            m_ticking.store(false, std::memory_order_release);
        }

    private:
        class Ticker;
        friend Ticker;

        WatchdogNotifier& m_notifier;
        std::atomic<bool> m_ticking;
        Ticker*           m_pTicker {nullptr};  /*< Watchdog ticker, if systemd enabled. */
    };

    /**
     * @class Workaround
     *
     * RAII-class using which the systemd watchdog notification can be
     * handled during synchronous worker activity that causes the epoll
     * event handling to be stalled.
     *
     * The constructor turns on the workaround and the destructor
     * turns it off.
     */
    class Workaround
    {
    public:
        Workaround(const Workaround&) = delete;
        Workaround& operator=(const Workaround&) = delete;

        /**
         * Turns on the watchdog workaround for a specific dependent.
         *
         * @param pDependent  The dependent for which the systemd notification
         *                    should be arranged.
         */
        Workaround(Dependent* pDependent)
            : m_dependent(*pDependent)
        {
            mxb_assert(pDependent);
            m_dependent.start_watchdog_workaround();
        }

        /**
         * Turns off the watchdog workaround.
         */
        ~Workaround()
        {
            m_dependent.stop_watchdog_workaround();
        }

    private:
        Dependent& m_dependent;
    };

    WatchdogNotifier(const WatchdogNotifier&) = delete;
    WatchdogNotifier& operator=(const WatchdogNotifier&) = delete;

    /**
     * Constructor
     *
     * @param usec  The systemd notification interval in micro seconds.
     *              If 0, then there will be no notifications.
     */
    WatchdogNotifier(uint64_t usec);
    ~WatchdogNotifier();

    const std::chrono::seconds& interval() const
    {
        return m_interval;
    }

    /**
     * Start the watchdog notifier. Multiple calls without intervening
     * call to @c stop() is not permissible.
     */
    void start();

    /**
     * Stop the watchdog notifier.
     */
    void stop();

private:
    friend Dependent;

    void add(Dependent* pDependent);
    void remove(Dependent* pDependent);

private:
    void run();
    void notify_systemd_watchdog();

    std::thread                    m_thread;
    std::atomic<bool>              m_running {true};
    std::mutex                     m_cond_lock;
    mxb::ConditionVariable         m_cond;
    std::chrono::seconds           m_interval;  /*< Duration between notifications, if any. */
    std::unordered_set<Dependent*> m_dependents;
    std::mutex                     m_dependents_lock;
};
}
