#pragma once
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
 * @file rdtsc.h Access the process time-stamp counter
 *
 * This is an Intel only facilty that is used to access an accurate time
 * value, the granularity of which is related to the processor clock speed
 * and the overhead for access is much lower than using any system call
 * mechanism.
 *
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 19/09/2014   Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

typedef unsigned long long CYCLES;

/**
 * Get the current time-stamp counter value from the processor. This is the
 * count of CPU cyceles as a 64 bit value.
 *
 * The value returned is related to the clock speed, to obtian a value in
 * seconds divide the returned value by the clock frequency for the processor.
 *
 * Note, on multi-processsor systems care much be taken to avoid the thread
 * moving to a different processor when taken succsive value of RDTSC to
 * obtian accurate timing. This may be done by setting pocessor affinity for
 * the thread. See sched_setaffinity/sched_getaffinity.
 *
 * @return CPU cycle count
 */
static __inline__ CYCLES rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}

MXS_END_DECLS
