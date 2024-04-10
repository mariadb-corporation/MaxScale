/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file protocol.hh
 *
 * The protocol module interface definition.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/dcbhandler.hh>
#include <maxscale/target.hh>

class DCB;
class SERVER;
class GWBUF;
class MXS_SESSION;

namespace maxscale
{
class Listener;
class ProtocolModule;
class ClientConnection;
class BackendConnection;

/**
 * Base protocol class. Implemented by both client and backend protocols
 */
class ProtocolConnection : public DCBHandler
{
public:
    virtual ~ProtocolConnection() = default;

    /**
     * Print connection diagnostics to json.
     *
     * @return JSON representation of the connection
     */
    virtual json_t* diagnostics() const = 0;

    virtual void set_dcb(DCB* dcb) = 0;

    /**
     * Can the connection be moved to another thread.
     *
     * @return True if connection can be moved
     */
    virtual bool is_movable() const
    {
        return true;
    }

    /**
     * Is the connection idle.
     *
     * This method is called when the associated session is about to be modified. If the connection is
     * logically idle, meaning no queries are ongoing and no results are expected, the session will be
     * modified. If the connection is not idle, the modification is postponed until the connection is idle.
     *
     * Note that for the client protocol, this will always return true inside the routeQuery and clientReply
     * functions. This happens as the client protocol stops being idle the moment the routeQuery is called and
     * becomes idle only after all results have been read.
     *
     * TODO: This should be changed so that the session is idle for the duration of routeQuery and only goes
     *       non-idle when the routeQuery successfully returns. This is currently must done this way as some
     *       modules call clientReply directly from routeQuery.
     *
     * @return True if the connection is idle.
     */
    virtual bool is_idle() const
    {
        return true;
    }

    /**
     * Size of internal buffers.
     *
     * @return The size of internal buffers in bytes.
     */
    virtual size_t sizeof_buffers() const = 0;
};
}
/**
 * Protocol module API
 */
struct MXS_PROTOCOL_API
{
    /**
     * Creates a new protocol module instance.
     *
     * @param name      The name of the listener for which this protocol is created
     * @param listener  The listener for which the protocol module is created.
     *
     * @return New protocol module instance
     */
    mxs::ProtocolModule* (* create_protocol_module)(const std::string& name, mxs::Listener* listener);
};

/**
 * The MXS_PROTOCOL version data. The following should be updated whenever
 * the MXS_PROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_PROTOCOL_VERSION {4, 0, 0}
