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

/**
 * Implementation of an atomic add operation for the GCC environment, or the
 * X86 processor.  If we are working within GNU C then we can use the GCC
 * atomic add built in function, which is portable across platforms that
 * implement GCC.  Otherwise, this function currently supports only X86
 * architecture (without further development).
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
int
atomic_add(int *variable, int value)
{
#ifdef __GNUC__
    return (int) __sync_fetch_and_add (variable, value);
#else
    asm volatile(
        "lock; xaddl %%eax, %2;"
        :"=a" (value)
        : "a" (value), "m" (*variable)
        : "memory" );
    return value;
#endif
}
