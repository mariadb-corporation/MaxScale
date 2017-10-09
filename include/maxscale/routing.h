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
    /**< Statements are delivered one per buffer. */
    RCAP_TYPE_STMT_INPUT           = 0x0001, /* 0b0000000000000001 */
    /**< Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_INPUT. */
    RCAP_TYPE_CONTIGUOUS_INPUT     = 0x0003, /* 0b0000000000000011 */
    /**< The transaction state and autocommit mode of the session are tracked;
         implies RCAP_TYPE_CONTIGUOUS_INPUT and RCAP_TYPE_STMT_INPUT. */
    RCAP_TYPE_TRANSACTION_TRACKING = 0x0007, /* 0b0000000000000111 */
    /**< Responses are delivered one per buffer. */
    RCAP_TYPE_STMT_OUTPUT           = 0x0010, /* 0b0000000000010000 */
    /**< Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_OUTPUT. */
    RCAP_TYPE_CONTIGUOUS_OUTPUT     = 0x0030, /* 0b0000000000110000 */
    /** Result sets are delivered in one buffer; implies RCAP_TYPE_STMT_OUTPUT. */
    RCAP_TYPE_RESULTSET_OUTPUT      = 0x0050, /* 0b0000000001110000 */
    /** Results are delivered as a set of complete packets */
    RCAP_TYPE_PACKET_OUTPUT         = 0x0080, /* 0b0000000010000000 */

} mxs_routing_capability_t;

#define RCAP_TYPE_NONE 0

/**
 * Determines whether a particular capability type is required.
 *
 * @param capabilites The capability bits to be tested.
 * @param type        A particular capability type or a bitmask of types.
 *
 * @return True, if @c type is present in @c capabilities.
 */
static inline bool rcap_type_required(uint64_t capabilities, uint64_t type)
{
    return (capabilities & type) == type;
}

MXS_END_DECLS

