/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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
#include <maxscale/dcb.hh>
#include <maxscale/target.hh>

class DCB;
class SERVER;
class GWBUF;
class MXS_SESSION;

namespace maxscale
{
class ProtocolModule;
class ClientProtocol;
class BackendProtocol;
}

/**
 * Base protocol class. Implemented by both client and backend protocols
 */
class MXS_PROTOCOL_SESSION : public DCB::Handler
{
public:
    virtual ~MXS_PROTOCOL_SESSION() = default;

    /**
     * Write data to a network socket
     *
     * @param dcb    DCB to write to
     * @param buffer Buffer to write
     * @return 1 on success, 0 on error
     */
    virtual int32_t write(DCB* dcb, GWBUF* buffer) = 0;

    /**
     * Provide JSON formatted diagnostics about a DCB
     *
     * @param dcb DCB to diagnose
     * @return JSON representation of the DCB
     */
    virtual json_t* diagnostics_json(DCB* dcb)
    {
        return nullptr;
    }
};

/**
 * Protocol module API
 */
struct MXS_PROTOCOL_API
{
    /**
     * Creates a new protocol module instance.
     *
     * @return New protocol module instance
     */
    mxs::ProtocolModule* (* create_protocol_module)();
};

/**
 * The MXS_PROTOCOL version data. The following should be updated whenever
 * the MXS_PROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_PROTOCOL_VERSION {3, 1, 0}
