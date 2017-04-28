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
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_add_fetch(variable, value, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_add(variable, value);
#endif
}

int64_t atomic_add_int64(int64_t *variable, int64_t value)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_add_fetch(variable, value, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_add(variable, value);
#endif
}

uint64_t atomic_add_uint64(uint64_t *variable, int64_t value)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_add_fetch(variable, value, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_add(variable, value);
#endif
}

int atomic_load_int32(int *variable)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_or(variable, 0);
#endif
}

int64_t atomic_load_int64(int64_t *variable)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_or(variable, 0);
#endif
}

uint64_t atomic_load_uint64(uint64_t *variable)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
#else
    return __sync_fetch_and_or(variable, 0);
#endif
}

void atomic_store_int32(int *variable, int value)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
#else
    __sync_lock_test_and_set(variable, value);
#endif
}

void atomic_store_int64(int64_t *variable, int64_t value)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
#else
    __sync_lock_test_and_set(variable, value);
#endif
}

void atomic_store_uint64(uint64_t *variable, uint64_t value)
{
#ifdef MXS_USE_ATOMIC_BUILTINS
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
#else
    __sync_lock_test_and_set(variable, value);
#endif
}
