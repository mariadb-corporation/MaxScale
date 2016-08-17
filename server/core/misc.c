/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale.h>
#include <time.h>

static time_t started;

/**
 * Reset the start time from which the uptime is calculated.
 */
void maxscale_reset_starttime(void)
{
    started = time(0);
}

/**
 * Return the time when MaxScale was started.
 */
time_t maxscale_started(void)
{
    return started;
}

/**
 * Return the time MaxScale has been running.
 *
 * @return The uptime in seconds.
 */
int maxscale_uptime()
{
    return time(0) - started;
}
