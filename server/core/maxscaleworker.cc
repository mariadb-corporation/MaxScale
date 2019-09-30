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
#include <maxscale/maxscaleworker.hh>
#include <maxscale/mainworker.hh>

namespace maxscale
{

class MaxScaleWorker::WatchdogNotifier
{
    WatchdogNotifier(const WatchdogNotifier&) = delete;
    WatchdogNotifier& operator=(const WatchdogNotifier&) = delete;

public:
    WatchdogNotifier(mxs::MaxScaleWorker* pOwner)
        : m_owner(*pOwner)
        , m_nClients(0)
        , m_terminate(false)
    {
        m_thread = std::thread([this] {
                                   uint32_t interval = mxs::MainWorker::watchdog_interval().secs();
                                   timespec timeout = {interval, 0};

                                   while (!mxb::atomic::load(&m_terminate, mxb::atomic::RELAXED))
                                   {
                                        // We will wakeup when someone wants the notifier to run,
                                        // or when MaxScale is going down.
                                       m_sem_start.wait();

                                       if (!mxb::atomic::load(&m_terminate, mxb::atomic::RELAXED))
                                       {
                                            // If MaxScale is not going down...
                                           do
                                           {
                                                // we check the systemd watchdog...
                                               m_owner.check_systemd_watchdog();
                                           }
                                           while (!m_sem_stop.timedwait(timeout));
                                            // until the semaphore is actually posted, which it will be
                                            // once the notification should stop.
                                       }
                                   }
                               });
    }

    ~WatchdogNotifier()
    {
        mxb_assert(m_nClients == 0);
        mxb::atomic::store(&m_terminate, true, mxb::atomic::RELAXED);
        m_sem_start.post();
        m_thread.join();
    }

    void start()
    {
        Guard guard(m_lock);
        mxb::atomic::add(&m_nClients, 1, mxb::atomic::RELAXED);

        if (m_nClients == 1)
        {
            m_sem_start.post();
        }
    }

    void stop()
    {
        Guard guard(m_lock);
        mxb::atomic::add(&m_nClients, -1, mxb::atomic::RELAXED);
        mxb_assert(m_nClients >= 0);

        if (m_nClients == 0)
        {
            m_sem_stop.post();
        }
    }

private:
    using Guard = std::lock_guard<std::mutex>;

    mxs::MaxScaleWorker& m_owner;
    int                  m_nClients;
    bool                 m_terminate;
    std::thread          m_thread;
    std::mutex           m_lock;
    mxb::Semaphore       m_sem_start;
    mxb::Semaphore       m_sem_stop;
};


MaxScaleWorker::MaxScaleWorker(MainWorker* pMain)
    : m_main(*pMain)
    , m_alive(true)
{
    if (MainWorker::watchdog_interval().count() != 0)
    {
        m_pWatchdog_notifier = new WatchdogNotifier(this);
    }

    m_main.add(this);
}

MaxScaleWorker::~MaxScaleWorker()
{
    m_main.remove(this);

    delete m_pWatchdog_notifier;
}

void MaxScaleWorker::start_watchdog_workaround()
{
    if (m_pWatchdog_notifier)
    {
        m_pWatchdog_notifier->start();
    }
}

void MaxScaleWorker::stop_watchdog_workaround()
{
    if (m_pWatchdog_notifier)
    {
        m_pWatchdog_notifier->stop();
    }
}

void MaxScaleWorker::epoll_tick()
{
    check_systemd_watchdog();

    epoll_tock();
}

}
