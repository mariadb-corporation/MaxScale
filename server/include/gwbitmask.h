#ifndef _GWBITMASK_H
#define _GWBITMASK_H
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
#include <spinlock.h>

/**
 * @file gwbitmask.h An implementation of an arbitrarily long bitmask
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 28/06/13     Mark Riddoch    Initial implementation
 * 17/10/15     Martin Brampton Add bitmask_render_readable
 *
 * @endverbatim
 */

/* Both these numbers MUST be exact multiples of 8 */
#define BIT_LENGTH_INITIAL      256      /**< Initial number of bits in the bitmask */
#define BIT_LENGTH_INC          256      /**< Number of bits to add on each increment */

/**
 * The bitmask structure used to store an arbitrary large bitmask
 */
typedef struct
{
    SPINLOCK lock;          /**< Lock to protect the bitmask */
    unsigned char *bits;    /**< Pointer to the bits themselves */
    int length;          /**< The number of bits in the bitmask */
    int size;            /**< The number of bytes in the bitmask */

} GWBITMASK;

#define GWBITMASK_INIT {SPINLOCK_INIT}

extern void bitmask_init(GWBITMASK *);
extern void bitmask_free(GWBITMASK *);
extern void bitmask_set(GWBITMASK *, int);
extern int  bitmask_clear(GWBITMASK *, int);
extern int  bitmask_clear_without_spinlock(GWBITMASK *, int);
extern int  bitmask_isset(GWBITMASK *, int);
extern int  bitmask_isallclear(GWBITMASK *);
extern void bitmask_copy(GWBITMASK *, GWBITMASK *);
extern char *bitmask_render_readable(GWBITMASK *);

#endif
