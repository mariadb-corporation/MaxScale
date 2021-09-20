/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/atomic.h>

/**
 * @file atomic.c  - Implementation of atomic operations
 */

int atomic_add(int* variable, int value)
{
    return __atomic_fetch_add(variable, value, __ATOMIC_SEQ_CST);
}

int32_t atomic_load_int32(const int32_t* variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

uint32_t atomic_load_uint32(const uint32_t* variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

uint64_t atomic_load_uint64(const uint64_t* variable)
{
    return __atomic_load_n(variable, __ATOMIC_SEQ_CST);
}

void atomic_store_int32(int32_t* variable, int32_t value)
{
    __atomic_store_n(variable, value, __ATOMIC_SEQ_CST);
}

bool atomic_cas_ptr(void** variable, void** old_value, void* new_value)
{
    return __atomic_compare_exchange_n(variable,
                                       old_value,
                                       new_value,
                                       false,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
}

