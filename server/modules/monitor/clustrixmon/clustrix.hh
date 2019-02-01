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

/**
 * Is a particular Clustrix node part of the quorum.
 *
 * @param zName   The name of the Clustrix monitor instance.
 * @param server  The server object of a Clustrix node.
 * @param pCon    Valid MYSQL handle to the server.
 *
 * @return True, if the node is part of the quorum, false otherwise.
 */
bool is_part_of_the_quorum(const char* zName, const SERVER& server, MYSQL* pCon);

/**
 * Is a particular Clustrix node part of the quorum.
 *
 * @param zName   The name of the Clustrix monitor instance.
 * @param ms      The monitored server object of a Clustrix node.
 *
 * @return True, if the node is part of the quorum, false otherwise.
 */
inline bool is_part_of_the_quorum(const char* zName, MXS_MONITORED_SERVER& ms)
{
    mxb_assert(ms.server);
    mxb_assert(ms.con);

    return is_part_of_the_quorum(zName, *ms.server, ms.con);
}

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName     The name of the Clustrix monitor instance.
 * @param settings  Connection settings
 * @param server    Server object referring to a Clustrix node.
 * @param ppCon     Address of pointer to MYSQL object referring to @server
 *                  (@c *ppCon may also be NULL).
 *
 * @return True, if the server can be used as hub, false otherwise.
 *
 * @note Upon return @c *ppCon will be non-NULL.
 */
bool ping_or_connect_to_hub(const char* zName,
                            const MXS_MONITORED_SERVER::ConnectionSettings& settings,
                            SERVER& server,
                            MYSQL** ppCon);

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName     The name of the Clustrix monitor instance.
 * @param settings  Connection settings
 * @param ms        The monitored server.
 *
 * @return True, if the server can be used as hub, false otherwise.
 */
inline bool ping_or_connect_to_hub(const char* zName,
                                   const MXS_MONITORED_SERVER::ConnectionSettings& settings,
                                   MXS_MONITORED_SERVER& ms)
{
    return ping_or_connect_to_hub(zName, settings, *ms.server, &ms.con);
}

}
