/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <tuple>
#include <iostream>
#include <vector>
#include <cstdint>
#include <string>

#include <maxscale/monitor.hh>

#define SERVER_DOWN 0

std::vector<std::tuple<uint64_t, uint64_t, mxs_monitor_event_t>> tests =
{
    // TODO: Add test cases
};

int main(int argc, char** argv)
{
    int error = 0;

    for (auto test : tests)
    {
        uint64_t before = std::get<0>(test);
        uint64_t after = std::get<1>(test);
        mxs_monitor_event_t expected = std::get<2>(test);
        mxs_monitor_event_t res = mxs::MonitorServer::event_type(before, after);

        std::string extra;

        if (res != expected)
        {
            error = 1;
            extra = " ERROR: Expected ";
            extra += mxs::Monitor::get_event_name(expected);
        }

        std::cout << mxs::Monitor::get_event_name(res) << ": "
                  << "[" << mxs::Target::status_to_string(before, 0) << "] -> "
                  << "[" << mxs::Target::status_to_string(after, 0) << "]"
                  << extra << "\n";
    }

    return error;
}
