#pragma once
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

/**
 * @file core/maxscale/maxscale.h - The private maxscale general definitions
 */

#include <maxscale/maxscale.h>

MXS_BEGIN_DECLS

/**
 * Initiate shutdown of MaxScale.
 *
 * This functions informs all threads that they should stop the
 * processing and exit.
 *
 * @return How many times maxscale_shutdown() has been called.
 */
int maxscale_shutdown(void);

/**
 * Reset the start time from which the uptime is calculated.
 */
void maxscale_reset_starttime(void);

MXS_END_DECLS
