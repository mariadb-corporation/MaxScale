/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxbase/stopwatch.hh>
#include <maxbase/host.hh>

#include <unordered_map>

/** Class PerformanceInfo is a basic structure for storing a Host and Duration pair, along with
 *  the time it was created.
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
private:
    maxbase::Host     m_host;
    maxbase::Duration m_duration;

    maxbase::TimePoint m_creation_time = maxbase::Clock::now();
};

/** class CanonicalPerformance holds the persistent performance
 *  info gathered thus far.
 */
class CanonicalPerformance
{
public:
    CanonicalPerformance(const std::string& persistent_file);

    /** Insert if not already inserted and return true, else false. */
    bool insert(const std::string& canonical, const PerformanceInfo& perf);

    /** Remove if entry exists and return true, else false. */
    bool remove(const std::string& canonical);

    /** If entry does not exists, returned PerformanceInfo::is_valid()==false */
    PerformanceInfo find(const std::string& canonical);

    void clear();
    void persist() const;

private:
    const std::string                                m_persistent_file;
    std::unordered_map<std::string, PerformanceInfo> m_perfs;
    void read_persisted();
    mutable int m_nChanges;
};

// Threadsafe global singleton behind perf_find and perf_update.
// perf_find, find existing performance info, handles expirations
PerformanceInfo perf_find(const std::string& canonical);

// Insert if not already inserted and return true, else false.
// Will probably handle some expiration like work.
bool perf_update(const std::string& canonical, const PerformanceInfo& perf);

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
