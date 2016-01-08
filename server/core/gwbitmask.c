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
 * 04/01/16     Martin Brampton     Changed bitmask_clear to not lock and return
 *                                  whether bitmask is clear; added bitmask_clear_with_lock.
 *
 * @endverbatim
 */

static int bitmask_isset_without_spinlock(GWBITMASK *bitmask, int bit);
static int bitmask_count_bits_set(GWBITMASK *bitmask);

static const unsigned char bitmapclear[8] = {
    255-1, 255-2, 255-4, 255-8, 255-16, 255-32, 255-64, 255-128
};
static const unsigned char bitmapset[8] = {
    1, 2, 4, 8, 16, 32, 64, 128
};

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
    bitmask->size = bitmask->length / 8;
    if ((bitmask->bits = calloc(bitmask->size, 1)) == NULL)
    {
        bitmask->length = bitmask->size = 0;
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
        bitmask->length = bitmask->size = 0;
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
    unsigned char *ptr = bitmask->bits;

    spinlock_acquire(&bitmask->lock);
    if (bit >= 8)
    {
        while (bit >= bitmask->length)
        {
            bitmask->bits = realloc(bitmask->bits,
                                (bitmask->size + (BIT_LENGTH_INC / 8)));
            memset(bitmask->bits + (bitmask->size), 0,
               BIT_LENGTH_INC / 8);
            bitmask->length += BIT_LENGTH_INC;
            bitmask->size += (BIT_LENGTH_INC / 8);
        }

        ptr += (bit / 8);
        bit = bit % 8;
    }
    *ptr |= bitmapset[bit];
    spinlock_release(&bitmask->lock);
}

/**
 * Clear the bit at the specified bit position in the bitmask.
 * Bits beyond the bitmask length are always assumed to be clear, so no
 * action is needed if the bit parameter is beyond the length.
 * Note that this function does not lock the bitmask, but assumes that
 * it is under the exclusive control of the caller.  If you want to use the
 * bitmask spinlock to protect access while clearing the bit, then call
 * the alternative bitmask_clear.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to clear
 * @return int          1 if the bitmask is all clear after the operation, else 0.
 */
int
bitmask_clear_without_spinlock(GWBITMASK *bitmask, int bit)
{
    unsigned char *ptr = bitmask->bits;
    int i;

    if (bit < bitmask->length)
    {
        if (bit >= 8)
        {
            ptr += (bit / 8);
            bit = bit % 8;
        }
        *ptr &= bitmapclear[bit];
    }
    ptr = bitmask->bits;
    for (i = 0; i < bitmask->size; i++)
    {
        if (*(ptr+i) != 0)
        {
            return 0;
        }
    }
    return 1;
}

/**
 * Clear the bit at the specified bit position in the bitmask using a spinlock.
 * See bitmask_clear_without_spinlock for more details
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to clear
 * @return int          1 if the bitmask is all clear after the operation, else 0
 */
int
bitmask_clear(GWBITMASK *bitmask, int bit)
{
    int result;

    spinlock_acquire(&bitmask->lock);
    result = bitmask_clear_without_spinlock(bitmask, bit);
    spinlock_release(&bitmask->lock);
    return result;
}

/**
 * Return a non-zero value if the bit at the specified bit
 * position in the bitmask is set. If the specified bit is outside the
 * bitmask, it is assumed to be unset; the bitmask is not extended.
 * This function wraps bitmask_isset_without_spinlock with a spinlock.
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
 * lock on the bitmask or having control of it in some other way.
 *
 * Bits beyond the current length are deemed unset.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to test
 */
static int
bitmask_isset_without_spinlock(GWBITMASK *bitmask, int bit)
{
    unsigned char *ptr = bitmask->bits;

    if (bit >= bitmask->length)
    {
        return 0;
    }
    if (bit >= 8)
    {
        ptr += (bit / 8);
        bit = bit % 8;
    }
    return *ptr & bitmapset[bit];
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
    unsigned char *ptr = bitmask->bits;
    int i;
    int result = 1;

    spinlock_acquire(&bitmask->lock);
    for (i = 0; i < bitmask->size; i++)
    {
        if (*(ptr+i) != 0)
        {
            result = 0;
            break;
        }
    }
    spinlock_release(&bitmask->lock);

    return result;
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
    if ((dest->bits = malloc(src->size)) == NULL)
    {
        dest->length = 0;
    }
    else
    {
        dest->length = src->length;
        dest->size = src->size;
        memcpy(dest->bits, src->bits, src->size);
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
 * @return int          Number of set bits
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
