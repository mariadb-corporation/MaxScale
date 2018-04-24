/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include "eventcount.hh"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>

namespace throttle
{

EventCount::EventCount(const std::string& event_id, Duration time_window, Duration granularity) :
    m_event_id(event_id),
    m_time_window(time_window),
    m_granularity(granularity.count())
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
        m_timestamps.emplace_back(TimePoint(ticks), 1);
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
    TimePointLessEqual(TimePoint tp) : lhs(tp) {}
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

    auto ite = std::find_if(m_timestamps.begin(), m_timestamps.end(),
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

void EventCount::dump(std::ostream &os) const
{
    os << m_event_id << ": " << count() << " " << m_timestamps.size();
}

std::ostream& operator<<(std::ostream& os, const EventCount& EventCount)
{
    EventCount.dump(os);
    return os;
}

// Force a purge once in awhile, could be configurable. This is needed if
// a client generates lots of events but rarely reads them back (purges).
const int CleanupCountdown = 10000;

SessionCount::SessionCount(const std::string& sess_id, Duration time_window,
                           Duration granularity) :
    m_sess_id(sess_id), m_time_window(time_window), m_granularity(granularity),
    m_cleanup_countdown(CleanupCountdown)
{
}

const std::vector<EventCount> &SessionCount::event_counts() const
{
    purge();
    return m_event_counts;
}

bool SessionCount::empty() const
{
    purge();
    return m_event_counts.empty();
}

namespace
{
struct MatchEventId
{
    std::string event_id;
    MatchEventId(const std::string& id) : event_id(id) {};
    bool operator()(const EventCount& stats) const
    {
        return event_id == stats.event_id();
    }
};
}

void SessionCount::increment(const std::string& event_id)
{
    // Always put the incremented entry (latest timestamp) last in the vector (using
    // rotate). This means the vector is ordered so that expired entries are always first.

    // Find in reverse, the entry is more likely to be towards the end. Actually no,
    // for some reason the normal search is slightly faster when measured.
    auto ite = find_if(m_event_counts.begin(), m_event_counts.end(),
                       MatchEventId(event_id));
    if (ite == m_event_counts.end())
    {
        m_event_counts.emplace_back(event_id, m_time_window, m_granularity);
    }
    else
    {
        ite->increment();
        // rotate so that the entry becomes the last one
        auto next = std::next(ite);
        std::rotate(ite, next, m_event_counts.end());
    }

    if (!--m_cleanup_countdown)
    {
        purge();
    }
}

namespace
{
struct NonZeroEntry
{
    bool operator()(const EventCount& stats)
    {
        return stats.count() != 0;
    }
};
}

void SessionCount::purge() const
{
    StopWatch sw;
    m_cleanup_countdown = CleanupCountdown;
    // erase entries up to the first non-zero one
    auto ite = find_if(m_event_counts.begin(), m_event_counts.end(), NonZeroEntry());
    // The gcc 4.4 vector::erase bug only happens if iterators are the same.
    if (ite != m_event_counts.begin())
    {
        m_event_counts.erase(m_event_counts.begin(), ite);
    }
}

void SessionCount::dump(std::ostream& os) const
{
    purge();
    if (!m_event_counts.empty())
    {
        os << "  Session: " << m_sess_id << '\n';
        for (auto ite = m_event_counts.begin(); ite != m_event_counts.end(); ++ite)
        {
            os << "    " << *ite << '\n';
        }
    }
}

void dumpHeader(std::ostream& os, const SessionCount& stats, const std::string& type)
{
    TimePoint tp = Clock::now();
    os << type << ": Time:" << tp
       << " Time Window: " << stats.time_window() << '\n';
}

void dump(std::ostream& os, const std::vector<SessionCount>& sessions)
{
    if (sessions.empty())
    {
        return;
    }

    dumpHeader(os, sessions[0], "Count");
    for (auto session = sessions.begin(); session != sessions.end(); ++session)
    {
        session->dump(os);
    }
}

void dumpTotals(std::ostream& os, const std::vector<SessionCount> &sessions)
{
    if (sessions.empty())
    {
        return;
    }

    std::map<std::string, int> counts;
    for (auto session = sessions.begin(); session != sessions.end(); ++session)
    {
        const auto& events = session->event_counts();
        for (auto event = events.begin(); event != events.end(); ++event)
        {
            counts[event->event_id()] += event->count();
        }
    }

    if (!counts.empty())
    {
        dumpHeader(os, sessions[0], "Count Totals");
        for (auto ite = counts.begin(); ite != counts.end(); ++ite)
        {
            os << "  " << ite->first << ": " << ite->second << '\n';
        }
    }
}

// EXTRA
// This section needed for gcc 4.4, to use move semantics and variadics.

EventCount::EventCount(EventCount && ss) :
    m_event_id(std::move(ss.m_event_id)),
    m_time_window(std::move(ss.m_time_window)),
    m_granularity(std::move(ss.m_granularity)),
    m_timestamps(std::move(ss.m_timestamps))
{
}

EventCount &EventCount::operator=(EventCount && ss)
{
    m_event_id = std::move(ss.m_event_id);
    m_time_window = std::move(ss.m_time_window);
    m_granularity = std::move(ss.m_granularity);
    m_timestamps = std::move(ss.m_timestamps);
    return *this;
}

SessionCount::SessionCount(SessionCount&& ss) :
    m_sess_id(std::move(ss.m_sess_id)),
    m_time_window(std::move(ss.m_time_window)),
    m_granularity(std::move(ss.m_granularity)),
    m_cleanup_countdown(std::move(ss.m_cleanup_countdown)),
    m_event_counts(std::move(ss.m_event_counts))
{
}

SessionCount & SessionCount::operator=(SessionCount&& ss)
{
    m_sess_id = std::move(ss.m_sess_id);
    m_time_window = std::move(ss.m_time_window);
    m_granularity = std::move(ss.m_granularity);
    m_cleanup_countdown = std::move(ss.m_cleanup_countdown);
    m_event_counts = std::move(ss.m_event_counts);

    return *this;
}
} // throttle
