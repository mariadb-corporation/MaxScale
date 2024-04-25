/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/eventcount.hh>

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>

namespace maxbase
{

EventCount::EventCount(const std::string& event_id, Duration time_window, Duration granularity)
    : m_event_id(event_id)
    , m_time_window(time_window)
    , m_granularity(granularity.count())
{
    increment();
}

void EventCount::increment()
{
    using namespace std::chrono;
    auto ticks = time_point_cast<nanoseconds>(Clock::now()).time_since_epoch().count();
    if (m_granularity)
    {
        ticks = ticks / m_granularity * m_granularity;
    }

    if (m_timestamps.empty()
        || m_timestamps.back().time_point.time_since_epoch().count() != ticks)
    {
        m_timestamps.emplace_back(TimePoint(Duration(nanoseconds(ticks))), 1);
    }
    else
    {
        ++m_timestamps.back().count;
    }
}

namespace
{
struct TimePointLessEqual
{
    TimePoint lhs;
    TimePointLessEqual(TimePoint tp)
        : lhs(tp)
    {
    }
    bool operator()(const EventCount::Timestamp& rhs) const
    {
        return lhs <= rhs.time_point;
    }
    bool operator()(TimePoint rhs) const
    {
        return lhs <= rhs;
    }
};
}

void EventCount::purge() const
{
    StopWatch sw;
    auto windowBegin = Clock::now() - m_time_window;

    auto ite = std::find_if(m_timestamps.begin(),
                            m_timestamps.end(),
                            TimePointLessEqual(windowBegin));
    m_timestamps.erase(m_timestamps.begin(), ite);
}

int EventCount::count() const
{
    purge();
    int count {0};

    for (auto ite = m_timestamps.begin(); ite != m_timestamps.end(); ++ite)
    {
        count += ite->count;
    }
    return count;
}

void EventCount::dump(std::ostream& os) const
{
    os << m_event_id << ": " << count() << " " << m_timestamps.size();
}

std::ostream& operator<<(std::ostream& os, const EventCount& EventCount)
{
    EventCount.dump(os);
    return os;
}

// EXTRA
// This section needed for gcc 4.4, to use move semantics and variadics.

EventCount::EventCount(EventCount&& ss)
    : m_event_id(std::move(ss.m_event_id))
    , m_time_window(std::move(ss.m_time_window))
    , m_granularity(std::move(ss.m_granularity))
    , m_timestamps(std::move(ss.m_timestamps))
{
}

EventCount& EventCount::operator=(EventCount&& ss)
{
    m_event_id = std::move(ss.m_event_id);
    m_time_window = std::move(ss.m_time_window);
    m_granularity = std::move(ss.m_granularity);
    m_timestamps = std::move(ss.m_timestamps);
    return *this;
}

}   // maxbase
