/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
#include <maxscale/config_common.hh>
#include <maxscale/dcbhandler.hh>
#include <maxscale/target.hh>

class DCB;
class SERVER;
class GWBUF;
class MXS_SESSION;

namespace maxscale
{
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
     * Write data to a network socket
     *
     * @param buffer Buffer to write
     * @return 1 on success, 0 on error
     */
    virtual int32_t write(GWBUF* buffer) = 0;

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
     * @return True if the connection is idle.
     */
    virtual bool is_idle() const
    {
        return true;
    }
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
     * @param name The name of the listener for which this protocol is created
     *
     * @return New protocol module instance
     */
    mxs::ProtocolModule* (* create_protocol_module)(const std::string& name);
};

/**
 * The MXS_PROTOCOL version data. The following should be updated whenever
 * the MXS_PROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_PROTOCOL_VERSION {4, 0, 0}
