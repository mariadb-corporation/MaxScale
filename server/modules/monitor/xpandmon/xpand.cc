/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xpand.hh"
#include <mysql.h>
#include <maxbase/assert.hh>

using maxscale::Monitor;
using maxscale::MonitorServer;

namespace
{

const char CN_DYNAMIC[] = "dynamic";
const char CN_LATE[] = "late";
const char CN_LATE_LEAVING[] = "late, leaving";
const char CN_LEAVING[] = "leaving";
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
    case SubState::LATE:
        return CN_LATE;

    case SubState::LATE_LEAVING:
        return CN_LATE_LEAVING;

    case SubState::LEAVING:
        return CN_LEAVING;

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
    if (substate == CN_LATE)
    {
        return SubState::LATE;
    }
    else if (substate == CN_LATE_LEAVING)
    {
        return SubState::LATE_LEAVING;
    }
    else if (substate == CN_LEAVING)
    {
        return SubState::LEAVING;
    }
    else if (substate == CN_NORMAL)
    {
        return SubState::NORMAL;
    }
    else
    {
        MXB_INFO("'%s' is an unknown sub-state for a Xpand node.", substate.c_str());
        return SubState::UNKNOWN;
    }
}

xpand::Result xpand::query(const char* zName, MYSQL* pCon, const char* zQuery)
{
    Result rv = Result::OK;

    if (mysql_query(pCon, zQuery) != 0)
    {
        if (is_group_change_error(pCon))
        {
            MXB_INFO("%s: Group change detected on %s: %s",
                     zName, mysql_get_host_info(pCon), mysql_error(pCon));
            rv = Result::GROUP_CHANGE;
        }
        else
        {
            MXB_ERROR("%s: Could not execute '%s' on %s: %s",
                      zName, zQuery, mysql_get_host_info(pCon), mysql_error(pCon));
            rv = Result::ERROR;
        }
    }

    return rv;
}

std::pair<xpand::Result, bool> xpand::is_part_of_the_quorum(const char* zName, MYSQL* pCon)
{
    std::pair<xpand::Result, bool> rv = { xpand::Result::ERROR, false };

    const char ZQUERY[] = "SELECT status FROM system.membership WHERE nid = gtmnid()";

    rv.first = xpand::query(zName, pCon, ZQUERY);

    if (rv.first == xpand::Result::OK)
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
                    rv.second = true;
                    break;

                case xpand::Status::STATIC:
                    MXB_NOTICE("%s: Node %s is not part of the quorum (static), switching to "
                               "other node for monitoring.",
                               zName, mysql_get_host_info(pCon));
                    break;

                case xpand::Status::DYNAMIC:
                    MXB_NOTICE("%s: Node %s is not part of the quorum (dynamic), switching to "
                               "other node for monitoring.",
                               zName, mysql_get_host_info(pCon));
                    break;

                case xpand::Status::UNKNOWN:
                    MXB_WARNING("%s: Do not know how to interpret '%s'. Assuming node %s "
                                "is not part of the quorum.",
                                zName, row[0], mysql_get_host_info(pCon));
                }
            }
            else
            {
                MXB_WARNING("%s: No status returned for '%s' on %s.",
                            zName, ZQUERY, mysql_get_host_info(pCon));
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXB_WARNING("%s: No result returned for '%s' on %s.",
                        zName, ZQUERY, mysql_get_host_info(pCon));
        }
    }

    return rv;
}

std::pair<xpand::Result, bool> xpand::is_being_softfailed(const char* zName, MYSQL* pCon)
{
    std::pair<Result, bool> rv = { xpand::Result::ERROR, false };

    const char ZQUERY[] = "SELECT nodeid FROM system.softfailed_nodes WHERE nodeid = gtmnid()";

    rv.first = xpand::query(zName, pCon, ZQUERY);

    if (rv.first == xpand::Result::OK)
    {
        MYSQL_RES* pResult = mysql_store_result(pCon);

        if (pResult)
        {
            mxb_assert(mysql_field_count(pCon) == 1);

            MYSQL_ROW row = mysql_fetch_row(pResult);
            if (row)
            {
                // If a row is found, it is because the node is being softfailed.
                rv.second = true;
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXB_WARNING("%s: No result returned for '%s' on %s.",
                        zName, ZQUERY, mysql_get_host_info(pCon));
        }
    }

    return rv;
}

xpand::Result xpand::ping_or_connect_to_hub(const char* zName,
                                            const MonitorServer::ConnectionSettings& settings,
                                            Softfailed softfailed,
                                            SERVER& server,
                                            MYSQL** ppCon)
{
    Result rv = Result::ERROR;
    std::string err;
    auto rv2 = mxs::MariaServer::ping_or_connect_to_db(settings, server, ppCon, &err);

    if (Monitor::connection_is_ok(rv2))
    {
        bool is_part_of_the_quorum;
        std::tie(rv, is_part_of_the_quorum) = xpand::is_part_of_the_quorum(zName, *ppCon);

        if (rv == Result::OK && is_part_of_the_quorum)
        {
            if (softfailed == Softfailed::REJECT)
            {
                bool is_being_softfailed;
                std::tie(rv, is_being_softfailed) = xpand::is_being_softfailed(zName, *ppCon);

                if (rv == Result::OK && is_being_softfailed)
                {
                    MXB_NOTICE("%s: The Xpand node %s used as hub is part of the quorum, "
                               "but it is being softfailed. Switching to another node.",
                               zName, server.address());
                }
            }
            else
            {
                rv = Result::OK;
            }
        }
    }
    else
    {
        if (is_group_change_error(err))
        {
            rv = Result::GROUP_CHANGE;
        }
        else
        {
            MXB_ERROR("%s: Could either not ping or create connection to %s:%d: %s",
                      zName, server.address(), server.port(), err.c_str());
        }
    }

    return rv;
}

bool xpand::is_group_change_error(const char* zError)
{
    return strstr(zError, "Group change") != NULL;
}
