#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <limits.h>

MXS_BEGIN_DECLS

#define MAXROWS_OK_PACKET_LEN              11
#define MAXROWS_EOF_PACKET_LEN             9

/*
 * The EOF packet 2 bytes flags start after:
 * network header (4 bytes) + eof indicator (1) + 2 bytes warnings count)
 */
#define MAXROWS_MYSQL_EOF_PACKET_FLAGS_OFFSET (MYSQL_HEADER_LEN + 1 + 2)

#define MAXROWS_DEBUG_NONE         0
#define MAXROWS_DEBUG_MATCHING     1
#define MAXROWS_DEBUG_NON_MATCHING 2
#define MAXROWS_DEBUG_USE          4
#define MAXROWS_DEBUG_NON_USE      8

#define MAXROWS_DEBUG_RULES        (MAXROWS_DEBUG_MATCHING | MAXROWS_DEBUG_NON_MATCHING)
#define MAXROWS_DEBUG_USAGE        (MAXROWS_DEBUG_USE | MAXROWS_DEBUG_NON_USE)
#define MAXROWS_DEBUG_MIN          MAXROWS_DEBUG_NONE
#define MAXROWS_DEBUG_MAX          (MAXROWS_DEBUG_RULES | MAXROWS_DEBUG_USAGE)

// Count
#define MAXROWS_DEFAULT_MAX_RESULTSET_ROWS UINT_MAX
// Bytes
#define MAXROWS_DEFAULT_MAX_RESULTSET_SIZE 64 * 1024
// Integer value
#define MAXROWS_DEFAULT_DEBUG              0

MXS_END_DECLS
