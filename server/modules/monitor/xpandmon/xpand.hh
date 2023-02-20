/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "xpandmon.hh"
#include <string>
#include <mysql.h>
#include <maxscale/monitor.hh>
#include <maxscale/server.hh>

namespace xpand
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
    LATE,
    LATE_LEAVING,
    LEAVING,
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

enum class Result
{
    OK,
    ERROR,
    GROUP_CHANGE
};

/**
 * Wrapped mysql_query().
 *
 * @param zName   Name of the monitor.
 * @param pCon    Valid MYSQL handle.
 * @param zQuery  The query.
 *
 * @return OK, if the query succeeded.
 *         GROUP_CHANGE if the query failed with a Group Change error.
 *         ERROR otherwise.
 */
Result query(const char* zName, MYSQL* pCon, const char* zQuery);

/**
 * Is a particular Xpand node part of the quorum.
 *
 * @param zName   The name of the Xpand monitor instance.
 * @param pCon    Valid MYSQL handle to the server.
 *
 * @return True, if the node is part of the quorum, false otherwise.
 */
std::pair<Result, bool> is_part_of_the_quorum(const char* zName, MYSQL* pCon);

/**
 * Is a particular Xpand node being softfailed.
 *
 * @param zName  The name of the Xpand monitor instance.
 * @param pCon   Valid MYSQL handle to the server.
 *
 * @return True, if the node is being softfailed, false otherwise.
 */
std::pair<Result, bool> is_being_softfailed(const char* zName, MYSQL* pCon);

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName       The name of the Xpand monitor instance.
 * @param settings    Connection settings
 * @param softfailed  Whether a softfailed node is considered ok or not.
 * @param server      Server object referring to a Xpand node.
 * @param ppCon       Address of pointer to MYSQL object referring to @server
 *                    (@c *ppCon may also be NULL).
 *
 * @return True, if the server can be used as hub, false otherwise.
 *
 * @note Upon return @c *ppCon will be non-NULL.
 */
Result ping_or_connect_to_hub(const char* zName,
                              const mxs::MonitorServer::ConnectionSettings& settings,
                              Softfailed softfailed,
                              SERVER& server,
                              MYSQL** ppCon);

/**
 * Ping or create connection to server and check whether it can be used
 * as hub.
 *
 * @param zName       The name of the Xpand monitor instance.
 * @param settings    Connection settings
 * @param softfailed  Whether a softfailed node is considered ok or not.
 * @param ms          The monitored server.
 *
 * @return True, if the server can be used as hub, false otherwise.
 */
inline Result ping_or_connect_to_hub(const char* zName,
                                     const mxs::MonitorServer::ConnectionSettings& settings,
                                     Softfailed softfailed,
                                     mxs::MariaServer& ms)
{
    return ping_or_connect_to_hub(zName, settings, softfailed, *ms.server, &ms.con);
}

/**
 * Does the error message refer to a group change error.
 *
 * @param zError  Error message as returned by mysql_error().
 *
 * @return True if error is a group change error, false otherwise.
 */
bool is_group_change_error(const char* zError);
inline bool is_group_change_error(const std::string& error)
{
    return is_group_change_error(error.c_str());
}

/**
 * Is the last error a group change error.
 *
 * @param pCon  Valid MYSQL handle.
 *
 * @return True if the last error is a group change error, false otherwise.
 */
inline bool is_group_change_error(MYSQL* pCon)
{
    return is_group_change_error(mysql_error(pCon));
}

}
