/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file atomic.c  - Implementation of atomic opertions for the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 10/06/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <atomic.h>

/**
 * Implementation of an atomic add operation for the GCC environment.
 * If we are working within GNU C then we can use the GCC atomic add
 * built in function, which is portable across platforms that implement GCC.
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
int atomic_add(int *variable, int value)
{
    return __sync_fetch_and_add(variable, value);
}

int64_t atomic_add_int64(int64_t *variable, int64_t value)
{
    return __sync_fetch_and_add(variable, value);
}
