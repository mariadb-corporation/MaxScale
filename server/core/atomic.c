/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/atomic.h>

/**
 * @file atomic.c  - Implementation of atomic operations for MaxScale
 */

int atomic_add(int *variable, int value)
{
    return __sync_fetch_and_add(variable, value);
}

int64_t atomic_add_int64(int64_t *variable, int64_t value)
{
    return __sync_fetch_and_add(variable, value);
}

uint64_t atomic_add_uint64(uint64_t *variable, int64_t value)
{
    return __sync_fetch_and_add(variable, value);
}
