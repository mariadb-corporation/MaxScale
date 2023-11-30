/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/watchdognotifier.hh>
#include <maxbase/threadpool.hh>
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
WatchdogNotifier::Dependent::Dependent(WatchdogNotifier* pNotifier)
    : m_notifier(*pNotifier)
{
    m_notifier.add(this);
}

WatchdogNotifier::Dependent::~Dependent()
{
    m_notifier.remove(this);
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
        mxb::set_thread_name(m_thread, "WD-Notifier");
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
    bool all_ticking = true;

    for (Dependent* pDependent : m_dependents)
    {
        if (pDependent->is_ticking())
        {
            pDependent->mark_not_ticking();
        }
        else
        {
            all_ticking = false;
            MXB_WARNING("Thread '%s' has not reported back in %ld seconds.",
                        pDependent->name(), m_interval.count());
        }
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
