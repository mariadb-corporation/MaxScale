#ifndef _MODUTIL_H
#define _MODUTIL_H
/*
 * This file is distributed as part of MaxScale from SkySQL.  It is free
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
 * Copyright SkySQL Ab 2014
 */

/**
 * @file modutil.h A set of useful routines for module writers
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 04/06/14	Mark Riddoch	Initial implementation
 * 24/06/14	Mark Riddoch	Add modutil_MySQL_Query to enable multipacket queries
 *
 * @endverbatim
 */
#include <buffer.h>

extern int	modutil_is_SQL(GWBUF *);
extern int	modutil_extract_SQL(GWBUF *, char **, int *);
extern int	modutil_MySQL_Query(GWBUF *, char **, int *, int *);
extern GWBUF	*modutil_replace_SQL(GWBUF *, char *);
char*           modutil_get_query(GWBUF* buf);

#endif
