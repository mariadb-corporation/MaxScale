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

// TODO: Temporaily public
public:
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
    void epoll_tick() override final;

    std::atomic<bool> m_alive;
};

}
