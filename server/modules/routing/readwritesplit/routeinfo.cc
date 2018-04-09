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

#include "routeinfo.hh"
#include <maxscale/queryclassifier.hh>
#include "rwsplitsession.hh"

using namespace maxscale;

RouteInfo::RouteInfo(RWSplitSession* rses, GWBUF* buffer)
    : target(TARGET_UNDEFINED)
    , command(0xff)
    , type(QUERY_TYPE_UNKNOWN)
    , stmt_id(0)
{
    ss_dassert(rses);
    ss_dassert(rses->m_client);
    ss_dassert(rses->m_client->data);
    ss_dassert(buffer);

    QueryClassifier::current_target_t current_target;

    if (rses->m_target_node == NULL)
    {
        current_target = QueryClassifier::CURRENT_TARGET_UNDEFINED;
    }
    else if (rses->m_target_node == rses->m_current_master)
    {
        current_target = QueryClassifier::CURRENT_TARGET_MASTER;
    }
    else
    {
        current_target = QueryClassifier::CURRENT_TARGET_SLAVE;
    }

    target = static_cast<route_target_t>(rses->qc().get_target_type(current_target,
                                                                    buffer,
                                                                    &command,
                                                                    &type,
                                                                    &stmt_id));
}
