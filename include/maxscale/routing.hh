/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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
#include <maxscale/target.hh>

namespace maxscale
{
/**
 * mxs::Routable is the base type representing the session related data of a particular routing module
 * instance. Implemented by filter and router sessions.
 */
class Routable
{
public:
    virtual ~Routable() = default;

    /**
     * Called when a packet is traveling downstream, towards a backend.
     *
     * @param pPacket Packet to route
     * @return 1 for success, 0 for error
     */
    virtual int32_t routeQuery(GWBUF* pPacket) = 0;

    /**
     * Called when a packet is traveling upstream, towards the client.
     *
     * @param pPacket Packet to route
     * @param down Response source
     * @param reply Reply information
     * @return 1 for success, 0 for error
     */
    virtual int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) = 0;
};
}

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
    /**< Statements are delivered one per buffer (currently always on). */
    RCAP_TYPE_STMT_INPUT = 0x0001,      /* 0b0000000000000001 */
    /**< Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_INPUT (currently always on). */
    RCAP_TYPE_CONTIGUOUS_INPUT = 0x0003,    /* 0b0000000000000011 */
    /**< The transaction state and autocommit mode of the session are tracked;
     *    implies RCAP_TYPE_CONTIGUOUS_INPUT and RCAP_TYPE_STMT_INPUT. */
    RCAP_TYPE_TRANSACTION_TRACKING = 0x0007,    /* 0b0000000000000111 */
    /**< Responses are delivered one per buffer. */
    RCAP_TYPE_STMT_OUTPUT = 0x0010,     /* 0b0000000000010000 */
    /**< Each delivered buffer is contiguous; implies RCAP_TYPE_STMT_OUTPUT. */
    RCAP_TYPE_CONTIGUOUS_OUTPUT = 0x0030,   /* 0b0000000000110000 */
    /** Result sets are delivered in one buffer; implies RCAP_TYPE_STMT_OUTPUT. */
    RCAP_TYPE_RESULTSET_OUTPUT = 0x0050,    /* 0b0000000001110000 */
    /** Results are delivered as a set of complete packets */
    RCAP_TYPE_PACKET_OUTPUT = 0x0080,   /* 0b0000000010000000 */
    /** Track session state changes, implies packet output */
    RCAP_TYPE_SESSION_STATE_TRACKING = 0x0180,      /* 0b0000000011000000 */
    /** Request and response tracking: tells when a the response to a query is complete.
     * Implies packet output. */
    RCAP_TYPE_REQUEST_TRACKING = 0x0283,    /* 0b0000001010000011 */
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
