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
 * Base protocol class. Implemented by both client and backend protocols
 */
class ProtocolSession : public MXS_PROTOCOL_SESSION
{
public:
    virtual int32_t read(DCB* dcb) = 0;
    virtual int32_t write(DCB* dcb, GWBUF* buffer) = 0;
    virtual int32_t write_ready(DCB* dcb) = 0;
    virtual int32_t error(DCB* dcb) = 0;
    virtual int32_t hangup(DCB* dcb) = 0;
    virtual json_t* diagnostics_json(DCB* dcb)
    {
        return nullptr;
    }
};

/**
 * Client protocol class
 */
class ClientProtocol : public ProtocolSession
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
class BackendProtocol : public ProtocolSession
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

    static int32_t read(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->read(dcb);
    }

    static int32_t write(DCB* dcb, GWBUF* buffer)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->write(dcb, buffer);
    }

    static int32_t write_ready(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->write_ready(dcb);
    }

    static int32_t error(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->error(dcb);
    }

    static int32_t hangup(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->hangup(dcb);
    }

    static json_t* diagnostics_json(DCB* dcb)
    {
        auto client_dcb = static_cast<ClientDCB*>(dcb);
        auto client_protocol = static_cast<ClientProtocol*>(client_dcb->m_protocol);
        return client_protocol->diagnostics_json(dcb);
    }

    static MXS_PROTOCOL_SESSION* create_session(MXS_SESSION* session, mxs::Component* component)
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

    static void free_session(MXS_PROTOCOL_SESSION* protocol_session)
    {
        delete protocol_session;
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
    &ClientProtocolApi<ProtocolImplementation>::read,
    &ClientProtocolApi<ProtocolImplementation>::write,
    &ClientProtocolApi<ProtocolImplementation>::write_ready,
    &ClientProtocolApi<ProtocolImplementation>::error,
    &ClientProtocolApi<ProtocolImplementation>::hangup,
    &ClientProtocolApi<ProtocolImplementation>::create_session,
    nullptr,
    &ClientProtocolApi<ProtocolImplementation>::free_session,
    &ClientProtocolApi<ProtocolImplementation>::init_connection,
    &ClientProtocolApi<ProtocolImplementation>::finish_connection,
    &ClientProtocolApi<ProtocolImplementation>::auth_default,
    &ClientProtocolApi<ProtocolImplementation>::connlimit,
    &ClientProtocolApi<ProtocolImplementation>::established,
    &ClientProtocolApi<ProtocolImplementation>::diagnostics_json,
    &ClientProtocolApi<ProtocolImplementation>::reject,
};

}
