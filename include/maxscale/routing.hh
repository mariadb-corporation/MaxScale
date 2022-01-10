/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file routing.hh - Common definitions and declarations for routers and filters.
 */

#include <maxscale/ccdefs.hh>

/**
 * Routing capability type. Indicates what kind of input a router or
 * a filter accepts.
 *
 *       The capability bit ranges are:
 *           0-15:  general capability bits
 *           16-23: router specific bits
 *           24-31: filter specific bits
 *           32-39: authenticator specific bits
 *           40-47: protocol specific bits
 *           48-55: monitor specific bits
 *           56-63: reserved for future use
 *
 * @note The values of the capabilities here *must* be between 0x0000
 *       and 0x8000, that is, bits 0 to 15.
 */
typedef enum routing_capability
{
    /**
     * Statements are delivered one per buffer (currently always on).
     *
     * Binary: 0b0000 0000 0000 0001
     */
    RCAP_TYPE_STMT_INPUT = 0x0001,

    /**
     * Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_INPUT (currently always on).
     *
     * Binary: 0b0000 0000 0000 0011
     */
    RCAP_TYPE_CONTIGUOUS_INPUT = 0x0003,

    /**
     * The transaction state and autocommit mode of the session are tracked; implies
     * RCAP_TYPE_CONTIGUOUS_INPUT and RCAP_TYPE_STMT_INPUT.
     *
     * Binary: 0b0000 0000 0000 0111
     */
    RCAP_TYPE_TRANSACTION_TRACKING = 0x0007,

    /**
     * Responses are delivered one per buffer.
     *
     * Binary: 0b0000 0000 0001 0000
     */
    RCAP_TYPE_STMT_OUTPUT = 0x0010,

    /**
     * Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_OUTPUT.
     *
     * Binary: 0b0000 0000 0011 0000
     */
    RCAP_TYPE_CONTIGUOUS_OUTPUT = 0x0030,

    /**
     * Result sets are delivered in one buffer; implies RCAP_TYPE_STMT_OUTPUT.
     *
     * Binary: 0b0000 0000 0111 0000
     */
    RCAP_TYPE_RESULTSET_OUTPUT = 0x0050,

    /**
     * Results are delivered as a set of complete packets
     *
     * Binary: 0b0000 0000 1000 0000
     */
    RCAP_TYPE_PACKET_OUTPUT = 0x0080,

    /**
     * Track session state changes, implies packet output.
     *
     * Binary: 0b0000 0001 1000 0000
     */
    RCAP_TYPE_SESSION_STATE_TRACKING = 0x0180,

    /**
     * Request and response tracking: tells when a the response to a query is complete.
     *
     * Binary: 0b0000 0010 1000 0011
     */
    RCAP_TYPE_REQUEST_TRACKING = 0x0283,
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
