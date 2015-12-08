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
#include <stdio.h>
#include <gwbitmask.h>

/**
 * @file gwbitmask.c  Implementation of bitmask operations for the gateway
 *
 * We provide basic bitmask manipulation routines, the size of
 * the bitmask will grow dynamically based on the highest bit
 * number that is set or cleared within the bitmask.
 *
 * Bitmask growth happens in increments rather than via a single bit as
 * a time.
 *
 * Please note limitations to these mechanisms:
 *
 * 1. The initial size and increment size MUST be exact multiples of 8
 * 2. Only suitable for a compact set of bit numbers i.e. the numbering
 * needs to start near to 0 and grow without sizeable gaps
 * 3. It is assumed that a bit number bigger than the current size can
 * be accommodated by adding a single extra block of bits
 * 4. During copy, if memory cannot be allocated, a zero length bitmap is
 * created as the destination. This will test true for all bits clear, which
 * may be a serious error. However, the memory requirement is very small and
 * is only likely to fail in circumstances where a lot else is going wrong.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 28/06/13     Mark Riddoch        Initial implementation
 * 20/08/15     Martin Brampton     Added caveats about limitations (above)
 * 17/10/15     Martin Brampton     Added display of bitmask
 *
 * @endverbatim
 */

static int bitmask_isset_without_spinlock(GWBITMASK *bitmask, int bit);
static int bitmask_count_bits_set(GWBITMASK *bitmask);

/**
 * Initialise a bitmask
 *
 * @param bitmask       Pointer the bitmask
 * @return              The value of *variable before the add occurred
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
 * beyond the current bitmask length. Note that growth is only
 * by a single increment - the bit numbers used need to be a
 * fairly dense set.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to set
 */
void
bitmask_set(GWBITMASK *bitmask, int bit)
{
    unsigned char *ptr;
    unsigned char mask;

    spinlock_acquire(&bitmask->lock);
    while (bit >= bitmask->length)
    {
        bitmask->bits = realloc(bitmask->bits,
                                (bitmask->length + BIT_LENGTH_INC) / 8);
        memset(bitmask->bits + (bitmask->length / 8), 0,
               BIT_LENGTH_INC / 8);
        bitmask->length += BIT_LENGTH_INC;
    }
    ptr = bitmask->bits + (bit / 8);
    mask = 1 << (bit % 8);
    *ptr |= mask;
    spinlock_release(&bitmask->lock);
}

/**
 * Clear the bit at the specified bit position in the bitmask.
 * The bitmask will automatically be extended if the bit is
 * beyond the current bitmask length. This could be optimised
 * by always assuming that a bit beyond the current length is
 * unset (i.e. 0) and not extending the actual bitmask.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to clear
 */
void
bitmask_clear(GWBITMASK *bitmask, int bit)
{
    unsigned char *ptr;
    unsigned char mask;

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
 * beyond the current bitmask length. The work is done in the function
 * bitmask_isset_without_spinlock, which can be called when a spinlock
 * has already been acquired.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to test
 */
int
bitmask_isset(GWBITMASK *bitmask, int bit)
{
    int result;

    spinlock_acquire(&bitmask->lock);
    result = bitmask_isset_without_spinlock(bitmask, bit);
    spinlock_release(&bitmask->lock);
    return result;
}

/**
 * Return a non-zero value if the bit at the specified bit
 * position in the bitmask is set.  Should be called while holding a
 * lock on the bitmask.
 *
 * The bitmask will automatically be extended if the bit is
 * beyond the current bitmask length. This could be optimised
 * by assuming that a bit beyond the length is unset.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to test
 */
static int
bitmask_isset_without_spinlock(GWBITMASK *bitmask, int bit)
{
    unsigned char *ptr;
    unsigned char mask;

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
    return *ptr & mask;
}

/**
 * Return a non-zero value of the bitmask has no bits set
 * in it.  This logic could be defeated if the bitmask is a
 * copy and there was insufficient memory when the copy was
 * made.
 *
 * @param bitmask       Pointer the bitmask
 * @return              Non-zero if the bitmask has no bits set
 */
int
bitmask_isallclear(GWBITMASK *bitmask)
{
    unsigned char *ptr, *eptr;

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
 * On memory failure, a zero length bitmask is created in the destination,
 * which could seriously undermine the logic.  Given the small size of the
 * bitmask, this is unlikely to happen.
 *
 * @param dest  Bitmap tp update
 * @param src   Bitmap to copy
 */
void
bitmask_copy(GWBITMASK *dest, GWBITMASK *src)
{
    spinlock_acquire(&src->lock);
    spinlock_acquire(&dest->lock);
    if (dest->length)
    {
        free(dest->bits);
    }
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

/**
 * Return a comma separated list of the numbers of the bits that are set in
 * a bitmask, numbering starting at zero. Constrained to reject requests that
 * could require more than three digit numbers.  The returned string must be
 * freed by the caller (unless it is null on account of memory allocation
 * failure).
 *
 * @param bitmask       Bitmap to make readable
 * @return pointer to the newly allocated string, or null if no memory
 */
char *
bitmask_render_readable(GWBITMASK *bitmask)
{
    static const char toobig[] = "Bitmask is too large to render readable";
    static const char empty[] = "No bits are set";
    char onebit[5];
    char *result;
    int count_set = 0;

    spinlock_acquire(&bitmask->lock);
    if (999 < bitmask->length)
    {
        result = malloc(sizeof(toobig));
        if (result)
        {
            strcpy(result, toobig);
        }
    }
    else
    {
        count_set = bitmask_count_bits_set(bitmask);
        if (count_set)
        {
            result = malloc(1 + (4 * count_set));
            if (result)
            {
                result[0] = 0;
                for (int i = 0; i<bitmask->length; i++)
                {
                    if (bitmask_isset_without_spinlock(bitmask, i))
                    {
                        sprintf(onebit, "%d,", i);
                        strcat(result, onebit);
                    }
                }
                result[strlen(result)-1] = 0;
            }
        }
        else
        {
            result = malloc(sizeof(empty));
            if (result)
            {
                strcpy(result, empty);
            }
        }
    }
    spinlock_release(&bitmask->lock);
    return result;
}

/**
 * Return a count of the number of bits set in a bitmask.  Helpful for setting
 * the size of string needed to show the set bits in readable form.
 *
 * @param bitmask       Bitmap whose bits are to be counted
 * @return int      Number of set bits
 */
static int
bitmask_count_bits_set(GWBITMASK *bitmask)
{
    const unsigned char oneBits[] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
    unsigned char partresults;
    int result = 0;
    unsigned char *ptr, *eptr;

    ptr = bitmask->bits;
    eptr = ptr + (bitmask->length / 8);
    while (ptr < eptr)
    {
        partresults = oneBits[*ptr&0x0f];
        partresults += oneBits[*ptr>>4];
        result += partresults;
        ptr++;
    }
    return result;
}
