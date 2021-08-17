/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <atomic>
#include <maxbase/worker.hh>
#include <maxbase/watchdognotifier.hh>

namespace maxbase
{

/**
 * @class WatchedWorker
 *
 * Base-class for workers that should be watched, that is, monitored
 * to ensure that they are processing epoll events.
 *
 * If a watched worker stops processing events it will cause the systemd
 * watchdog notification *not* to be generated, with the effect that the
 * process is killed and restarted.
 */
class WatchedWorker : public Worker,
                      public WatchdogNotifier::Dependent
{
public:
    ~WatchedWorker();

protected:
    WatchedWorker(mxb::WatchdogNotifier* pNotifier);

private:
    void call_epoll_tick() override final;
};

}
