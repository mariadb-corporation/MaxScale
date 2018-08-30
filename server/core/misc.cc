/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/maxscale.h>

#include <time.h>

#include <maxscale/routingworker.hh>

#include "internal/maxscale.h"
#include "internal/service.hh"

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

static sig_atomic_t n_shutdowns = 0;

bool maxscale_is_shutting_down()
{
    return n_shutdowns != 0;
}

int maxscale_shutdown()
{
    int n = n_shutdowns++;

    if (n == 0)
    {
        mxs::RoutingWorker::shutdown_all();
    }

    return n + 1;
}

static bool teardown_in_progress = false;

bool maxscale_teardown_in_progress()
{
    return teardown_in_progress;
}

void maxscale_start_teardown()
{
    teardown_in_progress = true;
}
