/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/jansson.hh>

#include <array>
#include <atomic>

namespace maxscale
{
class Profiler
{
public:
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    static int profiling_signal();

    static Profiler& get();

    /**
     * Collects a backtrace
     */
    void save_stacktrace();

    /**
     * Take a single snapshot of the thread stacks
     *
     * @param host The hostname of this server
     *
     * @return The snapshot as a JSON resource
     */
    json_t* snapshot(const char* host);

    /**
     * Get a human-readable stacktrace from all threads
     *
     * @return The pretty-printed stacktrace
     */
    std::string stacktrace();

private:
    using Stack = std::array<void*, 127>;

    struct Sample
    {
        Stack             stack {};
        int               count {0};
        std::atomic<bool> sampled{false};
    };

    static_assert(sizeof(Sample) == 1024);

    Profiler();
    void wait_for_samples(int num_samples);
    int  collect_samples();

    std::array<Sample, 1024> m_samples;
    std::atomic<uint64_t>    m_next_slot {0};
};
}
