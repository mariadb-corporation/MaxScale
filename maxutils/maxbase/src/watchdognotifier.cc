/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/watchdognotifier.hh>
#include <algorithm>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

using std::lock_guard;
using std::mutex;
using std::unique_lock;

namespace
{

static struct ThisUnit
{
    maxbase::WatchdogNotifier* pNotifier = nullptr;
} this_unit;
}

namespace maxbase
{

/**
 * @class WatchdogNotifier::Dependent::Ticker
 *
 * This class is capable making an instance of Dependent appear
 * to be ticking, even if it actually is not while performing some
 * lengthy synchronous operation.
 */
class WatchdogNotifier::Dependent::Ticker
{
    Ticker(const Ticker&) = delete;
    Ticker& operator=(const Ticker&) = delete;

public:
    Ticker(Dependent* pOwner)
        : m_owner(*pOwner)
        , m_nClients(0)
        , m_terminate(false)
    {
        m_thread = std::thread(&Ticker::run, this);
    }

    ~Ticker()
    {
        mxb_assert(m_nClients == 0);
        m_terminate.store(true, std::memory_order_release);
        m_cond.notify_one();
        m_thread.join();
    }

    void start()
    {
        Guard guard(m_lock);
        int clients = ++m_nClients;
        guard.unlock();

        if (clients == 1)
        {
            m_cond.notify_one();
        }
    }

    void stop()
    {
        Guard guard(m_lock);
        int clients = --m_nClients;
        guard.unlock();

        mxb_assert(clients >= 0);

        if (clients == 0)
        {
            m_cond.notify_one();
        }
    }

private:
    // Run in thread created in constructor.
    void run()
    {
        auto interval = m_owner.notifier().interval();

        while (!m_terminate.load(std::memory_order_acquire))
        {
            Guard guard(m_lock);

            if (m_nClients > 0)
            {
                m_owner.mark_ticking_if_currently_not();
            }

            m_cond.wait_for(guard, interval);
        }
    }

    using Guard = std::unique_lock<std::mutex>;

    Dependent&             m_owner;
    int                    m_nClients;
    std::atomic<bool>      m_terminate;
    std::thread            m_thread;
    std::mutex             m_lock;
    mxb::ConditionVariable m_cond;
};

WatchdogNotifier::Dependent::Dependent(WatchdogNotifier* pNotifier)
    : m_notifier(*pNotifier)
    , m_ticking(true)
{
    if (m_notifier.interval().count() != 0)
    {
        m_pTicker = new Ticker(this);
    }

    m_notifier.add(this);
}

WatchdogNotifier::Dependent::~Dependent()
{
    m_notifier.remove(this);

    delete m_pTicker;
}

void WatchdogNotifier::Dependent::start_watchdog_workaround()
{
    if (m_pTicker)
    {
        m_pTicker->start();
    }
}

void WatchdogNotifier::Dependent::stop_watchdog_workaround()
{
    if (m_pTicker)
    {
        m_pTicker->stop();
    }
}

WatchdogNotifier::WatchdogNotifier(uint64_t usecs)
// The internal timeout is 1/2 of the systemd configured interval. Note that
// the argument is in usecs, but the interval is stored in secs.
    : m_interval(usecs / 2000000)
{
    mxb_assert(this_unit.pNotifier == nullptr);
    this_unit.pNotifier = this;

    if (m_interval.count() != 0)
    {
        MXB_NOTICE("The systemd watchdog is Enabled. Internal timeout = %s\n",
                   to_string(m_interval).c_str());
    }
}

WatchdogNotifier::~WatchdogNotifier()
{
    mxb_assert(m_dependents.size() == 0);
    mxb_assert(this_unit.pNotifier == this);
    this_unit.pNotifier = nullptr;
}

void WatchdogNotifier::start()
{
    mxb_assert(m_thread.get_id() == std::thread::id());

    if (m_interval.count() != 0)
    {
        m_thread = std::thread(&WatchdogNotifier::run, this);
    }
}

void WatchdogNotifier::stop()
{
    if (m_interval.count() != 0)
    {
        mxb_assert(m_thread.get_id() != std::thread::id());

        m_running.store(false, std::memory_order_relaxed);
        m_cond.notify_one();
        m_thread.join();
    }
}

void WatchdogNotifier::add(Dependent* pDependent)
{
    lock_guard<mutex> guard(m_dependents_lock);

    mxb_assert(m_dependents.find(pDependent) == m_dependents.end());

    m_dependents.insert(pDependent);
}

void WatchdogNotifier::remove(Dependent* pDependent)
{
    lock_guard<mutex> guard(m_dependents_lock);

    auto it = m_dependents.find(pDependent);
    mxb_assert(it != m_dependents.end());

    m_dependents.erase(it);
}

void WatchdogNotifier::run()
{
    mxb_assert(m_interval.count() != 0);

    while (m_running.load(std::memory_order_relaxed))
    {
        notify_systemd_watchdog();
        unique_lock<mutex> guard(m_cond_lock);
        m_cond.wait_for(guard, m_interval);
    }
}

void WatchdogNotifier::notify_systemd_watchdog()
{
    unique_lock<mutex> guard(m_dependents_lock);

    bool all_ticking = std::all_of(
        m_dependents.begin(), m_dependents.end(), [](Dependent* pDependent) {
            return pDependent->is_ticking();
        });

    if (all_ticking)
    {
        std::for_each(
            m_dependents.begin(), m_dependents.end(), [](Dependent* pDependent) {
                pDependent->mark_not_ticking();
            });
    }

    guard.unlock();

    if (all_ticking)
    {
#ifdef HAVE_SYSTEMD
        MXB_DEBUG("systemd watchdog keep-alive ping: sd_notify(false, \"WATCHDOG=1\")");
        sd_notify(false, "WATCHDOG=1");
#endif
    }
}
}
