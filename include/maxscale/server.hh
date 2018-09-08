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

#include <maxscale/ccdefs.hh>

#include <string>
#include <cstdlib>

#include <maxscale/server.h>

namespace maxscale
{
bool server_set_status(SERVER* server, int bit, std::string* errmsg_out = NULL);
bool server_clear_status(SERVER* server, int bit, std::string* errmsg_out = NULL);

/** Returns true if the two server "scores" are within 1/(see code) of each other.
 *  The epsilon needs tweaking, and might even need to be in config. This
 *  function is important for some compares, where one server might be only
 *  marginally better than others, in which case historical data could determine
 *  the outcome.
 */
inline bool almost_equal_server_scores(double lhs, double rhs)
{
    constexpr double div = 100; // within 1% of each other.
    return std::abs((long)(lhs - rhs)) < std::abs((long)std::max(lhs, rhs)) * (1 / div);
}
}
