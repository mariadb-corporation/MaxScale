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
 * The global housekeeper heartbeat value. This value is incremented
 * every 100 milliseconds and may be used for crude timing etc.
 */

extern int64_t hkheartbeat;

/**
 * Convert heartbeats to seconds
 */
#define HB_TO_SEC(a) ((int64_t)a / 10)

/**
 * Convert seconds to heartbeats
 */
#define SEC_TO_HB(a) ((int64_t)a * 10)

MXS_END_DECLS
