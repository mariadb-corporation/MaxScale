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
#include <maxscale/watchdognotifier.hh>

namespace maxscale
{

/**
 * Base-class for workers that should be watched, that is, monitored
 * to ensure that they are processing epoll events.
 *
 * In case a watched worker stops processing events that will cause
 * systemd watchdog notifiation *not* to be generated, with the effect
 * that MaxScale is killed and restarted.
 */
class WatchedWorker : public mxb::Worker,
                      protected WatchdogNotifier::Dependent
{
public:
    ~WatchedWorker();

protected:
    WatchedWorker(WatchdogNotifier* pNotifier);

    /**
     * Called once per epoll loop from epoll_tick().
     */
    virtual void epoll_tock();

private:
    void epoll_tick() override final;
};

}
