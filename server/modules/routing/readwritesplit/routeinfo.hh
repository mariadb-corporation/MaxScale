#pragma once
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

class RWSplitSession;

struct RouteInfo
{
    RouteInfo(RWSplitSession* rses, GWBUF* buffer);

    route_target_t target;  /**< Route target type, TARGET_UNDEFINED for unknown */
    uint8_t        command; /**< The command byte, 0xff for unknown commands */
    uint32_t       type;    /**< The query type, QUERY_TYPE_UNKNOWN for unknown types*/
    uint32_t       stmt_id; /**< Prepared statement ID, 0 for unknown */
};
