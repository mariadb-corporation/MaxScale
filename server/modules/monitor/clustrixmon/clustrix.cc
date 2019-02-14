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

#include "clustrix.hh"
#include <maxbase/assert.h>

namespace
{

const char CN_DYNAMIC[] = "dynamic";
const char CN_NORMAL[]  = "normal";
const char CN_QUORUM[]  = "quorum";
const char CN_STATIC[]  = "static";
const char CN_UNKNOWN[] = "unknown";

}

std::string Clustrix::to_string(Clustrix::Status status)
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

Clustrix::Status Clustrix::status_from_string(const std::string& status)
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
        MXB_WARNING("'%s' is an unknown status for a Clustrix node.", status.c_str());
        return Status::UNKNOWN;
    }
}

std::string Clustrix::to_string(Clustrix::SubState substate)
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

Clustrix::SubState Clustrix::substate_from_string(const std::string& substate)
{
    if (substate == CN_NORMAL)
    {
        return SubState::NORMAL;
    }
    else
    {
        MXB_WARNING("'%s' is an unknown sub-state for a Clustrix node.", substate.c_str());
        return SubState::UNKNOWN;
    }
}

bool Clustrix::is_part_of_the_quorum(const char* zName, const SERVER& server, MYSQL* pCon)
{
    bool rv = false;

    const char* zAddress = server.address;
    int port = server.port;

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
                Clustrix::Status status = Clustrix::status_from_string(row[0]);

                switch (status)
                {
                case Clustrix::Status::QUORUM:
                    rv = true;
                    break;

                case Clustrix::Status::STATIC:
                    MXS_NOTICE("%s: Node %s:%d is not part of the quorum (static), switching to "
                               "other node for monitoring.",
                               zName, zAddress, port);
                    break;

                case Clustrix::Status::DYNAMIC:
                    MXS_NOTICE("%s: Node %s:%d is not part of the quorum (dynamic), switching to "
                               "other node for monitoring.",
                               zName, zAddress, port);
                    break;

                case Clustrix::Status::UNKNOWN:
                    MXS_WARNING("%s: Do not know how to interpret '%s'. Assuming node %s:%d "
                                "is not part of the quorum.",
                                zName, row[0], zAddress, port);
                }
            }
            else
            {
                MXS_WARNING("%s: No status returned for '%s' on %s:%d.", zName, ZQUERY, zAddress, port);
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXS_WARNING("%s: No result returned for '%s' on %s:%d.", zName, ZQUERY, zAddress, port);
        }
    }
    else
    {
        MXS_ERROR("%s: Could not execute '%s' on %s:%d: %s",
                  zName, ZQUERY, zAddress, port, mysql_error(pCon));
    }

    return rv;
}

bool Clustrix::is_being_softfailed(const char* zName, const SERVER& server, MYSQL* pCon)
{
    bool rv = false;

    const char* zAddress = server.address;
    int port = server.port;

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
            MXS_WARNING("%s: No result returned for '%s' on %s:%d.", zName, ZQUERY, zAddress, port);
        }
    }
    else
    {
        MXS_ERROR("%s: Could not execute '%s' on %s:%d: %s",
                  zName, ZQUERY, zAddress, port, mysql_error(pCon));
    }

    return rv;
}

bool Clustrix::ping_or_connect_to_hub(const char* zName,
                                      const MXS_MONITORED_SERVER::ConnectionSettings& settings,
                                      Softfailed softfailed,
                                      SERVER& server,
                                      MYSQL** ppCon)
{
    bool connected = false;
    mxs_connect_result_t rv = mon_ping_or_connect_to_db(settings, server, ppCon);

    if (mon_connection_is_ok(rv))
    {
        if (Clustrix::is_part_of_the_quorum(zName, server, *ppCon))
        {
            if ((softfailed == Softfailed::REJECT) && Clustrix::is_being_softfailed(zName, server, *ppCon))
            {
                MXS_NOTICE("%s: The Clustrix node %s used as hub is part of the quorum, "
                           "but it is being softfailed. Switching to another node.",
                           zName, server.address);
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
                  zName, server.address, server.port, mysql_error(*ppCon));
    }

    return connected;
}
