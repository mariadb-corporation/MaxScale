#pragma once
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

/**
 * @file atomic.h The atomic operations used within the gateway
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * Implementation of an atomic add operations for the GCC environment.
 *
 * Adds a value to the contents of a location pointed to by the first parameter.
 * The add operation is atomic and the return value is the value stored in the
 * location prior to the operation. The number that is added may be signed,
 * therefore atomic_subtract is merely an atomic add with a negative value.
 *
 * @param variable      Pointer the the variable to add to
 * @param value         Value to be added
 * @return              The value of variable before the add occurred
 */
int      atomic_add(int *variable, int value);
int64_t  atomic_add_int64(int64_t *variable, int64_t value);
uint64_t atomic_add_uint64(uint64_t *variable, int64_t value);

/**
 * @brief Impose a full memory barrier
 *
 * A full memory barrier guarantees that all store and load operations complete
 * before the function is called.
 *
 * Currently, only the GNUC __sync_synchronize() is used. C11 introduces
 * standard functions for atomic memory operations and should be taken into use.
 *
 * @see https://www.kernel.org/doc/Documentation/memory-barriers.txt
 */
static inline void atomic_synchronize()
{
#ifdef __GNUC__
    __sync_synchronize(); /* Memory barrier. */
#else
#error "No GNUC atomics available."
#endif
}

MXS_END_DECLS
