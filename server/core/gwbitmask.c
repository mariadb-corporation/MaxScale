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
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <stdlib.h>
#include <string.h>
#include <gwbitmask.h>

/**
 * @file gwbitmask.c  Implementation of bitmask opertions for the gateway
 *
 * We provide basic bitmask manipulation routines, the size of
 * the bitmask will grow dynamically based on the highest bit
 * number that is set or cleared within the bitmask.
 *
 * Bitmsk growth happens in increments rather than via a single bit as
 * a time.
 *
  * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 28/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * Initialise a bitmask
 *
 * @param bitmask	Pointer the bitmask
 * @return		The value of *variable before the add occured
 */
void
bitmask_init(GWBITMASK *bitmask)
{
	bitmask->length = BIT_LENGTH_INITIAL;
	if ((bitmask->bits = malloc(bitmask->length / 8)) == NULL)
	{
		bitmask->length = 0;
	}
	else
	{
		memset(bitmask->bits, 0, bitmask->length / 8);
	}
	spinlock_init(&bitmask->lock);
}

/**
 * Free a bitmask that is no longer required
 *
 * @param bitmask
 */
void
bitmask_free(GWBITMASK *bitmask)
{
	if (bitmask->length)
	{
		free(bitmask->bits);
		bitmask->length = 0;
	}
}

/**
 * Set the bit at the specified bit position in the bitmask.
 * The bitmask will automatically be extended if the bit is 
 * beyond the current bitmask length
 *
 * @param bitmask	Pointer the bitmask
 * @param bit		Bit to set
 */
void
bitmask_set(GWBITMASK *bitmask, int bit)
{
unsigned	char *ptr;
unsigned	char mask;

	spinlock_acquire(&bitmask->lock);
	if (bit >= bitmask->length)
	{
		bitmask->bits = realloc(bitmask->bits,
			(bitmask->length + BIT_LENGTH_INC) / 8);
		memset(bitmask->bits + (bitmask->length / 8), 0,
			BIT_LENGTH_INC / 8);
		bitmask->length += (BIT_LENGTH_INC / 8);
	}
	ptr = bitmask->bits + (bit / 8);
	mask = 1 << (bit % 8);
	*ptr |= mask;
	spinlock_release(&bitmask->lock);
}

/**
 * Clear the bit at the specified bit position in the bitmask.
 * The bitmask will automatically be extended if the bit is 
 * beyond the current bitmask length
 *
 * @param bitmask	Pointer the bitmask
 * @param bit		Bit to clear
 */
void
bitmask_clear(GWBITMASK *bitmask, int bit) 
{
unsigned	char *ptr;
unsigned	char mask;

	if (bit >= bitmask->length)
	{
		bitmask->bits = realloc(bitmask->bits,
			(bitmask->length + BIT_LENGTH_INC) / 8);
		memset(bitmask->bits + (bitmask->length / 8), 0,
			BIT_LENGTH_INC / 8);
		bitmask->length += (BIT_LENGTH_INC / 8);
	}
	ptr = bitmask->bits + (bit / 8);
	mask = 1 << (bit % 8);
	*ptr &= ~mask;
}

/**
 * Return a non-zero value if the bit at the specified bit
 * position in the bitmask is set.
 * The bitmask will automatically be extended if the bit is 
 * beyond the current bitmask length
 *
 * @param bitmask	Pointer the bitmask
 * @param bit		Bit to clear
 */
int
bitmask_isset(GWBITMASK *bitmask, int bit)
{
unsigned	char *ptr;
unsigned	char mask;

	spinlock_acquire(&bitmask->lock);
	if (bit >= bitmask->length)
	{
		bitmask->bits = realloc(bitmask->bits,
			(bitmask->length + BIT_LENGTH_INC) / 8);
		memset(bitmask->bits + (bitmask->length / 8), 0,
			BIT_LENGTH_INC / 8);
		bitmask->length += (BIT_LENGTH_INC / 8);
	}
	ptr = bitmask->bits + (bit / 8);
	mask = 1 << (bit % 8);
	spinlock_release(&bitmask->lock);
	return *ptr & mask;
}

/**
 * Return a non-zero value of the bitmask has no bits set
 * in it.
 *
 * @param bitmask	Pointer the bitmask
 * @return		Non-zero if the bitmask has no bits set
 */
int
bitmask_isallclear(GWBITMASK *bitmask)            
{
unsigned char	*ptr, *eptr;

	spinlock_acquire(&bitmask->lock);
	ptr = bitmask->bits;
	eptr = ptr + (bitmask->length / 8);
	while (ptr < eptr)
	{
		if (*ptr != 0)
		{
			spinlock_release(&bitmask->lock);
			return 0;
		}
		ptr++;
	}
	spinlock_release(&bitmask->lock);

	return 1;
}

/**
 * Copy the contents of one bitmap to another.
 *
 * @param dest	Bitmap tp update
 * @param src	Bitmap to copy
 */
void
bitmask_copy(GWBITMASK *dest, GWBITMASK *src)
{
	spinlock_acquire(&src->lock);
	spinlock_acquire(&dest->lock);
	if (dest->length)
		free(dest->bits);
	if ((dest->bits = malloc(src->length / 8)) == NULL)
	{
		dest->length = 0;
	}
	else
	{
		dest->length = src->length;
		memcpy(dest->bits, src->bits, src->length / 8);
	}
	spinlock_release(&dest->lock);
	spinlock_release(&src->lock);
}

