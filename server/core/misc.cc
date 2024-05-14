/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/maxscale.hh>

#include <ctime>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>

#include <maxbase/pretty_print.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/build_details.hh>
#include <maxscale/config.hh>
#include <maxscale/utils.hh>
#include <maxscale/version.hh>

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
        mxs::MainWorker::get()->execute_signal_safe(&mxs::MainWorker::start_shutdown);
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

void maxscale_log_info_blurb(LogBlurbAction action)
{
    const char* verb = action == LogBlurbAction::STARTUP ? "started " : "";
    struct sysinfo info;
    sysinfo(&info);

    const mxs::Config& cnf = mxs::Config::get();
    MXB_NOTICE("Host: '%s' OS: %s@%s, %s, %s with %ld processor cores (%.2f available).",
               cnf.nodename.c_str(), cnf.sysname.c_str(), cnf.release.c_str(),
               cnf.version.c_str(), cnf.machine.c_str(), get_processor_count(),
               get_vcpu_count());

    MXB_NOTICE("Total main memory: %s (%s usable).",
               mxb::pretty_size(get_total_memory()).c_str(),
               mxb::pretty_size(get_available_memory()).c_str());
    MXB_NOTICE("MaxScale is running in process %i", getpid());
    MXB_NOTICE("MariaDB MaxScale %s %s(Commit: %s)", MAXSCALE_VERSION, verb, maxscale_commit());

    const char* thp_enable_path = "/sys/kernel/mm/transparent_hugepage/enabled";
    std::string line;
    std::getline(std::ifstream(thp_enable_path), line);

    if (line.find("[always]") != std::string::npos)
    {
        MXB_NOTICE("Transparent hugepages are set to 'always', MaxScale may end up using more memory "
                   "than it needs. To disable it, set '%s' to 'madvise' ", thp_enable_path);
    }
}
