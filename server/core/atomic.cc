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

#include <maxscale/atomic.h>

/**
 * @file atomic.c  - Implementation of atomic operations for MaxScale
 */

int atomic_add(int *variable, int value)
{
    return __atomic_fetch_add(variable, value, __ATOMIC_SEQ_CST);
}

uint32_t atomic_add_uint32(uint32_t *variable, int32_t value)
{
    return __atomic_fetch_add(variable, value, __ATOMIC_SEQ_CST);
}

int64_t atomic_add_int64(int64_t *variable, int64_t value)
{
    return __atomic_fetch_add(variable, value, __ATOMIC_SEQ_CST);
}

uint64_t atomic_add_uint64(uint64_t *variable, int64_t value)
{
    return __atomic_fetch_add(variable, value, __ATOMIC_SEQ_CST);
}

int atomic_load_int(const int *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

int32_t atomic_load_int32(const int32_t *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

int64_t atomic_load_int64(const int64_t *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

uint32_t atomic_load_uint32(const uint32_t *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

uint64_t atomic_load_uint64(const uint64_t *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

void* atomic_load_ptr(void * const *variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

void atomic_store_int(int *variable, int value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

void atomic_store_int32(int32_t *variable, int32_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

void atomic_store_int64(int64_t *variable, int64_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

void atomic_store_uint32(uint32_t *variable, uint32_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

void atomic_store_uint64(uint64_t *variable, uint64_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

void atomic_store_ptr(void **variable, void *value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

bool atomic_cas_ptr(void **variable, void** old_value, void *new_value)
{
    return __atomic_compare_exchange_n(variable, old_value, new_value,
                                       false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int atomic_exchange_int(int *variable, int new_value)
{
    return __atomic_exchange_n(variable, new_value, __ATOMIC_SEQ_CST);
}