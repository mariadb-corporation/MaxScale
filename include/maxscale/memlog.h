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
 * @file memlog.h The memory logging mechanism
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 26/09/14     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

typedef enum { ML_INT, ML_LONG, ML_LONGLONG, ML_STRING } MEMLOGTYPE;

typedef struct memlog
{
    char            *name;
    SPINLOCK        lock;
    void            *values;
    int             offset;
    int             size;
    MEMLOGTYPE      type;
    unsigned int    flags;
    unsigned int    iflags;
    struct memlog   *next;
} MEMLOG;

/*
 * MEMLOG flag bits
 */
#define MLNOAUTOFLUSH           0x0001

/*
 * MEMLOG internal flags
 */
#define MLWRAPPED               0x0001


extern MEMLOG *memlog_create(char *, MEMLOGTYPE, int);
extern void    memlog_destroy(MEMLOG *);
extern void    memlog_set(MEMLOG *, unsigned int);
extern void    memlog_log(MEMLOG *, void *);
extern void    memlog_flush_all();
extern void    memlog_flush(MEMLOG *);

MXS_END_DECLS
