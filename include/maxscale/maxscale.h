/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

/**
 * @file include/maxscale/maxscale.h Some general definitions for MaxScale
*/

#include <maxscale/cdefs.h>

#include <time.h>

MXS_BEGIN_DECLS

/* Exit status for MaxScale */
#define MAXSCALE_SHUTDOWN       0   /* Normal shutdown */
#define MAXSCALE_BADCONFIG      1   /* Configuration file error */
#define MAXSCALE_NOLIBRARY      2   /* No embedded library found */
#define MAXSCALE_NOSERVICES     3   /* No services could be started */
#define MAXSCALE_ALREADYRUNNING 4   /* MaxScale is already running */
#define MAXSCALE_BADARG         5   /* Bad command line argument */
#define MAXSCALE_INTERNALERROR  6   /* Internal error, see error log */

/**
 * Return the time when MaxScale was started.
 */
time_t maxscale_started(void);

/**
 * Return the time MaxScale has been running.
 *
 * @return The uptime in seconds.
 */
int maxscale_uptime(void);

/**
 * Is MaxScale shutting down
 *
 * This function can be used to detect whether the shutdown has been initiated. It does not tell
 * whether the shutdown has been completed so thread-safety is still important.
 *
 * @return True if MaxScale is shutting down
 */
bool maxscale_is_shutting_down();

MXS_END_DECLS
