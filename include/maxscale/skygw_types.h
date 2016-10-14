#pragma once
#ifndef _MAXSCALE_SKYGW_TYPES_H
#define _MAXSCALE_SKYGW_TYPES_H
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

#include <maxscale/cdefs.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>

MXS_BEGIN_DECLS

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

MXS_END_DECLS

#endif /* SKYGW_TYPES_H */
