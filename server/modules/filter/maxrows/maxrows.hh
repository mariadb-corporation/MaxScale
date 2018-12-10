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

#define MXS_MODULE_NAME "maxrows"

#include <maxscale/ccdefs.hh>

#include <limits.h>

MXS_BEGIN_DECLS

/*
 * The EOF packet 2 bytes flags start after:
 * network header (4 bytes) + eof indicator (1) + 2 bytes warnings count)
 */
#define MAXROWS_MYSQL_EOF_PACKET_FLAGS_OFFSET (MYSQL_HEADER_LEN + 1 + 2)

#define MAXROWS_DEBUG_NONE       0
#define MAXROWS_DEBUG_DISCARDING 1
#define MAXROWS_DEBUG_DECISIONS  2

#define MAXROWS_DEBUG_USAGE (MAXROWS_DEBUG_DECISIONS | MAXROWS_DEBUG_DISCARDING)
#define MAXROWS_DEBUG_MIN   MAXROWS_DEBUG_NONE
#define MAXROWS_DEBUG_MAX   MAXROWS_DEBUG_USAGE

// Count
#define MAXROWS_DEFAULT_MAX_RESULTSET_ROWS MXS_MODULE_PARAM_COUNT_MAX
// Bytes
#define MAXROWS_DEFAULT_MAX_RESULTSET_SIZE "65536"
// Integer value
#define MAXROWS_DEFAULT_DEBUG "0"
// Max size of copied input SQL
#define MAXROWS_INPUT_SQL_MAX_LEN 1024

MXS_END_DECLS
