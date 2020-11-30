/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xpandnode.hh"
#include "xpand.hh"

bool XpandNode::can_be_used_as_hub(const char* zName,
                                   const mxs::MonitorServer::ConnectionSettings& settings,
                                   xpand::Softfailed softfailed)
{
    mxb_assert(m_pServer);
    bool rv = xpand::ping_or_connect_to_hub(zName, settings, softfailed, *m_pServer, &m_pCon);

    if (!rv)
    {
        mysql_close(m_pCon);
        m_pCon = nullptr;
    }

    return rv;
}
