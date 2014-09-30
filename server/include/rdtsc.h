#ifndef _RDTSC_H
#define _RDTSC_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
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
 * Date		Who		Description
 * 19/09/2014	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

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
#endif
