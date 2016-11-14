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
 * @file atomic.c  - Implementation of atomic operations for MaxScale
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 10/06/13     Mark Riddoch    Initial implementation
 *
 * @endverbatim
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
