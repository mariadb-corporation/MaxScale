/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"


namespace cs
{

namespace keys
{

const char CONFIG[] = "config";

}

namespace rest
{
enum Action {
    CONFIG,
    PING,
    SHUTDOWN,
    STATUS,
    START
};

const char* to_string(Action action);

std::string create_url(const SERVER& server, int64_t port, Action action);

inline std::string create_url(const mxs::MonitorServer& mserver, int64_t port, Action action)
{
    return create_url(*mserver.server, port, action);
}

}
}
