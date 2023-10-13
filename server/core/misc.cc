/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/maxscale.h>

#include <time.h>
#include <sys/sysinfo.h>

#include <maxbase/pretty_print.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/routingworker.hh>

#include "internal/maxscale.hh"
#include "internal/service.hh"

static time_t started;

namespace
{
struct ThisUnit
{
    std::atomic<const mxb::Worker*> admin_worker {nullptr};
};
ThisUnit this_unit;
}

void maxscale_reset_starttime(void)
{
    started = time(0);
}

time_t maxscale_started(void)
{
    return started;
}

int maxscale_uptime()
{
    return time(0) - started;
}

static sig_atomic_t n_shutdowns = 0;

bool maxscale_is_shutting_down()
{
    return n_shutdowns != 0;
}

int maxscale_shutdown()
{
    int n = n_shutdowns++;

    if (n == 0)
    {
        mxs::MainWorker::get()->execute_signal_safe(&mxs::MainWorker::start_shutdown);
    }

    return n + 1;
}

static bool teardown_in_progress = false;

bool maxscale_teardown_in_progress()
{
    return teardown_in_progress;
}

void maxscale_start_teardown()
{
    teardown_in_progress = true;
}

void maxscale_log_info_blurb(LogBlurbAction action)
{
    const char* verb = action == LogBlurbAction::STARTUP ? "started " : "";
    struct sysinfo info;
    sysinfo(&info);

    const mxs::Config& cnf = mxs::Config::get();
    MXB_NOTICE("Host: '%s' OS: %s@%s, %s, %s with %lu processor cores.",
               cnf.nodename.c_str(), cnf.sysname.c_str(), cnf.release.c_str(),
               cnf.version.c_str(), cnf.machine.c_str(), get_processor_count());

    MXB_NOTICE("Total usable main memory: %s.", mxb::pretty_size(info.mem_unit * info.totalram).c_str());
    MXB_NOTICE("MaxScale is running in process %i", getpid());
    MXB_NOTICE("MariaDB MaxScale %s %s(Commit: %s)", MAXSCALE_VERSION, verb, MAXSCALE_COMMIT);
}
