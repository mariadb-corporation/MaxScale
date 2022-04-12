/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

namespace maxbase
{

namespace poll_action
{
constexpr uint32_t NOP = 0x00;
constexpr uint32_t ACCEPT = 0x01;
constexpr uint32_t READ = 0x02;
constexpr uint32_t WRITE = 0x04;
constexpr uint32_t HUP = 0x08;
constexpr uint32_t ERROR = 0x10;
}

class Worker;

class Pollable
{
public:
    Pollable(const Pollable&) = delete;
    Pollable& operator=(const Pollable&) = delete;

    Pollable()
    {
    }

    Worker* polling_worker() const
    {
        return m_pPolling_worker;
    }

    /**
     * The file descriptor to add to the epoll-set.
     *
     * @return A *non-blocking* file descriptor.
     */
    virtual int poll_fd() const = 0;

    virtual uint32_t handle_poll_events(Worker* worker, uint32_t events) = 0;

private:
    friend class Worker;
    void set_polling_worker(Worker* pWorker)
    {
        m_pPolling_worker = pWorker;
    }

    Worker* m_pPolling_worker { nullptr };   /*< Owning worker. */
};

}
