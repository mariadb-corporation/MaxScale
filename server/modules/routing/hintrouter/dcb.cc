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

#include "dcb.hh"
#include <maxbase/atomic.hh>
#include <maxscale/service.hh>

Dcb::Dcb(DCB* pDcb)
    : m_sInner()
{
    // A null value for m_pDcb is allowed as a special non-existing dcb
    if (pDcb)
    {
        try
        {
            m_sInner = SDCB(pDcb, Dcb::deleter);
        }
        catch (const std::exception&)
        {
            dcb_close(pDcb);
            throw;
        }
    }
}

void Dcb::deleter(DCB* dcb)
{
    if (dcb)
    {
        HR_DEBUG("CLOSING dcb");
        // TODO: You should not need to manually adjust any
        // TODO: connections number, dcb_close should handle that.
        SERVER_REF* pSref = dcb->service->dbref;

        while (pSref && (pSref->server != dcb->server))
        {
            pSref = pSref->next;
        }

        if (pSref)
        {
            mxb::atomic::add(&pSref->connections, -1);
        }
        dcb_close(dcb);
    }
}
