/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

class DCB;

class DCBHandler
{
public:
    /**
     * EPOLLIN handler, used to read available data from network socket
     *
     * @param dcb  DCB to read from.
     */
    virtual void ready_for_reading(DCB* dcb) = 0;

    /**
     * EPOLLOUT handler, used to write buffered data
     *
     * @param dcb  DCB to write to.
     */
    virtual void write_ready(DCB* dcb) = 0;

    /**
     * EPOLLERR handler
     *
     * @param dcb  DCB for which the error occurred.
     */
    virtual void error(DCB* dcb) = 0;

    /**
     * EPOLLHUP and EPOLLRDHUP handler
     *
     * @param dcb  DCB for which the hangup occurred.
     */
    virtual void hangup(DCB* dcb) = 0;
};
