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
 * @file bitmask.h An implementation of an arbitrarily long bitmask
 */

#include <maxscale/cdefs.h>

#include <maxscale/limits.h>
#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

/* This number MUST an be exact multiple of 8 */
#define MXS_BITMASK_LENGTH     (MXS_MAX_THREADS + 1)    /**< Number of bits in the bitmask */

#define MXS_BITMASK_SIZE       (MXS_BITMASK_LENGTH / 8) /**< Number of bytes in the bitmask */

/**
 * The bitmask structure used to store a fixed size bitmask
 */
typedef struct
{
    SPINLOCK lock;                        /**< Lock to protect the bitmask */
    unsigned char bits[MXS_BITMASK_SIZE]; /**< The bits themselves */
} MXS_BITMASK;

#define MXS_BITMASK_INIT {SPINLOCK_INIT}

void bitmask_init(MXS_BITMASK *);
void bitmask_free(MXS_BITMASK *);
int  bitmask_set(MXS_BITMASK *, int);
int  bitmask_clear(MXS_BITMASK *, int);
int  bitmask_clear_without_spinlock(MXS_BITMASK *, int);
int  bitmask_isset(MXS_BITMASK *, int);
int  bitmask_isallclear(MXS_BITMASK *);
void bitmask_copy(MXS_BITMASK *, MXS_BITMASK *);
char *bitmask_render_readable(MXS_BITMASK *);

MXS_END_DECLS
