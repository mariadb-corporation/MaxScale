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

/**
 * @file blr_cache.c - binlog router cache, manage the binlog cache
 *
 * The binlog router is designed to be used in replication environments to
 * increase the replication fanout of a master server. It provides a transparant
 * mechanism to read the binlog entries for multiple slaves while requiring
 * only a single connection to the actual master to support the slaves.
 *
 * The current prototype implement is designed to support MySQL 5.6 and has
 * a number of limitations. This prototype is merely a proof of concept and
 * should not be considered production ready.
 */

#include "blr.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/service.hh>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxbase/atomic.h>
#include <maxscale/dcb.h>

#include <maxscale/log.h>


/**
 * Initialise the cache for this instanceof the binlog router. As a side
 * effect also determine the binlog file to read and the position to read
 * from.
 *
 * @param   router      The router instance
 */
void blr_init_cache(ROUTER_INSTANCE* router)
{
}
