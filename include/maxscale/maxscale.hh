/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file include/maxscale/maxscale.h Some general definitions for MaxScale
 */

#include <maxscale/ccdefs.hh>

#include <ctime>

/**
 * Return the time when MaxScale was started.
 */
time_t maxscale_started();

/**
 * Return the time MaxScale has been running.
 *
 * @return The uptime in seconds.
 */
int maxscale_uptime();

/**
 * Is MaxScale shutting down
 *
 * This function can be used to detect whether the shutdown has been initiated. It does not tell
 * whether the shutdown has been completed so thread-safety is still important.
 *
 * @return True if MaxScale is shutting down
 */
bool maxscale_is_shutting_down();
