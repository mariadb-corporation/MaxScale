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
#include <maxscale/target.hh>

class DCB;
class SERVER;
class GWBUF;
class MXS_SESSION;

/**
 * Base protocol class. Implemented by both client and backend protocols
 */
class MXS_PROTOCOL_SESSION
{
public:
    virtual ~MXS_PROTOCOL_SESSION() = default;

    /**
     * EPOLLIN handler, used to read available data from network socket
     *
     * @param dcb DCB to read from
     * @return 1 on success, 0 on error
     */
    virtual int32_t read(DCB* dcb) = 0;
    virtual int32_t write(DCB* dcb, GWBUF* buffer) = 0;

    /**
     * EPOLLOUT handler, used to write buffered data
     *
     * @param dcb DCB to write to
     * @return 1 on success, 0 on error
     * @note Currently the return value is ignored
     */
    virtual int32_t write_ready(DCB* dcb) = 0;

    /**
     * EPOLLERR handler
     *
     * @param dcb DCB for which the error occurred
     * @return 1 on success, 0 on error
     * @note Currently the return value is ignored
     */
    virtual int32_t error(DCB* dcb) = 0;

    /**
     * EPOLLHUP and EPOLLRDHUP handler
     *
     * @param dcb DCB for which the hangup occurred
     * @return 1 on success, 0 on error
     * @note Currently the return value is ignored
     */
    virtual int32_t hangup(DCB* dcb) = 0;

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
     * Write data to a network socket
     *
     * @param dcb    DCB to write to
     * @param buffer Buffer to write
     *
     * @return 1 on success, 0 on error
     */
    int32_t (* write)(DCB* dcb, GWBUF* buffer);

    /**
     * Allocate new client protocol session
     *
     * @param session   The session to which the connection belongs to
     * @param component The component to use for routeQuery
     *
     * @return New protocol session or null on error
     */
    MXS_PROTOCOL_SESSION* (* new_client_session)(MXS_SESSION* session, mxs::Component* component);

    /**
     * Allocate new backend protocol session
     *
     * @param session  The session to which the connection belongs to
     * @param server   Server where the connection is made
     * @param protocol The client protocol session.
     *
     * @return New protocol session or null on error
     */
    MXS_PROTOCOL_SESSION* (* new_backend_session)(MXS_SESSION* session,
                                                  SERVER* server,
                                                  MXS_PROTOCOL_SESSION* client_protocol_session,
                                                  mxs::Component* component);

    /**
     * Initialize a connection (client or server).
     *
     * @param dcb  The connection to be initialized.
     *
     * @return True, if the connection could be initialized, false otherwise.
     */
    bool (* init_connection)(DCB* dcb);

    /**
     * Finalize a connection (client or server).
     *
     * Called right before the DCB itself is closed.
     *
     * @param dcb  The connection to be finalized.
     */
    void (* finish_connection)(DCB* dcb);

    /**
     * Returns the name of the default authenticator module for this protocol
     *
     * @return The name of the default authenticator
     */
    char* (* auth_default)();

    /**
     * Handle connection limits
     *
     * @param dcb   DCB to handle
     * @param limit Maximum number of connections
     *
     * @return 1 on success, 0 on error
     *
     * @note Currently the return value is ignored
     */
    int32_t (* connlimit)(DCB* dcb, int limit);

    /**
     * Check if the connection has been fully established, used by connection pooling
     *
     * @param dcb DCB to check
     *
     * @return True if the connection is fully established and can be pooled
     */
    bool (* established)(DCB*);

    /**
     * Get rejection message
     *
     * The protocol should return an error indicating that access to MaxScale has been temporarily suspended.
     *
     * @param host The host that is blocked
     *
     * @return A buffer containing the error message
     */
    GWBUF* (* reject)(const char* host);
};

/**
 * The MXS_PROTOCOL version data. The following should be updated whenever
 * the MXS_PROTOCOL structure is changed. See the rules defined in modinfo.h
 * that define how these numbers should change.
 */
#define MXS_PROTOCOL_VERSION {3, 1, 0}

/**
 * Specifies capabilities specific for protocol.
 *
 * @see enum routing_capability
 *
 * @note The values of the capabilities here *must* be between 0x010000000000
 *       and 0x800000000000, that is, bits 40 to 47.
 */
enum protocol_capability_t
{
    PCAP_TYPE_NONE = 0x0    // TODO: remove once protocol capabilities are defined
};
