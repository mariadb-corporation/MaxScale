#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file cdefs.h
 *
 * This file has several purposes.
 *
 * - Its purpose is the same as that of x86_64-linux-gnu/sys/cdefs.h, that is,
 *   it defines things that are dependent upon the compilation environment.
 * - Since this *must* be included as the very first header by all other MaxScale
 *   headers, it allows you to redefine things globally, should that be necessary,
 *   for instance, when debugging something.
 * - Global constants applicable across the line can be defined here.
 */

#ifdef  __cplusplus
# define MXS_BEGIN_DECLS extern "C" {
# define MXS_END_DECLS   }
#else
# define MXS_BEGIN_DECLS
# define MXS_END_DECLS
#endif

#undef _GNU_SOURCE
#define _GNU_SOURCE 1

#undef OPENSSL_THREAD_DEFINES
#define OPENSSL_THREAD_DEFINES 1

/**
 * Fix compile errors for PRId64
 * in Centos 6
 */
#ifndef __STDC_FORMAT_MACROS
# define __STDC_FORMAT_MACROS
#endif

/**
 * Needs to be defined for INT32_MAX and others
 */
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

/**
 * Define intended for use with strerror.
 *
 * char errbuf[MXS_STRERROR_BUFLEN];
 * strerror_r(errno, errbuf, sizeof(errbuf))
 */
#define MXS_STRERROR_BUFLEN 512

/**
 * Returns the smaller of two items.
 *
 * @param a A value.
 * @param b Another value.
 *
 * @return a if a is smaller than b, b otherwise.
 *
 * @note This a macro, so the arguments will be evaluated more than once.
 */
#define MXS_MIN(a,b) ((a)<(b) ? (a) : (b))

/**
 * Returns the larger of two items.
 *
 * @param a A value.
 * @param b Another value.
 *
 * @return a if a is larger than b, b otherwise.
 *
 * @note This a macro, so the arguments will be evaluated more than once.
 */
#define MXS_MAX(a,b) ((a)>(b) ? (a) : (b))

/**
 * Define function attributes
 *
 * The function attributes are compiler specific.
 */
#ifdef __GNUC__
#define mxs_attribute(a) __attribute__(a)
#else
#define mxs_attribute(a)
#endif

/**
 * COMMON INCLUDE FILES
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
