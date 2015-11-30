#ifndef _MEMLOG_H
#define _MEMLOG_H
/*
 * This file is distributed as part of MariaDB MaxScale.  It is free
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
 * Copyright MariaDB Ab 2014
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
#include <spinlock.h>

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

#endif
