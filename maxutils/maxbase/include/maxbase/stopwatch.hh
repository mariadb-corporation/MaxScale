/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxbase/ccdefs.hh>
#include <chrono>
#include <iosfwd>
#include <string>

namespace maxbase
{

using Clock = std::chrono::steady_clock;

struct Duration : public Clock::duration    // for ADL
{
    using Clock::duration::duration;
    Duration() = default;
    Duration(Clock::duration d) : Clock::duration(d)
    {
    }
    Duration(long long l) : Clock::duration(l)
    {
    }                                               // FIXME. Get rid of this.

    explicit Duration(double secs) : Duration{rep(secs * period::den / period::num)}
    {
    }

    double secs()
    {
        return std::chrono::duration<double>(*this).count();
    }
};

typedef std::chrono::time_point<Clock, Duration> TimePoint;

class StopWatch
{
public:
    // Starts the stopwatch, which is always running.
    StopWatch();
    // Get elapsed time.
    Duration lap() const;
    // Get elapsed time, restart StopWatch.
    Duration restart();
private:
    TimePoint m_start;
};

// Returns the value as a double and string adjusted to a suffix like ms for milliseconds.
std::pair<double, std::string> dur_to_human_readable(Duration dur);

// Human readable output. No standard library for it yet.
std::ostream& operator<<(std::ostream&, Duration dur);

// TimePoint
std::string   time_point_to_string(TimePoint tp, const std::string& fmt = "%F %T");
std::ostream& operator<<(std::ostream&, TimePoint tp);
}   // maxbase
