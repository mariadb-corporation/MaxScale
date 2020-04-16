/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <sys/epoll.h>

struct MXB_POLL_DATA;

typedef struct MXB_WORKER
{
} MXB_WORKER;

typedef uint32_t (* mxb_poll_handler_t)(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);

typedef struct MXB_POLL_DATA
{
    mxb_poll_handler_t handler; /*< Handler for this particular kind of mxb_poll_data. */
    MXB_WORKER*        owner;   /*< Owning worker. */
} MXB_POLL_DATA;

class Worker : public MXB_WORKER
{
public:
    Worker();
    void add_fd(int fd, uint32_t events, MXB_POLL_DATA* pData);
    void run();
private:
    const int m_epoll_fd;               /*< The epoll file descriptor. */
    uint32_t  m_max_events = 42;
};
