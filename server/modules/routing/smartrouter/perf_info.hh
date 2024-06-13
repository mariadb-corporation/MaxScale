/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxbase/stopwatch.hh>
#include <maxbase/shareddata.hh>
#include <maxscale/target.hh>
#include <unordered_map>

/** PerformanceInfo is a class that on the one hand provides routeQuery() with performance/routing
 *  information and on the other has data for class SmartRouter to manage the life-time of a measurement.
 */
class PerformanceInfo
{
public:
    PerformanceInfo() = default;    // creates an instance where is_valid()==false;
    PerformanceInfo(mxs::Target* t, maxbase::Duration d);
    PerformanceInfo(const PerformanceInfo&) = default;
    PerformanceInfo& operator=(const PerformanceInfo&) = default;
    PerformanceInfo(PerformanceInfo&&) = default;
    PerformanceInfo& operator=(PerformanceInfo&&) = default;


    bool is_valid() const;

    mxs::Target*      target() const;
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
    mxs::Target*       m_target {nullptr};
    maxbase::Duration  m_duration {0};
    maxbase::TimePoint m_creation_time = maxbase::Clock::now(maxbase::NowType::EPollTick);

    int  m_eviction_schedule = 0;
    bool m_updating = false;
};

// Update to the SharedData. Container updates are currently always InsertUpdate.
struct PerformanceInfoUpdate
{
    std::string     key;
    PerformanceInfo value;

    // This copies the key, which is the canonical. Is that always necessary?
    PerformanceInfoUpdate(const std::string& key, const PerformanceInfo& value)
        : key(key)
        , value(value)
    {
    }
};

// The container and SharedData types of PerformanceInfo.
using PerformanceInfoContainer = std::unordered_map<std::string, PerformanceInfo>;
using SharedPerformanceInfo = maxbase::SharedData<PerformanceInfoContainer, PerformanceInfoUpdate>;

// implementation details below
inline PerformanceInfo::PerformanceInfo(mxs::Target* t, maxbase::Duration d)
    : m_target(t)
    , m_duration(d)
{
}

inline bool PerformanceInfo::is_valid() const
{
    return m_target != nullptr;
}

inline mxs::Target* PerformanceInfo::target() const
{
    return m_target;
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
    return maxbase::Clock::now(maxbase::NowType::EPollTick) - m_creation_time;
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
