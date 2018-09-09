/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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
 */

#if !defined (__cplusplus)
#error This file is only to be included by C++ code.
#endif

#include <maxbase/cdefs.h>
#include <exception>
#include <new>

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
