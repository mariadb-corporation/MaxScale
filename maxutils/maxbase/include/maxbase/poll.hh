/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <sys/epoll.h>

namespace maxbase
{

/**
 * For development and debugging, a function that converts a bitmask of epoll
 * events to a string with the corresponding information, e.g. "EPOLLIN|EPOLLOUT".
 *
 * @param events  A bitmask of EPOLLIN, EPOLLOUT, etc.
 *
 * @return  A '|' separated string of "EPOLLIN", "EPOLLOUT", etc..
 */
std::string epoll_events_to_string(EPOLL_EVENTS events);

namespace poll_action
{
constexpr uint32_t NOP = 0x00;
constexpr uint32_t ACCEPT = 0x01;
constexpr uint32_t READ = 0x02;
constexpr uint32_t WRITE = 0x04;
constexpr uint32_t HUP = 0x08;
constexpr uint32_t ERROR = 0x10;

constexpr uint32_t INCOMPLETE_READ = 0x20;
}

class Worker;

class Pollable
{
public:
    enum Kind
    {
        UNIQUE, // The Pollable can be added to at most 1 worker.
        SHARED  // The Pollable may be added to multiple workers.
    };

    enum Context
    {
        NEW_CALL,     // Due to event returned from epoll_wait().
        REPEATED_CALL // Due to previous event handling having returned poll_action::INCOMPLETE_READ
    };

    Pollable(const Pollable&) = delete;
    Pollable& operator=(const Pollable&) = delete;

    Pollable(Kind kind = UNIQUE)
        : m_kind(kind)
    {
    }

    Kind kind() const
    {
        return m_kind;
    }

    bool is_unique() const
    {
        return m_kind == UNIQUE;
    }

    bool is_shared() const
    {
        return m_kind == SHARED;
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
     * @param worker   The calling worker. If the Pollable is UNIQUE, then
     *                 will be same as that returned by @c polling_worker().
     * @param context  Whether it is a new or repeated call.
     * @param events   The events of the @c Pollable's file descriptor.
     *
     * @return A mask of @c poll_action values.
     *
     * @note If @c poll_action::INCOMPLETE_READ is set in the returned value, then
     *       @c handle_poll_events will be called again, irrespective of whether
     *       any new events have occurred on the file descriptor. If
     *       @c INCOMPLETE_READ is returned, then it is the responsibility of
     *       the @c Pollable to store sufficient context to know where it
     *       should proceed, when @c handle_poll_events is called again.
     *       The value of @c events will not change if @c handle_poll_events
     *       is called again, irrespective of any additional bits set in the
     *       returned value.
     */
    virtual uint32_t handle_poll_events(Worker* pWorker, uint32_t events, Context context) = 0;

    virtual ~Pollable() = default;

private:
    friend class Worker;
    void set_polling_worker(Worker* pWorker)
    {
        m_pPolling_worker = pWorker;
    }

    const Kind m_kind;
    Worker*    m_pPolling_worker { nullptr };   /*< Owning worker. */
};

}
