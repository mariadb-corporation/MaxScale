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
    virtual bool init_connection(DCB* dcb) = 0;
    virtual void finish_connection(DCB* dcb) = 0;
    virtual int32_t connlimit(DCB* dcb, int limit)
    {
        return 0;
    };

    virtual bool established(DCB*)
    {
        return true;
    }
};

/**
 * Backend protocol class
 */
class BackendProtocol : public MXS_PROTOCOL_SESSION
{
public:
    virtual ~BackendProtocol() = default;
    virtual bool init_connection(DCB* dcb) = 0;
    virtual void finish_connection(DCB* dcb) = 0;
    virtual bool established(DCB*) = 0;
};

template<class ProtocolImplementation>
class ClientProtocolApi
{
public:
    ClientProtocolApi() = delete;
    ClientProtocolApi(const ClientProtocolApi&) = delete;
    ClientProtocolApi& operator=(const ClientProtocolApi&) = delete;

    static int32_t write(DCB* dcb, GWBUF* buffer)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->write(dcb, buffer);
    }

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

    static bool init_connection(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->init_connection(dcb);
    }

    static void finish_connection(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        client_protocol->finish_connection(dcb);
    }

    static int32_t connlimit(DCB* dcb, int limit)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->connlimit(dcb, limit);
    }

    static bool established(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->established(dcb);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API ClientProtocolApi<ProtocolImplementation>::s_api =
{
    &ClientProtocolApi<ProtocolImplementation>::write,
    &ClientProtocolApi<ProtocolImplementation>::create_session,
    nullptr,
    &ClientProtocolApi<ProtocolImplementation>::init_connection,
    &ClientProtocolApi<ProtocolImplementation>::finish_connection,
    &ClientProtocolApi<ProtocolImplementation>::auth_default,
    &ClientProtocolApi<ProtocolImplementation>::connlimit,
    &ClientProtocolApi<ProtocolImplementation>::established,
    &ClientProtocolApi<ProtocolImplementation>::reject,
};

template<class ProtocolImplementation>
class BackendProtocolApi
{
public:
    BackendProtocolApi() = delete;
    BackendProtocolApi(const BackendProtocolApi&) = delete;
    BackendProtocolApi& operator=(const BackendProtocolApi&) = delete;

    static int32_t write(DCB* dcb, GWBUF* buffer)
    {
        auto backend_dcb = static_cast<BackendDCB*>(dcb);
        auto client_protocol = static_cast<BackendProtocol*>(backend_dcb->m_protocol);
        return client_protocol->write(dcb, buffer);
    }

    static char* auth_default()
    {
        return ProtocolImplementation::auth_default();
    }

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

    static bool init_connection(DCB* dcb)
    {
        auto backend_dcb = static_cast<BackendDCB*>(dcb);
        auto client_protocol = static_cast<BackendProtocol*>(backend_dcb->m_protocol);
        return client_protocol->init_connection(dcb);
    }

    static void finish_connection(DCB* dcb)
    {
        auto backend_dcb = static_cast<BackendDCB*>(dcb);
        auto client_protocol = static_cast<BackendProtocol*>(backend_dcb->m_protocol);
        client_protocol->finish_connection(dcb);
    }

    static bool established(DCB* dcb)
    {
        auto backend_dcb = static_cast<BackendDCB*>(dcb);
        auto client_protocol = static_cast<BackendProtocol*>(backend_dcb->m_protocol);
        return client_protocol->established(dcb);
    }

    static MXS_PROTOCOL_API s_api;
};

template<class ProtocolImplementation>
MXS_PROTOCOL_API BackendProtocolApi<ProtocolImplementation>::s_api =
{
        &BackendProtocolApi<ProtocolImplementation>::write,
        nullptr,
        &BackendProtocolApi<ProtocolImplementation>::create_backend_session,
        &BackendProtocolApi<ProtocolImplementation>::init_connection,
        &BackendProtocolApi<ProtocolImplementation>::finish_connection,
        &BackendProtocolApi<ProtocolImplementation>::auth_default,
        nullptr,
        &BackendProtocolApi<ProtocolImplementation>::established,
        nullptr,
};

}
