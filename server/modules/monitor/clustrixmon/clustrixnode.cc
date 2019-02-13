/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "clustrixnode.hh"
#include "clustrix.hh"

bool ClustrixNode::can_be_used_as_hub(const char* zName,
                                      const MXS_MONITORED_SERVER::ConnectionSettings& settings)
{
    mxb_assert(m_pServer);
    bool rv = Clustrix::ping_or_connect_to_hub(zName, settings, Clustrix::Softfailed::REJECT,
                                               *m_pServer, &m_pCon);

    if (!rv)
    {
        mysql_close(m_pCon);
        m_pCon = nullptr;
    }

    return rv;
}
