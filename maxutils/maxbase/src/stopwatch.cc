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

#include <maxbase/stopwatch.hh>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <ctime>

namespace maxbase
{

StopWatch::StopWatch()
{
    restart();
}

Duration StopWatch::split() const
{
    return {Clock::now() - m_start};
}

Duration StopWatch::lap()
{
    auto now = Clock::now();
    Duration lap = now - m_lap;
    m_lap = now;
    return lap;
}

Duration StopWatch::restart()
{
    TimePoint now = Clock::now();
    Duration split = now - m_start;
    m_start = m_lap = now;
    return split;
}

IntervalTimer::IntervalTimer()
    : m_total(0)
{
}

void IntervalTimer::start_interval()
{
    m_last_start = Clock::now();
}

void IntervalTimer::end_interval()
{
    if (m_last_start == maxbase::TimePoint())
    {
        // m_last_start is defaulted. Ignore, avoids extra logic at call sites.
        return;
    }

    m_total += Clock::now() - m_last_start;
    // reset to make it easier to spot usage bugs, like calling end_interval(); end_interval();
    m_last_start = TimePoint();
}

Duration IntervalTimer::total() const
{
    return m_total;
}
}   // maxbase

/********** OUTPUT ***********/
namespace
{
using namespace maxbase;
struct TimeConvert
{
    double      div;        // divide the value of the previous unit by this
    std::string suffix;     // milliseconds, hours etc.
    double      max_visual; // threashold to switch to the next unit
};
// Will never get to centuries because the duration is a long carrying nanoseconds
TimeConvert convert[]
{
    {1, "ns", 1000}, {1000, "us", 1000}, {1000, "ms", 1000},
    {1000, "s", 60}, {60, "min", 60}, {60, "hours", 24},
    {24, "days", 365.25}, {365.25, "years", 10000},
    {100, "centuries", std::numeric_limits<double>::max()}
};

int convert_size = sizeof(convert) / sizeof(convert[0]);
}

namespace maxbase
{

std::pair<double, std::string> dur_to_human_readable(Duration dur)
{
    using namespace std::chrono;
    double time = duration_cast<nanoseconds>(dur).count();
    bool negative = (time < 0) ? time = -time, true : false;

    for (int i = 0; i <= convert_size; ++i)
    {
        if (i == convert_size)
        {
            return std::make_pair(negative ? -time : time,
                                  convert[convert_size - 1].suffix);
        }

        time /= convert[i].div;

        if (time < convert[i].max_visual)
        {
            return std::make_pair(negative ? -time : time, convert[i].suffix);
        }
    }

    abort();    // should never get here
}

std::string to_string(Duration dur, const std::string& sep)
{
    auto p = dur_to_human_readable(dur);
    std::ostringstream os;
    os << p.first << sep << p.second;

    return os.str();
}

std::ostream& operator<<(std::ostream& os, Duration dur)
{
    auto p = dur_to_human_readable(dur);
    os << p.first << p.second;

    return os;
}

// TODO: this will require some thought. to_string() for a system_clock is
// obvious, but not so for a steady_clock. Maybe TimePoint belongs to a system clock
// and sould be called something else here, and live in a time_measuring namespace.
std::string to_string(TimePoint tp, const std::string& fmt)
{
    using namespace std::chrono;
    std::time_t timet = system_clock::to_time_t(system_clock::now() + (tp - Clock::now()));

    struct tm* ptm;
    ptm = gmtime (&timet);
    const int sz = 1024;
    char buf[sz];
    strftime(buf, sz, fmt.c_str(), ptm);
    return buf;
}

std::ostream& operator<<(std::ostream& os, TimePoint tp)
{
    os << to_string(tp);
    return os;
}

void test_stopwatch_output(std::ostream& os)
{
    long long dur[] =
    {
        400,                                // 400ns
        5 * 1000,                           // 5us
        500 * 1000,                         // 500us
        1 * 1000000,                        // 1ms
        700 * 1000000LL,                    // 700ms
        5 * 1000000000LL,                   // 5s
        200 * 1000000000LL,                 // 200s
        5 * 60 * 1000000000LL,              // 5m
        45 * 60 * 1000000000LL,             // 45m
        130 * 60 * 1000000000LL,            // 130m
        24 * 60 * 60 * 1000000000LL,        // 24 hours
        3 * 24 * 60 * 60 * 1000000000LL,    // 72 hours
        180 * 24 * 60 * 60 * 1000000000LL,  // 180 days
        1000 * 24 * 60 * 60 * 1000000000LL  // 1000 days
    };

    for (unsigned i = 0; i < sizeof(dur) / sizeof(dur[0]); ++i)
    {
        os << Duration(dur[i]) << std::endl;
    }
}
}
