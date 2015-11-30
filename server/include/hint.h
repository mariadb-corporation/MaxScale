#ifndef _HINT_H
#define _HINT_H
/*
 * This file is distributed as part of MaxScale.  It is free
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
 * @file hint.h The generic hint data that may be attached to buffers
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 10/07/14     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */

#include <skygw_debug.h>


/**
 * The types of hint that are supported by the generic hinting mechanism.
 */
typedef enum
{
    HINT_ROUTE_TO_MASTER = 1,
    HINT_ROUTE_TO_SLAVE,
    HINT_ROUTE_TO_NAMED_SERVER,
    HINT_ROUTE_TO_UPTODATE_SERVER,
    HINT_ROUTE_TO_ALL, /*< not implemented yet */
    HINT_PARAMETER
} HINT_TYPE;

/**
 * A generic hint.
 *
 * A hint has a type associated with it and may optionally have hint
 * specific data.
 * Multiple hints may be attached to a single buffer.
 */
typedef struct hint
{
    HINT_TYPE       type;   /*< The Type of hint */
    void            *data;  /*< Type specific data */
    void            *value; /*< Parameter value for hint */
    unsigned int    dsize;  /*< Size of the hint data */
    struct hint     *next;  /*< Another hint for this buffer */
} HINT;

extern  HINT    *hint_alloc(HINT_TYPE, void *, unsigned int);
extern  HINT    *hint_create_parameter(HINT *, char *, char *);
extern  HINT    *hint_create_route(HINT *, HINT_TYPE, char *);
extern  void    hint_free(HINT *);
extern  HINT    *hint_dup(HINT *);
bool            hint_exists(HINT **, HINT_TYPE);
#endif
