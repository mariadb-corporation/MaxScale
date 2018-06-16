/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/maxscale.h>

#include <time.h>

#include <maxscale/worker.hh>

#include "internal/maxscale.h"
#include "internal/service.h"

static time_t started;

void maxscale_reset_starttime(void)
{
    started = time(0);
}

time_t maxscale_started(void)
{
    return started;
}

int maxscale_uptime()
{
    return time(0) - started;
}

int maxscale_shutdown()
{
    static int n_shutdowns = 0;

    int n = atomic_add(&n_shutdowns, 1);

    if (n == 0)
    {
        service_shutdown();
        mxs::Worker::shutdown_all();
    }

    return n + 1;
}
