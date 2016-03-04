/*
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2016
 *
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
