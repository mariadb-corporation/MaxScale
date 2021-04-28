/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file poll.c  - Abstraction of the epoll functionality
 */

#include <maxscale/poll.hh>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <mysql.h>
#include <sys/epoll.h>

#include <maxbase/alloc.h>
#include <maxbase/atomic.h>
#include <maxscale/config.hh>
#include <maxscale/clock.h>
#include <maxscale/server.hh>
#include <maxscale/statistics.hh>
#include <maxscale/routingworker.hh>

#include "internal/poll.hh"

using maxbase::Worker;
using maxscale::RoutingWorker;

/**
 * Return a poll statistic from the polling subsystem
 *
 * @param what  The required statistic
 * @return      The value of that statistic
 */
int64_t poll_get_stat(POLL_STAT what)
{
    return RoutingWorker::get_one_statistic(what);
}
