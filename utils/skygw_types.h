/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

#include <math.h>

#if !defined(SKYGW_TYPES_H)
#define SKYGW_TYPES_H

#define SECOND_USEC (1024*1024L)
#define MSEC_USEC   (1024L)

#define KILOBYTE_BYTE (1024L)
#define MEGABYTE_BYTE (1024*1024L)
#define GIGABYTE_BYTE (1024*1024*1024L)

#define KB KILOBYTE_BYTE
#define MB MEGABYTE_BYTE
#define GB GIGABYTE_BYTE

#define CALCLEN(i) (floor(log10(abs(i))) + 1)

#define UINTLEN(i) (i<10 ? 1 : (i<100 ? 2 : (i<1000 ? 3 : CALCLEN(i))))

#if defined(__cplusplus) && !defined(TRUE) && !defined(FALSE)
# define TRUE true
# define FALSE false
#elif !defined(TRUE) && !defined(FALSE)
typedef enum {FALSE=0, TRUE} bool;
#else
# if !defined(bool)
#  define bool int
# endif
#endif

#endif /* SKYGW_TYPES_H */
