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
 * @file sqlite3.h
 *
 * Common SQLite defines
 */

#include <maxscale/cdefs.h>
#include <sqlite3.h>

/** SQLite3 version 3.7.14 introduced the new v2 close interface */
#if SQLITE_VERSION_NUMBER < 3007014
#define sqlite3_close_v2 sqlite3_close
#endif

/** Default timeout is one minute */
#define MXS_SQLITE_BUSY_TIMEOUT 60000
