/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/dcb.hh"

namespace
{

void initialize_dcb(DCB* pDcb)
{
    memset(pDcb, 0, sizeof(DCB));

    pDcb->fd = DCBFD_CLOSED;
    pDcb->state = DCB_STATE_ALLOC;
    pDcb->ssl_state = SSL_HANDSHAKE_UNKNOWN;
}
}

namespace maxscale
{

namespace mock
{

Dcb::Dcb(MXS_SESSION* pSession,
         const char*  zUser,
         const char*  zHost,
         Handler* pHandler)
    : m_user(zUser)
    , m_host(zHost)
    , m_pHandler(pHandler)
{
    DCB* pDcb = this;
    initialize_dcb(this);

    pDcb->session = pSession;
    pDcb->remote = const_cast<char*>(zHost);
    pDcb->user = const_cast<char*>(zUser);

    pDcb->func.write = &Dcb::write;
}

Dcb::~Dcb()
{
}

Dcb::Handler* Dcb::handler() const
{
    return m_pHandler;
}

Dcb::Handler* Dcb::set_handler(Handler* pHandler)
{
    Handler* p = m_pHandler;
    m_pHandler = pHandler;
    return p;
}

int32_t Dcb::write(GWBUF* pData)
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

// static
int32_t Dcb::write(DCB* pDcb, GWBUF* pData)
{
    return static_cast<Dcb*>(pDcb)->write(pData);
}
}
}
