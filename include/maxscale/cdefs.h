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

#include <maxbase/cdefs.h>

#define MXS_BEGIN_DECLS MXB_BEGIN_DECLS
#define MXS_END_DECLS   MXB_END_DECLS

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
#define mxs_attribute mxb_attribute
