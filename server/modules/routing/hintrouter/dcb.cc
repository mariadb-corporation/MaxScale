/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "dcb.hh"
#include <maxscale/atomic.h>
#include <maxscale/service.h>


Dcb::Dcb(DCB* pDcb)
    : m_pDcb(pDcb)
    , m_pRefs(NULL)
{
    try
    {
        m_pRefs = new int (1);
    }
    catch (const std::exception&)
    {
        dcb_close(pDcb);
        throw;
    }
}

Dcb::Dcb(const Dcb& rhs)
    : m_pDcb(rhs.m_pDcb)
    , m_pRefs(rhs.m_pRefs)
{
    ++(*m_pRefs);
}

Dcb& Dcb::operator = (Dcb rhs)
{
    swap(rhs);
    return *this;
}

void Dcb::dec()
{
    ss_dassert(*m_pRefs > 0);

    if (--(*m_pRefs) == 0)
    {
        HR_DEBUG("CLOSING dcb");
        // TODO: You should not need to manually adjust any
        // TODO: connections number, dcb_close should handle that.
        SERVER_REF* pSref = m_pDcb->service->dbref;

        while (pSref && (pSref->server != m_pDcb->server))
        {
            pSref = pSref->next;
        }

        if (pSref)
        {
            atomic_add(&pSref->connections, -1);
        }

        dcb_close(m_pDcb);

        delete m_pRefs;
    }
}
