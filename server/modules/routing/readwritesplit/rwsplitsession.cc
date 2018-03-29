/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rwsplitsession.hh"
#include "rwsplit_internal.hh"
#include "routeinfo.hh"

uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer)
{
    uint32_t rval = 0;

    // All COM_STMT type statements store the ID in the same place
    uint32_t id = mxs_mysql_extract_ps_id(buffer);
    ClientHandleMap::iterator it = rses->ps_handles.find(id);

    if (it != rses->ps_handles.end())
    {
        rval = it->second;
    }
    else
    {
        MXS_WARNING("Client requests unknown prepared statement ID '%u' that "
                    "does not map to an internal ID", id);
    }

    return rval;
}
