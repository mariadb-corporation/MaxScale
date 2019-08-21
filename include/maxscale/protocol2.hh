/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>
#include <maxscale/dcb.hh>
#include <maxscale/protocol.hh>

namespace maxscale
{
class ClientProtocol;
class BackendProtocol;


class ProtocolModule
{
    virtual mxs::ClientProtocol* create_client_protocol(MXS_SESSION* session, mxs::Component* component) = 0;
    virtual std::string auth_default() const = 0;
    virtual GWBUF* reject(const std::string& host)
    {
        return nullptr;
    }
};

/**
 * Client protocol class
 */
class ClientProtocol : public MXS_PROTOCOL_SESSION
{
public:
    enum Capabilities
    {
        CAP_BACKEND = (1 << 0) // The protocol supports backend communication
    };

    virtual ~ClientProtocol() = default;

    /**
     * Initialize a connection.
     *
     * @param dcb  The connection to be initialized.
     * @return True, if the connection could be initialized, false otherwise.
     */
    virtual bool init_connection(DCB* dcb) = 0;

    /**
     * Finalize a connection. Called right before the DCB itself is closed.
     *
     * @param dcb  The connection to be finalized.
     */
    virtual void finish_connection(DCB* dcb) = 0;

    /**
     * Handle connection limits. Currently the return value is ignored.
     *
     * @param dcb   DCB to handle
     * @param limit Maximum number of connections
     * @return 1 on success, 0 on error
     */
    virtual int32_t connlimit(DCB* dcb, int limit)
    {
        return 0;
    };

    virtual int64_t capabilities() const
    {
        return 0;
    }

    /**
     * Allocate new backend protocol session
     *
     * @param session  The session to which the connection belongs to
     * @param server   Server where the connection is made
     *
     * @return New protocol session or null on error
     */
    virtual BackendProtocol*
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component)
    {
        mxb_assert(!true);
        return nullptr;
    }
};

/**
 * Backend protocol class
 */
class BackendProtocol : public MXS_PROTOCOL_SESSION
{
public:
    virtual ~BackendProtocol() = default;

    /**
     * Initialize a connection.
     *
     * @param dcb  The connection to be initialized.
     * @return True, if the connection could be initialized, false otherwise.
     */
    virtual bool init_connection(DCB* dcb) = 0;

    /**
     * Finalize a connection. Called right before the DCB itself is closed.
     *
     * @param dcb  The connection to be finalized.
     */
    virtual void finish_connection(DCB* dcb) = 0;

    /**
     * Check if the connection has been fully established, used by connection pooling
     *
     * @param dcb DCB to check
     * @return True if the connection is fully established and can be pooled
     */
    virtual bool established(DCB*) = 0;
};

template<class ProtocolImplementation>
class ClientProtocolApi
{
public:
    ClientProtocolApi() = delete;
    ClientProtocolApi(const ClientProtocolApi&) = delete;
    ClientProtocolApi& operator=(const ClientProtocolApi&) = delete;

    static mxs::ClientProtocol* create_session(MXS_SESSION* session, mxs::Component* component)
    {
        return ProtocolImplementation::create(session, component);
    }

    static char* auth_default()
    {
        return ProtocolImplementation::auth_default();
    }

    static GWBUF* reject(const char* host)
    {
        return ProtocolImplementation::reject(host);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ClientProtocolApi<ProtocolImplementation>::s_api =
{
    &ClientProtocolApi<ProtocolImplementation>::create_session,
    &ClientProtocolApi<ProtocolImplementation>::auth_default,
    &ClientProtocolApi<ProtocolImplementation>::reject,
};

}
