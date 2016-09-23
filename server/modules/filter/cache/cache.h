#ifndef CACHE_H
#define CACHE_H
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


typedef enum cache_references
{
    CACHE_REFERENCES_ANY,         // select * from tbl;
    CACHE_REFERENCES_QUALIFIED    // select * from db.tbl;
} cache_references_t;

#define CACHE_DEFAULT_ALLOWED_REFERENCES CACHE_REFERENCES_QUALIFIED
// Count
#define CACHE_DEFAULT_MAX_RESULTSET_ROWS UINT_MAX
// Bytes
#define CACHE_DEFAULT_MAX_RESULTSET_SIZE 64 * 1024
// Seconds
#define CACHE_DEFAULT_TTL                10

#endif
