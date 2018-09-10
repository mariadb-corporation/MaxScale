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

/**
 * Internal header for the server type
 */

#include <maxbase/average.hh>
#include <maxscale/server.h>
#include <maxscale/resultset.hh>
#include <maxscale/routingworker.hh>

std::unique_ptr<ResultSet> serverGetList();

// Private server implementation
class Server : public SERVER
{
public:

    Server()
        : response_time(maxbase::EMAverage {0.04, 0.35, 500})
    {
    }

    int response_time_num_samples() const
    {
        return response_time->num_samples();
    }

    double response_time_average() const
    {
        return response_time->average();
    }

    void response_time_add(double ave, int num_samples)
    {
        response_time->add(ave, num_samples);
    }

private:
    // nantti, TODO. Decide whether to expose some of this in config, or if the values
    // can be calculated at runtime. The "500" or sample_max affects how often a
    // session should updates this stat. sample_max should be slightly lower than max sample
    // rate (which is less than qps due to the noise filter).
    mxs::rworker_local<maxbase::EMAverage> response_time;
};

void server_free(Server* server);
