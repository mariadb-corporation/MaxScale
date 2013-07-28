/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file atomic.c  - Implementation of atomic opertions for the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 10/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * Implementation of an atomic add operation for the X86 processor.
 * Adds a value to the contents of a location pointed to by the first parameter.
 * The add operation is atomic and the return value is the value stored in the location
 * prior to the operation. The number that is added may be signed, therefore atomic_subtract
 * is merely an atomic add with a negative value.
 *
 * @param variable	Pointer the the variable to add to
 * @param value		Value to be added
 * @return		The value of *variable before the add occured
 */
int
atomic_add(int *variable, int value)
{
	asm volatile(
		"lock; xaddl %%eax, %2;"
		:"=a" (value)
		: "a" (value), "m" (*variable)
		: "memory" );
	return value;
}
