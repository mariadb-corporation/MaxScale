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
 * Pre 4.7 GCC doesn't support the __atomic builtin functions. The older __sync
 * builtins don't have proper store/load functionality so we use a somewhat ugly
 * hack to emulate the store/load.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#ifndef MXS_USE_ATOMIC_BUILTINS
#define MXS_USE_ATOMIC_BUILTINS 1
#endif
#endif

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
 * Implementation of an atomic load operation for the GCC environment.
 *
 * Loads a value from the contents of a location pointed to by the first parameter.
 * The load operation is atomic and it uses the strongest memory ordering.
 *
 * @param variable      Pointer the the variable to load from
 * @return The stored value
 */
int atomic_load_int32(int *variable);
int64_t atomic_load_int64(int64_t *variable);
uint64_t atomic_load_uint64(uint64_t *variable);

/**
 * Implementation of an atomic store operation for the GCC environment.
 *
 * Stores a value to the contents of a location pointed to by the first parameter.
 * The store operation is atomic and it uses the strongest memory ordering.
 *
 * @param variable      Pointer the the variable to store to
 * @param value         Value to be stored
 */
void atomic_store_int32(int *variable, int value);
void atomic_store_int64(int64_t *variable, int64_t value);
void atomic_store_uint64(uint64_t *variable, uint64_t value);

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

#ifdef MXS_USE_ATOMIC_BUILTINS
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    __sync_synchronize(); /* Memory barrier. */
#endif

#else
#error "No GNUC atomics available."
#endif
}

MXS_END_DECLS
