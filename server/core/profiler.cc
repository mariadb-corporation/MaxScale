/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/profiler.hh"

#include <maxbase/string.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>

#include <dirent.h>
#include <execinfo.h>

#include <thread>

// Real-time signals are queued separately instead of being combined like regular signals.
static const int s_profiling_rt_signal = SIGRTMIN + 1;

namespace maxscale
{
// static
int Profiler::profiling_signal()
{
    return s_profiling_rt_signal;
}

Profiler& Profiler::get()
{
    static Profiler s_profile;
    return s_profile;
}

Profiler::Profiler()
{
    // This "primes" the backtrace by loading libgcc if it's not already loaded. The manual page for
    // backtrace() states that libgcc uses malloc when first initialized and by calling it once on startup, we
    // avoid doing the initial load inside of a signal handler where a malloc call could be catastrophic.
    backtrace(m_samples[0].stack.data(), m_samples[0].stack.size());
}

void Profiler::save_stacktrace()
{
    Sample& s = m_samples[m_next_slot.fetch_add(1, std::memory_order_relaxed) % m_samples.size()];
    s.count = backtrace(s.stack.data(), s.stack.size());
    s.sampled = true;
}

void Profiler::wait_for_samples(int num_samples)
{
    for (int i = 0; i < num_samples; i++)
    {
        Sample& s = m_samples[i];

        while (!s.sampled)
        {
            std::this_thread::sleep_for(1ms);
        }

        s.sampled = false;
    }
}

int Profiler::collect_samples()
{
    pid_t pid = getpid();
    int samples = 0;
    m_next_slot = 0;

    if (DIR* dir = opendir("/proc/self/task"))
    {
        while (dirent* de = readdir(dir))
        {
            if (int tid = atoi(de->d_name))
            {
                if (tgkill(pid, tid, s_profiling_rt_signal) == -1)
                {
                    // The call can fail with ESRCH if the thread disappears between the time we read it and
                    // the time the tgkill call is done.
                    if (errno != ESRCH)
                    {
                        MXB_INFO("Failed to signal thread: %d, %s", errno, mxb_strerror(errno));
                    }
                }
                else
                {
                    samples++;
                }
            }
        }

        closedir(dir);
    }

    samples = std::min(samples, (int)m_samples.size());

    wait_for_samples(samples);

    return samples;
}

json_t* Profiler::snapshot(const char* host)
{
    int num_samples = collect_samples();
    json_t* arr = json_array();

    for (int i = 0; i < num_samples; i++)
    {
        const Sample& s = m_samples[i];
        json_t* val = json_array();
        char** symbols = backtrace_symbols(s.stack.data(), s.count);

        // The first frames are in the signal handling code which is not what we're interested in. Reverse the
        // stackframes so that the bottom of the stack is the first element in the array. This makes it easier
        // to process them later on into a flamegraph.
        for (int n = s.count - 1; n > 0; n--)
        {
            json_array_append_new(val, json_string(symbols[n]));
        }

        free(symbols);
        json_array_append_new(arr, val);
    }

    json_t* attr = json_object();
    json_object_set_new(attr, "profile", arr);

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_object_set_new(obj, CN_ID, json_string("profile"));
    json_object_set_new(obj, CN_TYPE, json_string("profile"));

    return mxs_json_resource(host, "/maxscale/debug/profile", obj);
}
}
