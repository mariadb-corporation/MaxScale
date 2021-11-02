/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/stopwatch.hh>
#include <maxbase/host.hh>

#include <unordered_map>

/** PerformanceInfo is a class that on the one hand provides routeQuery() with performance/routing
 *  information and on the other has data for class SmartRouter to manage the life-time of a measurment.
 */
class PerformanceInfo
{
public:
    PerformanceInfo() = default;    // creates an instance where is_valid()==false;
    PerformanceInfo(const maxbase::Host& h, maxbase::Duration d);

    bool is_valid() const;

    maxbase::Host     host() const;
    maxbase::Duration duration() const;

    /** When was this PerformanceInfo created.
     */
    maxbase::TimePoint creation_time() const;

    /** Duration since this PerformanceInfo was created
     */
    maxbase::Duration age() const;

    /** Managed and used only by class SmartRouter. */
    void   set_eviction_schedule(size_t es);
    size_t eviction_schedule() const;

    /** Managed and used only by class SmartRouter. */
    void set_updating(bool val);
    bool is_updating() const;
private:
    maxbase::Host     m_host;
    maxbase::Duration m_duration;

    int  m_eviction_schedule = 0;
    bool m_updating = false;

    maxbase::TimePoint m_creation_time = maxbase::Clock::now();
};

// For logging. Shortens str to nchars and adds "..." TODO move somewhere more appropriate
std::string show_some(const std::string& str, int nchars = 70);

// implementation details below
inline PerformanceInfo::PerformanceInfo(const maxbase::Host& h, maxbase::Duration d)
    : m_host(h)
    , m_duration(d)
{
}

inline bool PerformanceInfo::is_valid() const
{
    return m_host.is_valid();
}

inline maxbase::Host PerformanceInfo::host() const
{
    return m_host;
}

inline maxbase::Duration PerformanceInfo::duration() const
{
    return m_duration;
}

inline maxbase::TimePoint PerformanceInfo::creation_time() const
{
    return m_creation_time;
}

inline maxbase::Duration PerformanceInfo::age() const
{
    return maxbase::Clock::now() - m_creation_time;
}

inline void PerformanceInfo::set_eviction_schedule(size_t es)
{
    m_eviction_schedule = es;
}

inline size_t PerformanceInfo::eviction_schedule() const
{
    return m_eviction_schedule;
}

inline void PerformanceInfo::set_updating(bool val)
{
    m_updating = val;
}

inline bool PerformanceInfo::is_updating() const
{
    return m_updating;
}
