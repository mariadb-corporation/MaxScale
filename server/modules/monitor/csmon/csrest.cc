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
#include "csrest.hh"

using std::string;

namespace
{
// TODO: This is just the mockup Columnstore daemon.
const char REST_BASE[] = "/drrtuy/cmapi/0.0.2/node/";
}

namespace cs
{

const char* rest::to_string(rest::Action action)
{
    switch (action)
    {
    case CONFIG:
        return "config";

    case PING:
        return "ping";

    case SHUTDOWN:
        return "shutdown";

    case STATUS:
        return "status";

    case START:
        return "start";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

std::string rest::create_url(const SERVER& server, int64_t port, rest::Action action)
{
    string url("https://");
    url += server.address;
    url += ":";
    url += std::to_string(port);
    url += REST_BASE;

    url += to_string(action);

    return url;
}

}
