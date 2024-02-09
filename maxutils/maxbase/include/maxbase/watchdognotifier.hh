/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
#include <maxbase/assert.hh>
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

        virtual const char* name() const = 0;

        const WatchdogNotifier& notifier() const
        {
            return m_notifier;
        }

        /**
         * @return True, if the dependent is alive and kicking.
         */
        bool is_ticking() const
        {
            return m_state.load(std::memory_order_relaxed) & (TICKING | BLOCKED);
        }

    protected:
        Dependent(WatchdogNotifier* pNotifier);

        /**
         * To be regularly called by a derived class.
         */
        void mark_ticking_if_currently_not()
        {
            set_state(TICKING);
        }

    private:
        friend class WatchdogNotifier;

        static constexpr uint8_t TICKING = 0x1;     // Working normally
        static constexpr uint8_t BLOCKED = 0x2;     // Doing a blocking operation

        void mark_not_ticking()
        {
            clear_state(TICKING);
        }

        void set_state(uint8_t state)
        {
            m_state.fetch_or(state, std::memory_order_relaxed);
        }

        void clear_state(uint8_t state)
        {
            m_state.fetch_and(~state, std::memory_order_relaxed);
        }

    private:
        WatchdogNotifier&    m_notifier;
        std::atomic<uint8_t> m_state {TICKING};
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
            m_dependent.set_state(Dependent::BLOCKED);
        }

        /**
         * Turns off the watchdog workaround.
         */
        ~Workaround()
        {
            m_dependent.clear_state(Dependent::BLOCKED);
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
