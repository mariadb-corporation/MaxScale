/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "clustrixmon.hh"
#include <string>
#include <maxscale/monitor.hh>
#include <maxscale/server.hh>

namespace Clustrix
{

enum class Status
{
    QUORUM,
    STATIC,
    DYNAMIC,
    UNKNOWN
};

Status      status_from_string(const std::string& status);
std::string to_string(Status status);

enum class SubState
{
    NORMAL,
    UNKNOWN
};

SubState    substate_from_string(const std::string& substate);
std::string to_string(SubState sub_state);

enum class Softfailed
{
    ACCEPT,
    REJECT
};

/**
 * Is a particular Clustrix node part of the quorum.
 *
 * @param zName   The name of the Clustrix monitor instance.
 * @param pCon    Valid MYSQL handle to the server.
 *
 * @return True, if the node is part of the quorum, false otherwise.
 */
bool is_part_of_the_quorum(const char* zName, MYSQL* pCon);

/**
 * Is a particular Clustrix node part of the quorum.
 *
 * @param zName   The name of the Clustrix monitor instance.
 * @param ms      The monitored server object of a Clustrix node.
 *
 * @return True, if the node is part of the quorum, false otherwise.
 */
inline bool is_part_of_the_quorum(const char* zName, mxs::MonitorServer& ms)
{
    mxb_assert(ms.con);

    return is_part_of_the_quorum(zName, ms.con);
}

/**
 * Is a particular Clustrix node being softfailed.
 *
 * @param zName  The name of the Clustrix monitor instance.
 * @param pCon   Valid MYSQL handle to the server.
 *
 * @return True, if the node is being softfailed, false otherwise.
 */
bool is_being_softfailed(const char* zName, MYSQL* pCon);

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName       The name of the Clustrix monitor instance.
 * @param settings    Connection settings
 * @param softfailed  Whether a softfailed node is considered ok or not.
 * @param server      Server object referring to a Clustrix node.
 * @param ppCon       Address of pointer to MYSQL object referring to @server
 *                    (@c *ppCon may also be NULL).
 *
 * @return True, if the server can be used as hub, false otherwise.
 *
 * @note Upon return @c *ppCon will be non-NULL.
 */
bool ping_or_connect_to_hub(const char* zName,
                            const mxs::MonitorServer::ConnectionSettings& settings,
                            Softfailed softfailed,
                            SERVER& server,
                            MYSQL** ppCon);

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName       The name of the Clustrix monitor instance.
 * @param settings    Connection settings
 * @param softfailed  Whether a softfailed node is considered ok or not.
 * @param ms          The monitored server.
 *
 * @return True, if the server can be used as hub, false otherwise.
 */
inline bool ping_or_connect_to_hub(const char* zName,
                                   const mxs::MonitorServer::ConnectionSettings& settings,
                                   Softfailed softfailed,
                                   mxs::MonitorServer& ms)
{
    return ping_or_connect_to_hub(zName, settings, softfailed, *ms.server, &ms.con);
}
}
