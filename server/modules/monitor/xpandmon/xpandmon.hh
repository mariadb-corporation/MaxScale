/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "xpandmon"

#include <maxscale/ccdefs.hh>
#include <maxbase/log.hh>

const long DEFAULT_CLUSTER_MONITOR_INTERVAL = 60000;
const long DEFAULT_HEALTH_CHECK_THRESHOLD = 2;
const bool DEFAULT_DYNAMIC_NODE_DETECTION = true;
const long DEFAULT_HEALTH_CHECK_PORT = 3581;
