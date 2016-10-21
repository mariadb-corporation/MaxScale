#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file routing.h - Common definitions and declarations for routers and filters.
 */

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * Routing capability type. Indicates what kind of input a router or
 * a filter accepts.
 *
 * @note The values of the capabilities here *must* be between 0x0000
 *       and 0x8000, that is, bits 0 to 15.
 */
typedef enum routing_capability
{
    RCAP_TYPE_STMT_INPUT = 0x0001, /**< Statement per buffer. */
} routing_capability_t;

#define RCAP_TYPE_NONE 0

MXS_END_DECLS

