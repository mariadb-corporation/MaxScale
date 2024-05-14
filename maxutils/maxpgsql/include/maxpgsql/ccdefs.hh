/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

/**
 * @file ccdefs.hh
 *
 * This file should be included first by all maxpgsql headers.
 */

#if !defined (__cplusplus)
#error This file is only to be included by C++ code.
#endif

#include <maxbase/ccdefs.hh>

/**
 * All classes of MaxPgSql are defined in the namespace @c maxpgsql.
 */
namespace maxpgsql
{
}

/**
 * Shorthand for the @c maxsql namespace.
 */
namespace mxp = maxpgsql;
