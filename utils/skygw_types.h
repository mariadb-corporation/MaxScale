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
#if !defined(SKYGW_TYPES_H)
#define SKYGW_TYPES_H

#include <math.h>
#include <stdbool.h>
#include <ctype.h>

#define SECOND_USEC (1024*1024L)
#define MSEC_USEC   (1024L)

#define KILOBYTE_BYTE (1024L)
#define MEGABYTE_BYTE (1024*1024L)
#define GIGABYTE_BYTE (1024*1024*1024L)

#define KB KILOBYTE_BYTE
#define MB MEGABYTE_BYTE
#define GB GIGABYTE_BYTE

#define CALCLEN(i) ((size_t)(floor(log10(abs(i))) + 1))

#define UINTLEN(i) (i<10 ? 1 : (i<100 ? 2 : (i<1000 ? 3 : CALCLEN(i))))

#if !defined(PATH_MAX)
# if defined(__USE_POSIX)
#   define PATH_MAX _POSIX_PATH_MAX
# else
#   define PATH_MAX 256
# endif
#endif

#define MAX_ERROR_MSG PATH_MAX
#define array_nelems(a) ((uint)(sizeof(a)/sizeof(a[0])))

#endif /* SKYGW_TYPES_H */
