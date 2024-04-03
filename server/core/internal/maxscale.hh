/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file core/maxscale/maxscale.hh - The private maxscale general definitions
 */

#include <maxscale/maxscale.hh>

/**
 * Initiate shutdown of MaxScale.
 *
 * This functions informs all threads that they should stop the
 * processing and exit. This should only be called by the SIGTERM and SIGINT signal handlers.
 *
 * @return How many times maxscale_shutdown() has been called.
 */
int maxscale_shutdown();

/**
 * Reset the start time from which the uptime is calculated.
 */
void maxscale_reset_starttime();

// Helper functions for debug assertions
bool maxscale_teardown_in_progress();
void maxscale_start_teardown();

enum class LogBlurbAction {STARTUP, LOG_ROTATION};

/**
 * Log the details of the MaxScale and the system it is running on
 *
 * Should be called on startup and whenever the log is rotated.
 *
 * @param type The action type
 */
void maxscale_log_info_blurb(LogBlurbAction type);
