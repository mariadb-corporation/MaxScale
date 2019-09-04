/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "maxscale/mock/dcb.hh"

namespace maxscale
{

namespace mock
{

Dcb::Dcb(MXS_SESSION* pSession,
         const char* zUser,
         const char* zHost,
         Handler* pHandler)
    : ClientDCB(DCB::FD_CLOSED, DCB::Role::CLIENT, pSession)
    , m_user(zUser)
    , m_host(zHost)
    , m_protocol_session(pHandler)
{
    DCB* pDcb = this;

    pDcb->m_remote = MXS_STRDUP(zHost);
    pDcb->m_user = MXS_STRDUP(zUser);
}

Dcb::~Dcb()
{
}

Dcb::Handler* Dcb::handler() const
{
    return m_protocol_session.handler();
}

Dcb::Handler* Dcb::set_handler(Handler* pHandler)
{
    return m_protocol_session.set_handler(pHandler);
}

int32_t Dcb::ProtocolSession::write(DCB*, GWBUF* pData)
{
    int32_t rv = 1;

    if (m_pHandler)
    {
        rv = m_pHandler->write(pData);
    }
    else
    {
        gwbuf_free(pData);
    }

    return rv;
}
}
}
