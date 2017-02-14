#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

#if !defined(__cplusplus)

#if __STDC_VERSION__ >= 201112

#if defined(__STDC_NO_THREADS__)
#define thread_local _Thread_local
#else
#include <threads.h>
#endif

#else // __STDC_VERSION >= 201112

#if defined(__GNUC__)
#define thread_local __thread
#else
#error Do not know how to define thread_local on this compiler/OS platform.
#endif

#endif

#else // __cplusplus

// C++11 supports thread_local natively.
#if __cplusplus < 201103

#if defined(__GNUC__)
#define thread_local __thread
#else
#error Do not know how to define thread_local on this compiler/OS platform.
#endif

#endif // __cplusplus < 201103

#endif // __cplusplus

MXS_END_DECLS
