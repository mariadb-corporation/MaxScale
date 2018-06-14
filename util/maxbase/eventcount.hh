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

#pragma once

#include <base/stopwatch.hh>
#include <iosfwd>
#include <vector>

namespace base
{
/**
 * @brief Keep a count of an events for a time period from "now" into the past.
 *
 * Events are stored, or distinguished, with timestamps of a given granularity.
 * For example, if the granularity is set to 1s, each time an event is reported the current
 * time is rounded down to the nearest whole second. All events that round down to the same
 * second share a single entry in the EventCount. Granularity==0 causes all events (timestamps)
 * to be stored in their own entry, which could use large amounts of memory.
 *
 */
class EventCount
{
public:
    explicit EventCount(const std::string& event_id, Duration time_window,
                        Duration granularity = Duration(std::chrono::milliseconds(10)));
    EventCount(const EventCount&) = delete;
    EventCount& operator=(const EventCount&) = delete;
    EventCount(EventCount&&);  // can't be defaulted in gcc 4.4
    EventCount& operator=(EventCount&&); // can't be defaulted in gcc 4.4

    const std::string& event_id() const
    {
        return m_event_id;
    }
    Duration time_window() const
    {
        return m_time_window;
    }
    void dump(std::ostream& os) const;
    int count() const;
    void increment();

    // these defs need not be public once lambdas are available
    struct Timestamp
    {
        TimePoint time_point;
        int count;
        Timestamp(TimePoint p, int c) : time_point(p), count(c) {}
    };
private:
    void purge() const; // remove out of window stats

    std::string m_event_id;
    Duration m_time_window;
    Duration::rep m_granularity;
    mutable std::vector<Timestamp> m_timestamps;
};

std::ostream& operator<<(std::ostream& os, const EventCount& stats);

// Time series statistics for a Session (a collection of related EventCount).
class SessionCount
{
public:
    SessionCount(const std::string& sess_id, Duration time_window,
                 Duration granularity = Duration(std::chrono::milliseconds(10)));
    SessionCount(const SessionCount&) = delete;
    SessionCount& operator=(const SessionCount&) = delete;
    SessionCount(SessionCount &&);  // can't be defaulted in gcc 4.4
    SessionCount& operator=(SessionCount&&); // can't be defaulted in gcc 4.4

    const std::string& session_id() const
    {
        return m_sess_id;
    }
    Duration time_window() const
    {
        return m_time_window;
    }
    const std::vector<EventCount>& event_counts() const;
    void dump(std::ostream& os) const;
    bool empty() const; // no stats

    void increment(const std::string& event_id);
private:
    void purge() const; // remove out of window stats

    std::string m_sess_id;
    Duration m_time_window;
    Duration m_granularity;
    mutable int m_cleanup_countdown;
    mutable std::vector<EventCount> m_event_counts;
};

// conveniece. Any real formatted output should go elsewhere.
std::ostream& operator<<(std::ostream& os, const SessionCount& stats);
void dump(std::ostream& os, const std::vector<SessionCount>& sessions);
void dumpTotals(std::ostream& os, const std::vector<SessionCount> &sessions);

} // base
