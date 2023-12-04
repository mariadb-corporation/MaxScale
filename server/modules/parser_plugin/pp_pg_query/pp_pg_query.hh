/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "pp_pg_query"
#include <maxscale/ccdefs.hh>
#include <pg_query.h>
extern "C"
{
#include <src/pg_query_internal.h>
#include <catalog/pg_class.h>
#include <parser/parser.h>
}
#include <maxbase/assert.hh>

// For development
#define ASSERT_ON_NOT_HANDLED
#undef ASSERT_ON_NOT_HANDLED

#if defined(ASSERT_ON_NOT_HANDLED)
#define nhy_assert() mxb_assert(!true);
#else
#define nhy_assert()
#endif
