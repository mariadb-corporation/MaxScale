/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 * This file should be included first by all maxsql headers.
 */

#if !defined (__cplusplus)
#error This file is only to be included by C++ code.
#endif

#include <maxbase/ccdefs.hh>

/**
 * All classes of MaxSql are defined in the namespace @c maxsql.
 */
namespace maxsql
{
}

/**
 * Shorthand for the @c maxsql namespace.
 */
namespace mxq = maxsql;
