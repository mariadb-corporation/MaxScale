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
     * Process one request
     */
    void process();

private:
    int m_fd;                       /**< The client socket */
    int m_timeout;                  /**< Network timeout for reads and writes */
    struct sockaddr_storage m_addr; /**< Network info for the client */
};
