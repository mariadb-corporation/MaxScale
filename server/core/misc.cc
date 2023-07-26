/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/maxscale.hh>

#include <ctime>
#include <maxscale/mainworker.hh>
#include <maxscale/build_details.hh>
#include "internal/maxscale.hh"

namespace
{
time_t started;
sig_atomic_t n_shutdowns {0};
bool teardown_in_progress {false};
}

void maxscale_reset_starttime()
{
    started = time(nullptr);
}

time_t maxscale_started()
{
    return started;
}

int maxscale_uptime()
{
    return time(nullptr) - started;
}

bool maxscale_is_shutting_down()
{
    return n_shutdowns != 0;
}

int maxscale_shutdown()
{
    int n = n_shutdowns++;

    if (n == 0)
    {
        mxs::MainWorker::start_shutdown();
    }

    return n + 1;
}

bool maxscale_teardown_in_progress()
{
    return teardown_in_progress;
}

void maxscale_start_teardown()
{
    teardown_in_progress = true;
}

const char* maxscale_commit()
{
    return MAXSCALE_COMMIT;
}

const char* maxscale_source()
{
    return MAXSCALE_SOURCE;
}

const char* maxscale_cmake_flags()
{
    return MAXSCALE_CMAKE_FLAGS;
}

const char* maxscale_jenkins_build_tag()
{
    return MAXSCALE_JENKINS_BUILD_TAG;
}
