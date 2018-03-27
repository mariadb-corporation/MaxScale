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

#include "readwritesplit.hh"
#include "rwsplit_internal.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/modutil.h>
#include <maxscale/alloc.h>
#include <maxscale/router.h>

/**
 * The functions that carry out checks on statements to see if they involve
 * various operations involving temporary tables or multi-statement queries.
 */

/*
 * The following are to do with checking whether the statement refers to
 * temporary tables, or is a multi-statement request. Maybe they belong
 * somewhere else, outside this router. Perhaps in the query classifier?
 */

