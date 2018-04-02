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

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * The global clock
 *
 * This value is incremented roughly every 100 milliseconds and may be used for
 * very crude timing. The crudeness is due to the fact that the housekeeper
 * thread does the updating of this value.
 *
 * @return The current clock tick
 */
int64_t mxs_clock();

/**
 * Convert heartbeats to seconds
 */
#define MXS_CLOCK_TO_SEC(a) ((int64_t)a / 10)

/**
 * Convert seconds to heartbeats
 */
#define MXS_SEC_TO_CLOCK(a) ((int64_t)a * 10)

MXS_END_DECLS
