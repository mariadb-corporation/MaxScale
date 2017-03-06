#pragma once
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

#include "hintrouterdefs.hh"
#include <algorithm>
#include <maxscale/dcb.h>

class Dcb
{
public:
    explicit Dcb(DCB* pDcb);
    Dcb(const Dcb& rhs);
    ~Dcb()
    {
        dec();
    }

    Dcb& operator = (Dcb rhs);

    struct server* server() const
    {
        return m_pDcb->server;
    }

    DCB* get() const
    {
        return m_pDcb;
    }

    bool write(GWBUF* pPacket)
    {
        ss_dassert(m_pDcb);
        return m_pDcb->func.write(m_pDcb, pPacket) == 1;
    }

private:
    void inc()
    {
        ++(*m_pRefs);
    }

    void dec();

    void swap(Dcb& rhs)
    {
        std::swap(m_pDcb, rhs.m_pDcb);
        std::swap(m_pRefs, rhs.m_pRefs);
    }

private:
    DCB* m_pDcb;
    int* m_pRefs;
};

