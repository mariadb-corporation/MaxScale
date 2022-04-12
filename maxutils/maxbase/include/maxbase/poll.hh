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

constexpr uint32_t INTERRUPTED = 0x20;
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

    /**
     * Handle events that have occurred on the @c Pollable's file descriptor.
     *
     * @param worker  The worker polling the @c Pollable.
     * @param events  The events of the @c Pollable's file descriptor.
     *
     * @return A mask of @c poll_action values.
     *
     * @note If @c poll_action::INTERRUPTED is set in the returned value, then
     *       @c handle_poll_events will be called again, irrespective of whether
     *       any new events have occurred on the file descriptor. If
     *       @c INTERRUPTED is returned, then it is the responsibility of
     *       the @c Pollable to store sufficient context to know where it
     *       should proceed, when @c handle_poll_events is called again.
     *       The value of @c events will not change if @c handle_poll_events
     *       is called again, irrespective of any additional bits set in the
     *       returned value.
     */
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
