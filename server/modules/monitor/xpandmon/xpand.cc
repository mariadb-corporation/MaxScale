/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xpand.hh"
#include <mysql.h>
#include <maxbase/assert.h>

using maxscale::Monitor;
using maxscale::MonitorServer;

namespace
{

const char CN_DYNAMIC[] = "dynamic";
const char CN_NORMAL[] = "normal";
const char CN_QUORUM[] = "quorum";
const char CN_STATIC[] = "static";
const char CN_UNKNOWN[] = "unknown";
}

std::string xpand::to_string(xpand::Status status)
{
    switch (status)
    {
    case Status::QUORUM:
        return CN_QUORUM;

    case Status::STATIC:
        return CN_STATIC;

    case Status::DYNAMIC:
        return CN_DYNAMIC;

    case Status::UNKNOWN:
        return CN_UNKNOWN;
    }

    mxb_assert(!true);
    return CN_UNKNOWN;
}

xpand::Status xpand::status_from_string(const std::string& status)
{
    if (status == CN_QUORUM)
    {
        return Status::QUORUM;
    }
    else if (status == CN_STATIC)
    {
        return Status::STATIC;
    }
    else if (status == CN_DYNAMIC)
    {
        return Status::DYNAMIC;
    }
    else
    {
        MXB_WARNING("'%s' is an unknown status for a Xpand node.", status.c_str());
        return Status::UNKNOWN;
    }
}

std::string xpand::to_string(xpand::SubState substate)
{
    switch (substate)
    {
    case SubState::NORMAL:
        return CN_NORMAL;

    case SubState::UNKNOWN:
        return CN_UNKNOWN;
    }

    mxb_assert(!true);
    return CN_UNKNOWN;
}

xpand::SubState xpand::substate_from_string(const std::string& substate)
{
    if (substate == CN_NORMAL)
    {
        return SubState::NORMAL;
    }
    else
    {
        MXB_WARNING("'%s' is an unknown sub-state for a Xpand node.", substate.c_str());
        return SubState::UNKNOWN;
    }
}

bool xpand::is_part_of_the_quorum(const char* zName, MYSQL* pCon)
{
    bool rv = false;

    const char ZQUERY[] = "SELECT status FROM system.membership WHERE nid = gtmnid()";

    if (mysql_query(pCon, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pCon);

        if (pResult)
        {
            mxb_assert(mysql_field_count(pCon) == 1);

            MYSQL_ROW row = mysql_fetch_row(pResult);
            if (row && row[0])
            {
                xpand::Status status = xpand::status_from_string(row[0]);

                switch (status)
                {
                case xpand::Status::QUORUM:
                    rv = true;
                    break;

                case xpand::Status::STATIC:
                    MXS_NOTICE("%s: Node %s is not part of the quorum (static), switching to "
                               "other node for monitoring.",
                               zName, mysql_get_host_info(pCon));
                    break;

                case xpand::Status::DYNAMIC:
                    MXS_NOTICE("%s: Node %s is not part of the quorum (dynamic), switching to "
                               "other node for monitoring.",
                               zName, mysql_get_host_info(pCon));
                    break;

                case xpand::Status::UNKNOWN:
                    MXS_WARNING("%s: Do not know how to interpret '%s'. Assuming node %s "
                                "is not part of the quorum.",
                                zName, row[0], mysql_get_host_info(pCon));
                }
            }
            else
            {
                MXS_WARNING("%s: No status returned for '%s' on %s.",
                            zName, ZQUERY, mysql_get_host_info(pCon));
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXS_WARNING("%s: No result returned for '%s' on %s.",
                        zName, ZQUERY, mysql_get_host_info(pCon));
        }
    }
    else
    {
        MXS_ERROR("%s: Could not execute '%s' on %s: %s",
                  zName, ZQUERY, mysql_get_host_info(pCon), mysql_error(pCon));
    }

    return rv;
}

bool xpand::is_being_softfailed(const char* zName, MYSQL* pCon)
{
    bool rv = false;

    const char ZQUERY[] = "SELECT nodeid FROM system.softfailed_nodes WHERE nodeid = gtmnid()";

    if (mysql_query(pCon, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pCon);

        if (pResult)
        {
            mxb_assert(mysql_field_count(pCon) == 1);

            MYSQL_ROW row = mysql_fetch_row(pResult);
            if (row)
            {
                // If a row is found, it is because the node is being softfailed.
                rv = true;
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXS_WARNING("%s: No result returned for '%s' on %s.",
                        zName, ZQUERY, mysql_get_host_info(pCon));
        }
    }
    else
    {
        MXS_ERROR("%s: Could not execute '%s' on %s: %s",
                  zName, ZQUERY, mysql_get_host_info(pCon), mysql_error(pCon));
    }

    return rv;
}

bool xpand::ping_or_connect_to_hub(const char* zName,
                                   const MonitorServer::ConnectionSettings& settings,
                                   Softfailed softfailed,
                                   SERVER& server,
                                   MYSQL** ppCon)
{
    bool connected = false;
    std::string err;
    MonitorServer::ConnectResult rv = MonitorServer::ping_or_connect_to_db(settings, server, ppCon, &err);

    if (Monitor::connection_is_ok(rv))
    {
        if (xpand::is_part_of_the_quorum(zName, *ppCon))
        {
            if ((softfailed == Softfailed::REJECT) && xpand::is_being_softfailed(zName, *ppCon))
            {
                MXS_NOTICE("%s: The Xpand node %s used as hub is part of the quorum, "
                           "but it is being softfailed. Switching to another node.",
                           zName, server.address());
            }
            else
            {
                connected = true;
            }
        }
    }
    else
    {
        MXS_ERROR("%s: Could either not ping or create connection to %s:%d: %s",
                  zName, server.address(), server.port(), err.c_str());
    }

    return connected;
}
