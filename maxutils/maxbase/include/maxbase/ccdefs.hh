/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

/**
 * @file ccdefs.hh
 *
 * This file is to be included first by all C++ headers.
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

#if !defined (__cplusplus)
#error This file is only to be included by C++ code.
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
 * Define function attributes
 *
 * The function attributes are compiler specific.
 */
#ifdef __GNUC__
#define mxb_attribute(a) __attribute__ (a)
#else
#define mxb_attribute(a)
#endif

/**
 * Macro for exposing or hiding symbols
 *
 * The MXB_API should be used by modules to export the symbols that are needed. In practice the only symbol
 * that needs to be exported is the MXS_CREATE_MODULE one that returns the module instance.
 *
 * The MXB_PRIVATE can be used to hide the symbols of e.g. C++ classes defined in headers.
 */
#define MXB_API     __attribute__ ((visibility ("default")))
#define MXB_PRIVATE __attribute__ ((visibility ("hidden")))

/**
 * COMMON INCLUDE FILES
 */
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <chrono>

using namespace std::string_literals;
using namespace std::chrono_literals;

/**
 * All classes of MaxBase are defined in the namespace @c maxbase.
 */
namespace maxbase
{
}

/**
 * Shorthand for the @c maxscale namespace.
 */
namespace mxb = maxbase;
