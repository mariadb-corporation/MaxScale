#ifndef _PLATFORM_H
#define _PLATFORM_H
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
 * Copyright MariaDB Corporation Ab 2016
 */

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

#endif // _PLATFORM_H
