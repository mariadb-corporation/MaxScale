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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <maxscale/gwbitmask.h>
#include <maxscale/alloc.h>

/**
 * @file gwbitmask.c  Implementation of bitmask operations for the gateway
 *
 * GWBITMASK is a fixed size bitmask with space for 256 bits.
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

static const unsigned char bitmapclear[8] =
{
    255 - 1, 255 - 2, 255 - 4, 255 - 8, 255 - 16, 255 - 32, 255 - 64, 255 - 128
};
static const unsigned char bitmapset[8] =
{
    1, 2, 4, 8, 16, 32, 64, 128
};

/**
 * Initialise a bitmask
 *
 * @param bitmask       Pointer the bitmask
 */
void
bitmask_init(GWBITMASK *bitmask)
{
    spinlock_init(&bitmask->lock);
    memset(bitmask->bits, 0, MXS_BITMASK_SIZE);
}

/**
 * Free a bitmask that is no longer required
 *
 * @param bitmask
 */
void
bitmask_free(GWBITMASK *bitmask)
{
}

/**
 * Set the bit at the specified bit position in the bitmask.
 *
 * @param bitmask       Pointer the bitmask
 * @param bit           Bit to set
 * @return              1 if the bit could be set, 0 otherwise.
 *                      Setting a bit may fail only if the bit exceeds
 *                      the maximum length of the bitmask.
 */
int
bitmask_set(GWBITMASK *bitmask, int bit)
{
    ss_dassert(bit >= 0);

    spinlock_acquire(&bitmask->lock);

    if (bit < MXS_BITMASK_LENGTH)
    {
        unsigned char *ptr = bitmask->bits;

        if (bit >= 8)
        {
            ptr += (bit / 8);
            bit = bit % 8;
        }

        *ptr |= bitmapset[bit];
    }

    spinlock_release(&bitmask->lock);

    return bit < MXS_BITMASK_LENGTH;
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
    ss_dassert(bit >= 0);

    unsigned char *ptr = bitmask->bits;

    if (bit < MXS_BITMASK_LENGTH)
    {
        if (bit >= 8)
        {
            ptr += (bit / 8);
            bit = bit % 8;
        }
        *ptr &= bitmapclear[bit];
    }
    ptr = bitmask->bits;
    for (int i = 0; i < MXS_BITMASK_SIZE; i++)
    {
        if (*(ptr + i) != 0)
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
    ss_dassert(bit >= 0);

    if (bit >= MXS_BITMASK_LENGTH)
    {
        return 0;
    }

    unsigned char *ptr = bitmask->bits;

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
    int result = 1;

    spinlock_acquire(&bitmask->lock);
    for (int i = 0; i < MXS_BITMASK_SIZE; i++)
    {
        if (*(ptr + i) != 0)
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
 * @param dest  Bitmap tp update
 * @param src   Bitmap to copy
 */
void
bitmask_copy(GWBITMASK *dest, GWBITMASK *src)
{
    spinlock_acquire(&src->lock);
    spinlock_acquire(&dest->lock);
    memcpy(dest->bits, src->bits, MXS_BITMASK_SIZE);
    spinlock_release(&dest->lock);
    spinlock_release(&src->lock);
}

/**
 * Return a comma separated list of the numbers of the bits that are set in
 * a bitmask, numbering starting at zero. The returned string must be
 * freed by the caller (unless it is null on account of memory allocation
 * failure).
 *
 * @param bitmask       Bitmap to make readable
 * @return pointer to the newly allocated string, or null if no memory
 */
char *
bitmask_render_readable(GWBITMASK *bitmask)
{
    static const char empty[] = "No bits are set";
    char *result;

    spinlock_acquire(&bitmask->lock);
    int count_set = bitmask_count_bits_set(bitmask);
    if (count_set)
    {
        result = MXS_MALLOC(1 + (4 * count_set));
        if (result)
        {
            result[0] = 0;
            for (int i = 0; i < MXS_BITMASK_LENGTH; i++)
            {
                if (bitmask_isset_without_spinlock(bitmask, i))
                {
                    char onebit[5];
                    sprintf(onebit, "%d,", i);
                    strcat(result, onebit);
                }
            }
            result[strlen(result) - 1] = 0;
        }
    }
    else
    {
        result = MXS_MALLOC(sizeof(empty));
        if (result)
        {
            strcpy(result, empty);
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
    static const unsigned char oneBits[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    unsigned char partresults;
    int result = 0;
    unsigned char *ptr, *eptr;

    ptr = bitmask->bits;
    eptr = ptr + MXS_BITMASK_SIZE;
    while (ptr < eptr)
    {
        partresults = oneBits[*ptr & 0x0f];
        partresults += oneBits[*ptr >> 4];
        result += partresults;
        ptr++;
    }
    return result;
}
