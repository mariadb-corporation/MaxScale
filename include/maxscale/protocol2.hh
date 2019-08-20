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

/**
 * Client protocol class
 */
class ClientProtocol : public MXS_PROTOCOL_SESSION
{
public:
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

    static void finish_connection(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        client_protocol->finish_connection(dcb);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ClientProtocolApi<ProtocolImplementation>::s_api =
{
    &ClientProtocolApi<ProtocolImplementation>::create_session,
    nullptr,
    &ClientProtocolApi<ProtocolImplementation>::auth_default,
    &ClientProtocolApi<ProtocolImplementation>::reject,
};

template<class ProtocolImplementation>
class BackendProtocolApi
{
public:
    BackendProtocolApi() = delete;
    BackendProtocolApi(const BackendProtocolApi&) = delete;
    BackendProtocolApi& operator=(const BackendProtocolApi&) = delete;

    static GWBUF* reject(const char* host)
    {
        return ProtocolImplementation::reject(host);
    }

    static mxs::BackendProtocol* create_backend_session(
            MXS_SESSION* session, SERVER* server, MXS_PROTOCOL_SESSION* client_protocol_session,
            mxs::Component* component)
    {
        return ProtocolImplementation::create_backend_session(session, server, client_protocol_session,
                                                              component);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API BackendProtocolApi<ProtocolImplementation>::s_api =
{
        nullptr,
        &BackendProtocolApi<ProtocolImplementation>::create_backend_session,
        nullptr,
        nullptr,
};

}
