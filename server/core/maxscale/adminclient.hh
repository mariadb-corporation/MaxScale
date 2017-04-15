#pragma once

/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <map>
#include <sys/socket.h>
#include <tr1/memory>

#include <maxscale/atomic.h>
#include <maxscale/spinlock.hh>

using mxs::SpinLock;

class AdminClient
{
public:
    /**
     * @brief Create a new client connection
     *
     * @param fd      Client socket
     * @param addr    Network address where @c fd is connected to
     * @param timeout Network timeout for reads and writes
     */
    AdminClient(int fd, const struct sockaddr_storage& addr, int timeout);

    ~AdminClient();

    /**
     * @brief Process one request
     */
    void process();

    /**
     *  @brief Close the connection
     */
    void close_connection();

    /**
     * @brief Get last activity timestamp
     *
     * @return The hkheartbeat of the last activity
     */
    int64_t last_activity()
    {
        return atomic_read_int64(&m_last_activity);
    }

private:
    int m_fd;                       /**< The client socket */
    int64_t m_last_activity;
    struct sockaddr_storage m_addr; /**< Network info for the client */
    SpinLock m_lock;
};

typedef std::shared_ptr<AdminClient> SAdminClient;
