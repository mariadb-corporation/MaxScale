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
 * @file
 *
 * Internal code for the statistics system.
 */

#include <maxscale/statistics.h>

MXS_BEGIN_DECLS

/**
 * @brief Initialize statistics system
 *
 * This function should only be called once by the MaxScale core.
 */
void ts_stats_init();

/**
 * @brief Terminate statistics system
 */
void ts_stats_end();

MXS_END_DECLS
