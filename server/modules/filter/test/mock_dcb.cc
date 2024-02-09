/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
         const char* zHost,
         Handler* pHandler)
    : ClientDCB(DCB::FD_CLOSED, zHost, DCB::Role::CLIENT, pSession)
    , m_protocol(pHandler)
{
    m_protocol.set_dcb(this);
}

Dcb::~Dcb()
{
}

Dcb::Handler* Dcb::handler() const
{
    return m_protocol.handler();
}

Dcb::Handler* Dcb::set_handler(Handler* pHandler)
{
    return m_protocol.set_handler(pHandler);
}

bool Dcb::Protocol::write(GWBUF&& data)
{
    int32_t rv = 1;

    if (m_pHandler)
    {
        rv = m_pHandler->write(std::move(data));
    }

    return rv;
}
}
}
